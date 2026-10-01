# Tectonic terrain for cubiomes

`tectonic.h` / `tectonic.c` evaluate the terrain shape of the [Tectonic](https://modrinth.com/mod/tectonic) mod on
Minecraft 26.3 without running the game:

| function | what |
|----------|------|
| `initTectonic(tn, variant, seed)` | seed the 16-17 noises of a variant (`TECTONIC_MOD_3_0_31`, `TECTONIC_DP_3_0_29`) |
| `sampleTectonicOffset(tn, x, z)` | `tectonic:terrain_spline/offset/final`, the base surface offset of a block column |
| `getTectonicHeight(tn, x, z)` | base surface height in blocks, `128 * (1 + offset)`; sea level is 63 |
| `mapTectonicHeight(y, tn, x, z, w, h, step)` | a grid of base heights |
| `mapApproxHeightTectonic(y, ids, tn, x, z, w, h)` | like `mapApproxHeight`: 1:4 scale, heights and optionally biomes |
| `sampleTectonicClimate(tn, np, nv, x, y, z)` | the six climate parameters the biome source sees |
| `getTectonicBiomeAt(tn, scale, x, y, z)`, `sampleTectonicBiome(...)` | biome from that climate (`climateToBiome`, vanilla 26.3 tree) |
| `sampleTectonicBiomeHeight(tn, np, &height, x, y, z, dat)` | biome of a 1:4 cell and base height of its column from one evaluation (for map tiles) |
| `sampleTectonicField(tn, id, x, y, z)` | any named density function, ids via `getTectonicFieldId("tectonic:...")` |
| `genTectonicBiomes(tn, out, r, sha)`, `genTectonicClimate(tn, out, r, nptype)` | biomes / one climate parameter for a `Range` (what `genBiomes` calls) |

## Through the Generator

```C
Generator g;
setupGenerator(&g, MC_26_3, TECTONIC_MOD);       // or TECTONIC_DATAPACK
applySeed(&g, DIM_OVERWORLD, seed);
```

With one of the two flags the Overworld of the `Generator` is Tectonic's:

* `genBiomes` / `getBiomeAt` return Tectonic's biomes at every scale (1:1 with the voronoi zoom, 1:4 exact, larger
  scales sample the cell centre like the vanilla path). If `g.bn.nptype` is set to a climate type, `genBiomes` returns
  that parameter of Tectonic's climate instead (for `NP_DEPTH` the terrain offset), as for vanilla.
* `mapApproxHeight` returns Tectonic's base surface height (and biomes at that height if `ids` is given).
* Nether and End, other versions (the flags are dropped unless the version is 26.3) and Generators without the flag
  behave exactly as before. The vanilla `BiomeNoise` is still seeded, because several finders read it directly
  (`sampleBiomeNoise(&g->bn, ...)`); those finders therefore see vanilla climate, and structure positions in a
  Tectonic world are **unverified**.
* `sizeof(Generator)` grows from 24.6 kB to 71.4 kB, because it embeds a `TectonicNoise`.

The base surface is the terrain before Tectonic's peaks (`jaggedness`) and the 3D density noise are added. Measured
against the real generator: between y 100 and 160 the real surface lies 0-7 blocks above it, on mountain crests the
peaks add up to ~130 blocks. The real surface (final density, aquifers) is **not** part of this module.

About 0.7 million columns per second and thread (heights), a `TectonicNoise` is about 47 kB and read-only after
`initTectonic`, so it can be shared between threads.

## How it is built

* `gen_tectonic.py` reads the density functions from the pack json (the mod's data with its default config resolved to
  vanilla types, or the datapack zip) plus the vanilla 26.3 data, follows everything reachable from
  `tectonic:terrain_spline/offset/final` and the six climate functions of the noise router, and writes
  `../tectonic_gen.h`: one small C function per density function node, spline tables, noise parameters. Nothing is
  copied by hand. Each node performs the float operations of the game's point sampler in the same order
  (`mul` skips its right side when the left side is 0, `clamp` is `Mth.clamp`, `min`/`max` are `Math.min`/`Math.max`,
  `cache`d and named functions are evaluated once per position, ...). Unknown node types make the generator fail.
