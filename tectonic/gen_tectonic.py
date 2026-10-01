#!/usr/bin/env python3
"""Generate tectonic_gen.h: C code for Tectonic's 2D terrain / climate density functions.

  gen_tectonic.py --vanilla <26.3 data dir> --variant mod=<pack dir|zip> --variant dp=<pack dir|zip> > tectonic_gen.h

Nothing is copied by hand: every density function reachable from the roots (base surface offset and the six
climate parameters of the noise router) is read from the pack json (falling back to the vanilla data pack for
`minecraft:` ids the pack does not override) and translated node by node into a C function that performs the
same float operations, in the same order, as the sampleValue() path of Minecraft 26.3's compiled samplers
(net.minecraft.world.level.levelgen.densityfunction.op.* / generator.*). Splines become tables walked by
tectSplineEval() (CubicSpline.Multipoint.sample), noises become entries of a parameter table that
tectonic.c seeds exactly like RandomState / NormalNoise.create.

A pack "dir" is the directory that contains data/ (or a datapack .zip). The mod's own density function types
(tectonic:config_*) must already be resolved to vanilla types, see tools/make_mod_pack.py in the Tectonic
seed-finding project.
"""
import argparse, json, os, re, struct, sys, zipfile

ROOT_OFFSET = "tectonic:terrain_spline/offset/final"
ROUTER_KEYS = ["temperature", "vegetation", "continents", "erosion", "depth", "ridges"]   # Climate.Sampler order


def f32(x):
    return struct.unpack("f", struct.pack("f", float(x)))[0]


def flit(x):
    """C float literal that parses back to exactly this float32."""
    x = f32(x)
    if x != x or x in (float("inf"), float("-inf")):
        raise ValueError("non-finite constant")
    r = repr(x)
    if "e" in r and "." not in r:
        m, e = r.split("e")
        r = m + ".0e" + e
    elif "." not in r and "e" not in r:
        r += ".0"
    return r + "f"


def dlit(x):
    r = repr(float(x))
    if "." not in r and "e" not in r and "n" not in r:
        r += ".0"
    return r


class Source:
    """Reads worldgen json from a pack (directory or zip) with a fallback to the vanilla data."""

    def __init__(self, pack, vanilla):
        self.pack, self.vanilla = pack, vanilla
        self.zip = zipfile.ZipFile(pack) if pack.endswith(".zip") else None

    def _read(self, rel):
        if self.zip is not None:
            try:
                return json.loads(self.zip.read(rel))
            except KeyError:
                pass
        else:
            p = os.path.join(self.pack, rel)
            if os.path.exists(p):
                return json.load(open(p))
        p = os.path.join(self.vanilla, rel)
        if os.path.exists(p):
            return json.load(open(p))
        raise FileNotFoundError(rel)

    def get(self, kind, ident):
        ns, path = ident.split(":")
        return self._read("data/%s/worldgen/%s/%s.json" % (ns, kind, path))


