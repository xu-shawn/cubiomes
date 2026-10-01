#!/bin/bash
# Compares getTectonicBiomeAt() with the biomes chosen by the real game (MultiNoiseBiomeSource, overworld preset,
# Tectonic's climate) at the base surface of random columns.
#
#   tectonic/validate_biomes.sh [url of a running explorer tile server=http://127.0.0.1:8123] [grids per case=8]
#
# Ground truth comes from the tile server of the Tectonic seed-finding project (../explorer/run.sh), whose
# fn=biome_seq grids are produced by the game's own biome source, row by row on a fresh thread.
# The climate itself is bit-exact (validate.sh). Where two biomes fit a climate equally well (their parameter
# boxes touch and the value lies exactly on the border, which Tectonic's splines produce far more often than
# vanilla noise) the game takes whichever its search meets first, or its previous result; cubiomes' tree does not
# always resolve such ties the same way. Exit status 0 if every difference is such an exact tie.
set -o pipefail
cd "$(dirname "$0")/.." || exit 1
URL=${1:-http://127.0.0.1:8123}
GRIDS=${2:-8}
CHECK=${CHECK:-build/tectonic/tectonic_check}
[ -x "$CHECK" ] || { echo "build first: cmake -B build && cmake --build build" >&2; exit 1; }
rc=0
for v in mod dp; do
  for s in 12345 93929 -7046029254386353131 62232235; do
    echo "== $v seed $s"
    python3 - "$URL" $v $s "$GRIDS" <<'PY' | "$CHECK" $v $s | grep -vE "^0 points|^OK|^MISMATCH" || rc=1
import json, random, struct, sys, urllib.request, zlib, math
url, pack, seed, grids = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
f32 = lambda x: struct.unpack('f', struct.pack('f', x))[0]
names = json.load(urllib.request.urlopen("%s/api/biomes?pack=%s" % (url, pack)))
r = random.Random(zlib.crc32((pack + seed).encode()))
for i in range(grids):
    span = [3000, 50000, 1000000, 29000000][i % 4]
    step = r.choice([1, 3, 16, 100, 1000])
    x0, z0 = r.randint(-span, span), r.randint(-span, span)
    q = "%s/api/grid?pack=%s&seed=%s&x0=%d&z0=%d&step=%d&nx=50&nz=40&fmt=txt&fn=" % (url, pack, seed, x0, z0, step)
    biome = urllib.request.urlopen(q + "biome_seq").read().decode().split("\n")
    base = urllib.request.urlopen(q + "base").read().decode().split("\n")
    for b, o in zip(biome, base):
        if not b: continue
        x, z, idx = b.split(); off = float(o.split()[2])
        y = max(63, math.floor(f32(128.0 * f32(1.0 + off))))       # where the tile server samples the biome
        print("B", x, y, z, names[int(float(idx))])
PY
  done
done
[ $rc = 0 ] && echo "ALL OK: biomes identical except for exact ties" || echo "MISMATCH"
exit $rc