* `tectonic.c` holds the runtime: MD5 name hashing and Xoroshiro seeding as in `RandomState`, `NormalNoise` /
  `NoiseStack` / `PerlinNoise` in float as in 26.3 (not `noise.c`, which follows the older double semantics), and
  `CubicSpline`.

Regenerate after a Tectonic update (paths are those of the seed-finding project this fork was made in):

```sh
python3 tectonic/gen_tectonic.py --vanilla ../resources/mc/26.3-data \
    --variant mod=../packs/mod/tectonic-mod-3.0.31 --variant dp=../packs/dp/tectonic-datapack-3.0.29.zip > tectonic_gen.h
```

`tectonic_gen.h` is derived from Tectonic's data files: the spline points, noise parameters and the structure of
the density functions are Tectonic's. Tectonic is by Apollo and MIT licensed
(https://github.com/Apollounknowndev/tectonic, https://modrinth.com/mod/tectonic).

## Validation

Ground truth is the real Minecraft 26.3 server code with the Tectonic pack loaded (the `harness/` of the Tectonic
seed-finding project next to this directory).

* `tectonic/validate.sh [points per seed] [seeds...]` compares every named density function of the graph (56 for the
  mod, 52 for the datapack) and the six router climate functions at random positions (x, z from spawn to the world
  border at 29,999,900; y from -64 to 320). Result of `tectonic/validate.sh 20000`, 5 seeds x 2 variants:
  **200,000 positions, 12,000,000 values, all bit-identical** (`max |diff| 0` for every function).
  A second pass does the same through the `Generator` (heights from `mapApproxHeight`, five climate parameters from
  `genBiomes` with `bn.nptype`): **200,000 positions, 1,200,000 values, all identical**.
* `tectonic/validate_biomes.sh` compares biomes at the base surface with the game's `MultiNoiseBiomeSource`
  (through the explorer's tile server). 4 seeds x 2 variants, 128,000 columns: **127,937 identical (99.95%)**, both
  with `getTectonicBiomeAt` and with `getBiomeAt(&g, 4, ...)`. Biomes at 1:1 (`getBiomeAt(&g, 1, ...)`, voronoi zoom)
  against the game's `BiomeManager`: **127,949 of 128,000 identical**. All differences are exact ties: the climate lies
  exactly on the border between the parameter ranges of two biomes, both are equally close, and the game takes
  whichever its search meets first (or its previous result) while cubiomes' 26.3 tree picks the other. Tectonic's
  splines output such border values (e.g. continentalness exactly -0.11) far more often than vanilla noise does.
* `tests/test_tectonic.c` (ctest) checks 288 reference positions printed by the game, bit for bit, a few biomes, and
  the `Generator` hook against the direct functions (all scales, climate parameters, flag handling);
  `tectonic/gen_test_reference.sh` regenerates the reference table.

Compile `tectonic.c` without floating point contraction. `CMakeLists.txt` passes `-ffp-contract=off` for that file
and the source carries pragmas for other build systems; they hold for GCC with `-march=native -ffp-contract=fast` and
for clang's default mode, but clang with an explicit `-ffp-contract=fast` ignores them (the test then fails).

## tectonic_map

```
tectonic_map <seed> [-v mod|dp] [-c x z] [-s blocks/px] [-n w h] [-m y] [-b] [-o out.ppm]
```

renders a shaded height map (`-b`: biome map, `-m 150`: outline mountains) as a PPM image, e.g. the 100,000 block
mountain range of seed 93929:

```sh
build/tectonic/tectonic_map 93929 -c -21182484 -22681584 -s 64 -n 1024 768 -m 150 -o range.ppm
```

## Not included

* The real surface with peaks, caves and aquifers (3D final density).
* Structures and features in a Tectonic world: the finders are unchanged; with a Tectonic `Generator` their biome
  checks see Tectonic's biomes, but terrain checks and direct climate reads are vanilla. None of it is verified.
* Other Tectonic versions or non-default mod configs (regenerate `tectonic_gen.h` from the corresponding pack).