class Variant:
    def __init__(self, name, src):
        self.name, self.src, self.p = name, src, "t" + name + "_"
        self.funcs = []          # (cname, body lines, slot or None, comment)
        self.named = {}          # density function id -> cname
        self.noises = []         # noise ids, index = slot in TectonicNoise.noise[]
        self.nslots = 0
        self.tables = []         # spline table text
        self.coords = []         # cnames used as spline coordinates
        self.counter = 0
        self.nlayers = 0             # Perlin layers of all noises together

    # ---------------------------------------------------------------- helpers
    def new_name(self, hint):
        self.counter += 1
        return "%sn%d_%s" % (self.p, self.counter, re.sub(r"[^a-z0-9]+", "_", hint.lower())[:24])

    def slot(self):
        self.nslots += 1
        return self.nslots - 1

    def noise_index(self, ident):
        if ident not in self.noises:
            self.noises.append(ident)
        return self.noises.index(ident)

    def resolve(self, node):
        """Follow references until something that is not a plain id (what INLINE_REFERENCE does)."""
        seen = 0
        while isinstance(node, str):
            node = self.src.get("density_function", node)
            seen += 1
            assert seen < 50
        return node

    def const_of(self, node):
        """float value if the node compiles to a ConstantFunction, else None."""
        node = self.resolve(node)
        if isinstance(node, (int, float)) and not isinstance(node, bool):
            return f32(node)
        if isinstance(node, dict) and node.get("type", "").replace("minecraft:", "") == "constant":
            return f32(node["value"])
        return None

    def emit(self, hint, body, memo=False, comment=""):
        cname = self.new_name(hint)
        self.funcs.append([cname, body, self.slot() if memo else None, comment])
        return cname

    # ---------------------------------------------------------------- node -> expression
    def expr(self, node):
        """C expression (a float literal or a call) for a density function node."""
        c = self.const_of(node)
        if c is not None:
            return flit(c)
        return self.func(node) + "(e)"

    def func(self, node, force_memo=False):
        """Name of the C function evaluating this node."""
        if isinstance(node, str):
            if node not in self.named:
                self.named[node] = None                      # guards against reference cycles
                inner = self.src.get("density_function", node)
                if isinstance(inner, str):
                    cname = self.func(inner)
                else:
                    cname = self.build(inner, node, memo=True)
                self.named[node] = cname
                self.ids_in_order.append(node)
            assert self.named[node] is not None, "cyclic reference " + node
            return self.named[node]
        return self.build(node, None, memo=force_memo)

    def build(self, node, ident, memo):
        c = self.const_of(node)
        if c is not None:
            return self.emit(ident or "const", ["return %s;" % flit(c)], False, ident or "")
        t = node["type"].replace("minecraft:", "")
        hint = ident.split(":")[1] if ident else t
        cm = ident or ""
        E = self.expr
        if t == "cache":
            # CacheFunction: pure memoisation of its input
            inner = node["input"]
            if isinstance(inner, str) or self.const_of(inner) is not None:
                return self.emit(hint, ["return %s;" % E(inner)], True, cm)
            return self.build(inner, ident, memo=True)
        if t in ("add", "sub"):
            op = "+" if t == "add" else "-"
            return self.emit(hint, ["return %s %s %s;" % (E(node["left"]), op, E(node["right"]))], memo, cm)
        if t == "mul":
            cl, cr = self.const_of(node["left"]), self.const_of(node["right"])
            if cl is not None:      # ConstMulSampler(right, left)
                return self.emit(hint, ["return %s * %s;" % (E(node["right"]), flit(cl))], memo, cm)
            if cr is not None:      # ConstMulSampler(left, right)
                return self.emit(hint, ["return %s * %s;" % (E(node["left"]), flit(cr))], memo, cm)
            # MulSampler: the right side is not evaluated when the left side is zero
            return self.emit(hint, ["float l = %s;" % E(node["left"]), "return l == 0.0f ? 0.0f : l * %s;" % E(node["right"])], memo, cm)
        if t in ("min", "max"):
            return self.emit(hint, ["return tectJava%s(%s, %s);" % (t.capitalize(), E(node["left"]), E(node["right"]))], memo, cm)
        if t == "abs":
            return self.emit(hint, ["return fabsf(%s);" % E(node["input"])], memo, cm)
        if t == "negate":
            return self.emit(hint, ["return -%s;" % E(node["input"])], memo, cm)
        if t == "square":
            return self.emit(hint, ["float v = %s;" % E(node["input"]), "return v * v;"], memo, cm)
        if t == "cube":
            return self.emit(hint, ["float v = %s;" % E(node["input"]), "return v * v * v;"], memo, cm)
        if t == "half_negative" or t == "quarter_negative":
            k = "0.5f" if t == "half_negative" else "0.25f"
            return self.emit(hint, ["float v = %s;" % E(node["input"]), "return v > 0.0f ? v : v * %s;" % k], memo, cm)
        if t == "clamp":
            # Mth.clamp(value, min, max) = value < min ? min : Math.min(value, max)
            return self.emit(hint, ["float v = %s;" % E(node["input"]),
                                    "return v < %s ? %s : tectJavaMin(v, %s);" % (flit(node["min"]), flit(node["min"]), flit(node["max"]))], memo, cm)
        if t == "range_choice":
            return self.emit(hint, ["float v = %s;" % E(node["input"]),
                                    "return (v >= %s && v < %s) ? %s : %s;" % (flit(node["min_inclusive"]), flit(node["max_exclusive"]),
                                                                              E(node["when_in_range"]), E(node["when_out_of_range"]))], memo, cm)
        if t == "lerp":
            return self.emit(hint, ["float a = %s;" % E(node["alpha"]),
                                    "if (a == 0.0f) return %s;" % E(node["first"]),
                                    "if (a == 1.0f) return %s;" % E(node["second"]),
                                    "{ float f = %s, s = %s; return f + a * (s - f); }" % (E(node["first"]), E(node["second"]))], memo, cm)
        if t == "blend_alpha":      # no Blender in a freshly generated world
            return self.emit(hint, ["return 1.0f;"], False, cm)
        if t == "blend_offset":
            return self.emit(hint, ["return 0.0f;"], False, cm)
        if t == "noise":
            k = self.noise_index(node["noise"])
            xz, ys = float(node["xz_scale"]), float(node["y_scale"])
            def shift(key):
                s = node.get(key)
                if s is None or self.const_of(s) == 0.0:
                    return ""
                return " + (double)%s" % E(s)
            if node.get("shift_y") is not None and self.const_of(node["shift_y"]) != 0.0:
                ysh = shift("shift_y")
            else:
                ysh = ""
            body = ["double x = (double)e->x * %s%s;" % (dlit(xz), shift("shift_x")),
                    "double y = (double)e->y * %s%s;" % (dlit(ys), ysh),
                    "double z = (double)e->z * %s%s;" % (dlit(xz), shift("shift_z")),
                    "return tectNoiseGet(e->tn, %d, x, y, z);" % k]
            return self.emit(hint, body, memo, cm + "  noise " + node["noise"])
        if t in ("shift_a", "shift_b", "shift"):
            k = self.noise_index(node["noise"] if "noise" in node else node["argument"])
            args = {"shift_a": "(double)e->x * 0.25, 0.0, (double)e->z * 0.25",
                    "shift_b": "(double)e->z * 0.25, (double)e->x * 0.25, 0.0",
                    "shift": "(double)e->x * 0.25, (double)e->y * 0.25, (double)e->z * 0.25"}[t]
            return self.emit(hint, ["return tectNoiseGet(e->tn, %d, %s) * 4.0f;" % (k, args)], memo, cm)
        if t == "gradient":
            assert node.get("tiling", "clamp_to_edge").replace("minecraft:", "") == "clamp_to_edge", "gradient tiling"
            fc, tc = int(node["from_coordinate"]), int(node["to_coordinate"])
            fv, tv = f32(node["from_value"]), f32(node["to_value"])
            factor = f32(f32(tv - fv) / f32(tc - fc))
            lo, hi = min(fc, tc), max(fc, tc)
            ax = node["axis"]
            return self.emit(hint, ["int c = e->%s;" % ax, "c = c < %d ? %d : (c > %d ? %d : c);" % (lo, lo, hi, hi),
                                    "return %s + (float)(c - %d) * %s;" % (flit(fv), fc, flit(factor))], memo, cm)
        if t == "spline":
            return self.spline(node["spline"], hint, memo, cm)
        raise NotImplementedError("density function type %r (in %s)" % (t, ident))

    def spline(self, sp, hint, memo, cm):
        if not isinstance(sp, dict):       # CubicSpline.Constant
            return self.emit(hint, ["return %s;" % flit(sp)], False, cm)
        nodes, points = [], []

        def add(s):
            idx = len(nodes)
            nodes.append(None)
            coord = self.func(s["coordinate"], force_memo=True)
            if coord not in self.coords:
                self.coords.append(coord)
            pts = []
            for p in s["points"]:
                v = p["value"]
                if isinstance(v, dict):
                    pts.append((p["location"], p["derivative"], 0, 0.0, add(v)))
                else:
                    pts.append((p["location"], p["derivative"], 1, v, 0))
            first = len(points)
            points.extend(pts)
            nodes[idx] = (self.coords.index(coord), first, len(pts))
            return idx

        add(sp)
        tn = "%ss%d" % (self.p, len(self.tables))
        txt = ["static const TectSplinePoint %s_pts[] = {" % tn]
        txt += ["    {%s, %s, %d, %s, %d}," % (flit(l), flit(d), isc, flit(v), ch) for l, d, isc, v, ch in points]
        txt += ["};", "static const TectSplineNode %s_nodes[] = {" % tn]
        txt += ["    {%d, %d, %d}," % nd for nd in nodes]
        txt += ["};"]
        self.tables.append("\n".join(txt))
        return self.emit(hint, ["return tectSplineEval(%s_nodes, %s_pts, %scoords, 0, e);" % (tn, tn, self.p)], memo, cm)

    # ---------------------------------------------------------------- driver
    def generate(self):
        self.ids_in_order = []
        settings = self.src.get("noise_settings", "minecraft:overworld")
        router = settings["noise_router"]
        self.roots = [self.func(ROOT_OFFSET)]
        for k in ROUTER_KEYS:
            v = router[k]
            self.roots.append(self.func(v) if isinstance(v, str) or self.const_of(v) is None else self.build(v, None, False))
        self.sea_level = int(settings["sea_level"])

    def noise_params(self, ident):
        d = self.src.get("noise", ident)
        n = int(d.get("octave_count", 1))
        mods = [float(m) for m in d.get("amplitude_modifiers", [])]
        assert not mods or len(mods) == n
        norm = d.get("normalize", True)
        assert norm in (True, "legacy"), "normalize: %r not supported" % (norm,)
        used = sum(1 for i in range(n) if (mods[i] if mods else 1.0) != 0.0)
        assert n <= 16, "too many octaves in " + ident
        self.nlayers += 2 * used
        return '{"%s", %d, %d, %s, %d, {%s}}' % (ident, int(d["base_octave"]), n, dlit(d.get("base_amplitude", 1.0)),
                                                 1 if norm == "legacy" else 0, ", ".join(dlit(m) for m in (mods or [1.0] * n)))

    def write(self, out, all_ids):
        p = self.p
        out.append("/* ------------------------------------------------------------------ variant %s: %s */" % (self.name, os.path.basename(self.src.pack.rstrip("/"))))
        out.append("static const TectNoiseParams %snoise_params[] = {" % p)
        out += ["    %s," % self.noise_params(n) for n in self.noises]
        out.append("};")
        out += ["static float %s(TectEval *e);" % f[0] for f in self.funcs]
        out.append("static float (*const %scoords[])(TectEval *) = {" % p)
        out += ["    %s," % c for c in self.coords] or ["    0,"]
        out.append("};")
        out += self.tables
        for cname, body, slot, comment in self.funcs:
            cm = ("   /* %s */" % comment) if comment else ""
            unused = not any("e->" in b or "(e)" in b or ", e)" in b for b in body)
            lines = (["    (void)e;"] if unused else []) + ["    " + b for b in body]
            if slot is None:
                out += ["static float %s(TectEval *e)%s" % (cname, cm), "{"] + lines + ["}"]
            else:       # memoised: computed at most once per sampled position
                out += ["static inline float %s_(TectEval *e)%s" % (cname, cm), "{"] + lines + ["}"]
                out += ["static float %s(TectEval *e)" % cname, "{",
                        "    if (!e->have[%d]) { e->val[%d] = %s_(e); e->have[%d] = 1; }" % (slot, slot, cname, slot),
                        "    return e->val[%d];" % slot, "}"]
        out.append("static float (*const %sfields[])(TectEval *) = {" % p)
        out += ["    %s," % (self.named.get(i) or "0") for i in all_ids]
        out.append("};")
        out.append("static float (*const %sroots[7])(TectEval *) = {%s};" % (p, ", ".join(self.roots)))
        out.append("")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vanilla", required=True)
    ap.add_argument("--variant", action="append", required=True, help="name=pack dir or zip (in TectonicVariant order)")
    a = ap.parse_args()
    variants = []
    for spec in a.variant:
        name, path = spec.split("=", 1)
        v = Variant(name, Source(path, a.vanilla))
        v.generate()
        variants.append(v)
    all_ids = []
    for v in variants:
        for i in v.ids_in_order:
            if i not in all_ids:
                all_ids.append(i)
    all_ids.sort()
    out = ["/* generated by tectonic/gen_tectonic.py -- do not edit.",
           " * Derived from the data files of Tectonic (https://github.com/Apollounknowndev/tectonic) by Apollo, MIT licensed.",
           " * Sources: " + ", ".join("%s = %s" % (v.name, os.path.basename(v.src.pack.rstrip("/"))) for v in variants) + " */", ""]
    out.append("#define TECT_NFIELDS %d" % len(all_ids))
    out.append("#define TECT_GEN_SLOTS %d" % max(v.nslots for v in variants))
    out.append("#define TECT_GEN_NOISES %d" % max(len(v.noises) for v in variants))
    out.append("#define TECT_GEN_LAYERS @LAYERS@")
    out.append("static const char *const tect_field_names[TECT_NFIELDS] = {")
    out += ['    "%s",' % i for i in all_ids]
    out.append("};")
    out.append("")
    for v in variants:
        v.write(out, all_ids)
    out.append("static const TectVariant tect_variants[] = {")
    for v in variants:
        out.append("    {%snoise_params, %d, %sfields, %sroots, %d}," % (v.p, len(v.noises), v.p, v.p, v.sea_level))
    out.append("};")
    out.append("#define TECT_NVARIANTS %d" % len(variants))
    sys.stdout.write("\n".join(out).replace("@LAYERS@", str(max(v.nlayers for v in variants))) + "\n")
    for v in variants:
        sys.stderr.write("%s: %d named functions, %d C functions, %d splines, %d noises (%d Perlin layers), %d memo slots\n"
                         % (v.name, len(v.ids_in_order), len(v.funcs), len(v.tables), len(v.noises), v.nlayers, v.nslots))


if __name__ == "__main__":
    main()
