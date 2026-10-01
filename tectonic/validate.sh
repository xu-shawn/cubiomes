#!/bin/bash
# Compares tectonic.c with the real Minecraft 26.3 generator, value by value, on random block positions.
#
#   tectonic/validate.sh [points per seed=20000] [seed ...]
#
# Ground truth is the harness of the Tectonic seed-finding project this fork lives in (../harness/run.sh, which
# boots the real 26.3 server classes with the Tectonic pack and prints density function values). Every named
# density function of the generated graph and the six climate parameters of the noise router are compared, for
# both variants; positions range from spawn to the world border, y from -64 to 320. A second pass checks the
# Generator hook (setupGenerator with TECTONIC_MOD / TECTONIC_DATAPACK): heights from mapApproxHeight() and climate
# parameters from genBiomes().
# Requires a build (cmake -B build && cmake --build build). Exit status 0 only if everything is bit-identical.
cd "$(dirname "$0")/.." || exit 1
N=${1:-20000}; shift
SEEDS=${@:-"12345 -7046029254386353131 0 987654321987654321 93929"}
HARNESS=$(realpath "${HARNESS:-../harness/run.sh}")
CHECK=${CHECK:-build/tectonic/tectonic_check}
[ -x "$CHECK" ] || { echo "build first: cmake -B build && cmake --build build" >&2; exit 1; }
[ -x "$HARNESS" ] || { echo "harness not found at $HARNESS" >&2; exit 1; }
export JAVA_OPTS="${JAVA_OPTS:--Xmx3g -XX:+UseParallelGC -XX:ActiveProcessorCount=3}"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
python3 - "$N" > "$TMP/points.txt" <<'PY'
import random, sys
n = int(sys.argv[1]); r = random.Random(20261001)
for i in range(n):
    span = [3000, 50000, 1000000, 29999900][i % 4]      # near spawn ... the world border
    print(r.randint(-span, span), r.randint(-64, 320), r.randint(-span, span))
PY
# the same positions rounded to 1:4 cells, for the Generator path (mapApproxHeight / genBiomes with a climate type)
awk '{print int($1/4)*4, int($2/4)*4, int($3/4)*4}' "$TMP/points.txt" > "$TMP/points4.txt"
GIDS="tectonic:terrain_spline/offset/final,router:temperature,router:vegetation,router:continents,router:erosion,router:ridges"
rc=0
for v in mod dp; do
  for s in $SEEDS; do
    IDS=$("$CHECK" $v $s --fields)
    echo "== $v seed $s"
    # the game logs to stdout too; data lines are the ones that start with a coordinate
    (cd "$TMP" && "$HARNESS" $v points $s "$IDS" < "$TMP/points.txt" 2>/dev/null) | grep -E '^-?[0-9]+ -?[0-9]+ -?[0-9]+ ' \
      | "$CHECK" $v $s "$IDS" > "$TMP/out.txt" || rc=1
    if [ -n "$VERBOSE" ]; then cat "$TMP/out.txt"; else grep -E "points|MISMATCH|diff|^OK" "$TMP/out.txt"; grep -E "offset/final|router:" "$TMP/out.txt"; fi
    (cd "$TMP" && "$HARNESS" $v points $s "$GIDS" < "$TMP/points4.txt" 2>/dev/null) | grep -E '^-?[0-9]+ -?[0-9]+ -?[0-9]+ ' \
      | "$CHECK" -g $v $s "$GIDS" > "$TMP/out.txt" || rc=1
    cat "$TMP/out.txt"
  done
done
[ $rc = 0 ] && echo "ALL OK: tectonic.c == Minecraft 26.3 + Tectonic on every compared value" || echo "MISMATCH"
exit $rc
