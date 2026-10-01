#include "testing.h"
#include "../tectonic.h"
#include "../generator.h"

#include <math.h>
#include <stdlib.h>

#include "tectonic_reference.h"

static uint32_t float_bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

/* every reference value was printed by the real 26.3 generator; the port has to match bit for bit */
static int test_tectonic_reference() {
    int ret = 0;

    TectonicNoise *tn = malloc(sizeof *tn);
    int n = sizeof(tectonic_reference) / sizeof(tectonic_reference[0]);
    int variant = -1;
    int64_t seed = 0;
    for (int i = 0; i < n; i++) {
        if (tectonic_reference[i].variant != variant || tectonic_reference[i].seed != seed) {
            variant = tectonic_reference[i].variant;
            seed = tectonic_reference[i].seed;
            ASSERT_EQ(ret, initTectonic(tn, variant, (uint64_t) seed), 0);
        }
        int x = tectonic_reference[i].x, y = tectonic_reference[i].y, z = tectonic_reference[i].z;
        ASSERT_EQ(ret, float_bits(sampleTectonicOffset(tn, x, z)), tectonic_reference[i].offset);

        int64_t np[6];
        float nv[6];
        sampleTectonicClimate(tn, np, nv, x, y, z);
        for (int k = 0; k < 6; k++) {
            ASSERT_EQ(ret, float_bits(nv[k]), tectonic_reference[i].climate[k]);
            ASSERT_EQ(ret, np[k], (int64_t) (nv[k] * 10000.0F));
        }
    }
    ASSERT_TRUE(ret, n >= 200);
    free(tn);

    return ret;
}

static int test_tectonic_height_and_biomes() {
    int ret = 0;

    TectonicNoise *tn = malloc(sizeof *tn);

    /* a crest of the 100k block mountain range of seed 93929, mod variant */
    ASSERT_EQ(ret, initTectonic(tn, TECTONIC_MOD_3_0_31, 93929), 0);
    ASSERT_EQ(ret, float_bits(sampleTectonicOffset(tn, -21198424, -22681504)), float_bits(0.17251337F));
    ASSERT_EQ(ret, (int) (getTectonicHeight(tn, -21198424, -22681504) * 100.0F), 15008);
    ASSERT_EQ(ret, getTectonicBiomeAt(tn, 1, -21198424, 150, -22681504), snowy_slopes);
    ASSERT_EQ(ret, getTectonicBiomeAt(tn, 4, -21196484 >> 2, 63 >> 2, -22681024 >> 2), lukewarm_ocean);

    float y[6];
    int ids[6];
    ASSERT_EQ(ret, mapTectonicHeight(y, tn, -21198424, -22681504, 3, 2, 64), 0);
    ASSERT_EQ(ret, float_bits(y[0]), float_bits(128.0F * (1.0F + 0.17251337F)));
    ASSERT_EQ(ret, float_bits(y[1]), float_bits(128.0F * (1.0F + -0.22614098F)));
    ASSERT_EQ(ret, float_bits(y[4]), float_bits(128.0F * (1.0F + -0.6441841F)));
    ASSERT_EQ(ret, mapApproxHeightTectonic(y, ids, tn, -21198424 >> 2, -22681504 >> 2, 3, 2), 0);
    ASSERT_EQ(ret, float_bits(y[0]), float_bits(getTectonicHeight(tn, -21198424, -22681504)));
    ASSERT_EQ(ret, ids[0], snowy_slopes);

    /* the datapack builds slightly different continents */
    ASSERT_EQ(ret, initTectonic(tn, TECTONIC_DP_3_0_29, 93929), 0);
    ASSERT_EQ(ret, float_bits(sampleTectonicOffset(tn, -21198424, -22681504)), float_bits(0.032791436F));

    ASSERT_EQ(ret, initTectonic(tn, TECTONIC_DP_3_0_29, 12345), 0);
    ASSERT_EQ(ret, getTectonicBiomeAt(tn, 1, 0, 74, 0), dark_forest);
    ASSERT_EQ(ret, getTectonicBiomeAt(tn, 1, 1000, 63, -2000), taiga);

    ASSERT_EQ(ret, initTectonic(tn, TECTONIC_MOD_3_0_31, 62232235), 0);
    ASSERT_EQ(ret, getTectonicBiomeAt(tn, 1, 14424, 197, 8), jagged_peaks);

    /* named density functions */
    int raw = getTectonicFieldId("tectonic:noise/raw_continents");
    ASSERT_TRUE(ret, raw >= 0);
    ASSERT_EQ(ret, getTectonicFieldId("tectonic:no/such/function"), -1);
    ASSERT_TRUE(ret, sampleTectonicField(tn, raw, 14424, 0, 8) > 0.0F);
    ASSERT_NONZERO(ret, initTectonic(tn, 99, 0));

    free(tn);

    return ret;
}

/* setupGenerator() with a TECTONIC_* flag routes the usual entry points to Tectonic */
static int test_tectonic_generator() {
    int ret = 0;

    TectonicNoise *tn = malloc(sizeof *tn);
    Generator *g = malloc(sizeof *g);
    Generator *v = malloc(sizeof *v);
    const int64_t seed = 93929;
    const int x = -21198424, z = -22681504;

    ASSERT_EQ(ret, initTectonic(tn, TECTONIC_MOD_3_0_31, (uint64_t) seed), 0);
    setupGenerator(g, MC_26_3, TECTONIC_MOD);
    applySeed(g, DIM_OVERWORLD, (uint64_t) seed);
    ASSERT_TRUE(ret, (g->flags & TECTONIC_MOD) != 0);

    /* biomes at 1:4 and height */
    ASSERT_EQ(ret, getBiomeAt(g, 4, x >> 2, 150 >> 2, z >> 2), snowy_slopes);
    float y = 0;
    ASSERT_EQ(ret, mapApproxHeight(&y, NULL, g, NULL, x >> 2, z >> 2, 1, 1), 0);
    ASSERT_EQ(ret, float_bits(y), float_bits(getTectonicHeight(tn, x, z)));

    /* an area at every scale: 1:4 is the plain sample, larger scales sample the cell centre */
    for (int scale = 4; scale <= 256; scale *= 4) {
        Range r = {scale, (x >> 2) / (scale / 4), (z >> 2) / (scale / 4), 5, 4, 160 >> 2, 1};
        int *ids = allocCache(g, r);
        ASSERT_EQ(ret, genBiomes(g, ids, r), 0);
        int mid = (scale / 4) / 2;
        for (int j = 0; j < r.sz; j++) {
            for (int i = 0; i < r.sx; i++) {
                int x4 = (r.x + i) * (scale / 4) + mid, z4 = (r.z + j) * (scale / 4) + mid;
                ASSERT_EQ(ret, ids[j * r.sx + i], getTectonicBiomeAt(tn, 4, x4, r.y, z4));
            }
        }
        free(ids);
    }

    /* 1:1 applies the voronoi zoom on top of the 1:4 biomes */
    Range r1 = {1, x, z, 9, 7, 150, 1};
    int *ids = allocCache(g, r1);
    ASSERT_EQ(ret, genBiomes(g, ids, r1), 0);
    for (int j = 0; j < r1.sz; j++) {
        for (int i = 0; i < r1.sx; i++) {
            int x4, y4, z4;
            voronoiAccess3D(g->sha, r1.x + i, r1.y, r1.z + j, &x4, &y4, &z4);
            ASSERT_EQ(ret, ids[j * r1.sx + i], getTectonicBiomeAt(tn, 4, x4, y4, z4));
            ASSERT_EQ(ret, ids[j * r1.sx + i], getBiomeAt(g, 1, r1.x + i, r1.y, r1.z + j));
        }
    }
    free(ids);

    /* biome and height from one evaluation equal the separate calls, with and without a search hint */
    uint64_t hint = 0;
    for (int i = 0; i < 4000; i++) {
        int x4 = (x >> 2) + (i < 2000 ? i : 97 * i), z4 = (z >> 2) - (i < 2000 ? i / 50 : 61 * i), y4 = (i * 7) % 80 - 16;
        float h = 0;
        int64_t npa[6], npb[6];
        int id = sampleTectonicBiomeHeight(tn, npa, &h, x4, y4, z4, i % 3 ? &hint : NULL);
        ASSERT_EQ(ret, id, sampleTectonicBiome(tn, npb, x4, y4, z4, NULL));
        ASSERT_EQ(ret, float_bits(h), float_bits(getTectonicHeight(tn, x4 * 4, z4 * 4)));
        for (int k = 0; k < 6; k++)
            ASSERT_EQ(ret, npa[k], npb[k]);
    }

    /* a single climate parameter */
    int64_t np[6];
    sampleTectonicClimate(tn, np, NULL, x, 148, z);
    Range rc = {4, x >> 2, z >> 2, 1, 1, 148 >> 2, 1};
    for (int k = 0; k < 6; k++) {
        int val = 0;
        g->bn.nptype = k;
        ASSERT_EQ(ret, genBiomes(g, &val, rc), 0);
        if (k == NP_DEPTH)
            ASSERT_EQ(ret, val, (int) (sampleTectonicOffset(tn, x, z) * 10000.0F));
        else
            ASSERT_EQ(ret, (int64_t) val, np[k]);
    }
    g->bn.nptype = -1;

    /* the datapack variant */
    setupGenerator(g, MC_26_3, TECTONIC_DATAPACK | LARGE_BIOMES);
    applySeed(g, DIM_OVERWORLD, (uint64_t) seed);
    ASSERT_EQ(ret, mapApproxHeight(&y, NULL, g, NULL, x >> 2, z >> 2, 1, 1), 0);
    ASSERT_EQ(ret, float_bits(y), float_bits(128.0F * (1.0F + 0.032791436F)));

    /* without the flag, and for versions without Tectonic data, nothing changes */
    setupGenerator(v, MC_26_3, 0);
    applySeed(v, DIM_OVERWORLD, (uint64_t) seed);
    setupGenerator(g, MC_26_2, TECTONIC_MOD);
    ASSERT_TRUE(ret, (g->flags & TECTONIC_ANY) == 0);
    setupGenerator(g, MC_1_21, TECTONIC_MOD | LARGE_BIOMES);
    ASSERT_EQ(ret, g->flags, (uint32_t) LARGE_BIOMES);
    setupGenerator(g, MC_26_3, 0);
    applySeed(g, DIM_OVERWORLD, (uint64_t) seed);
    int differs = 0;
    for (int i = 0; i < 64; i++) {
        int bx = (x >> 2) + 37 * i, bz = (z >> 2) - 53 * i;
        int a = getBiomeAt(g, 4, bx, 16, bz), b = getBiomeAt(v, 4, bx, 16, bz);
        ASSERT_EQ(ret, a, b);
        differs += a != getTectonicBiomeAt(tn, 4, bx, 16, bz);
    }
    ASSERT_TRUE(ret, differs > 0); /* vanilla 26.3 is not Tectonic */

    /* other dimensions are untouched */
    setupGenerator(g, MC_26_3, TECTONIC_MOD);
    setupGenerator(v, MC_26_3, 0);
    applySeed(g, DIM_NETHER, 12345);
    applySeed(v, DIM_NETHER, 12345);
    for (int i = 0; i < 16; i++)
        ASSERT_EQ(ret, getBiomeAt(g, 4, 31 * i, 16, -17 * i), getBiomeAt(v, 4, 31 * i, 16, -17 * i));

    free(tn);
    free(g);
    free(v);

    return ret;
}

int main(void) {
    int ret = 0;
    ret += test_tectonic_reference();
    ret += test_tectonic_height_and_biomes();
    ret += test_tectonic_generator();
    return ret;
}
