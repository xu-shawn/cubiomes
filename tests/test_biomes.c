#include "testing.h"
#include "../generator.h"

static int test_biomes_1_16_5() {
    int ret = 0;

    Generator g;
    setupGenerator(&g, MC_1_16_5, 0);

    applySeed(&g, DIM_OVERWORLD, 1437905338718953247ULL);
    ASSERT_EQ(ret, getBiomeAt(&g, 4, 3611 - 8, 0, -141), wooded_badlands_plateau);
    ASSERT_EQ(ret, getBiomeAt(&g, 4, -54 >> 2, 0, -23 >> 2), cold_ocean);
    ASSERT_EQ(ret, getBiomeAt(&g, 4, 68 >> 2, 0, 47 >> 2), mushroom_fields);
    ASSERT_EQ(ret, getBiomeAt(&g, 4, 186 >> 2, 0, 249 >> 2), frozen_ocean);
    ASSERT_EQ(ret, getBiomeAt(&g, 4, 3256313 >> 2, 0, -3265404 >> 2), forest);

    applySeed(&g, DIM_NETHER, 1551515151585454ULL);
    ASSERT_EQ(ret, getBiomeAt(&g, 4, 181 >> 2, 0, 209 >> 2), crimson_forest);
    ASSERT_EQ(ret, getBiomeAt(&g, 4, 404 >> 2, 0, 416 >> 2), soul_sand_valley);
    ASSERT_EQ(ret, getBiomeAt(&g, 4, 308 >> 2, 0, 32 >> 2), basalt_deltas);

    applySeed(&g, DIM_END, 1551515151585454ULL);
    ASSERT_EQ(ret, getBiomeAt(&g, 1, 10000, 0, 10000), end_barrens);

    return ret;
}

static int test_biomes_1_18_2() {
    int ret = 0;

    Generator g;
    setupGenerator(&g, MC_1_18_2, 0);
    applySeed(&g, DIM_NETHER, 12345);
    int biome = getBiomeAt(&g, 1, 0, 0, 0);
    ASSERT_EQ(ret, biome, nether_wastes);

    return ret;
}

static int test_biomes_26_3() {
    int ret = 0;

    Generator g;
    setupGenerator(&g, MC_26_3, 0);
    applySeed(&g, DIM_OVERWORLD, -3829811542736183482ULL);
    int biome = getBiomeAt(&g, 1, 68148, 77, 80990);
    ASSERT_EQ(ret, biome, dappled_forest);

    return ret;
}

/* a search hint must never change the result of the biome lookup */
static int test_climate_to_biome_hint() {
    int ret = 0;

    const int versions[] = {MC_1_18_2, MC_1_19_2, MC_1_19_4, MC_1_20_6, MC_1_21_4, MC_1_21_5, MC_26_2, MC_26_3};
    for (int v = 0; v < (int) (sizeof(versions) / sizeof(versions[0])); v++) {
        int mc = versions[v];
        Generator g;
        setupGenerator(&g, mc, 0);
        applySeed(&g, DIM_OVERWORLD, 12345);
        uint64_t hint = 0, rng = 0x9E3779B97F4A7C15ULL + (uint64_t) mc;
        int differs = 0, n = 0;
        /* climates of a real world along rows (good hints), then random ones (bad hints),
         * snapped to multiples of 500 so that many lie exactly on parameter borders (ties) */
        for (int i = 0; i < 60000; i++) {
            int64_t np[6];
            if (i < 30000) {
                sampleBiomeNoise(&g.bn, np, (i % 300) * 3 - 450, 16, (i / 300) * 7 - 350, NULL, SAMPLE_NO_BIOME);
            } else {
                for (int k = 0; k < 6; k++) {
                    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
                    np[k] = (int64_t) (rng % 26001) - 13000;
                    if (i % 2) np[k] = np[k] / 500 * 500;
                }
            }
            int a = climateToBiome(mc, (const uint64_t *) np, NULL);
            int b = climateToBiomeHint(mc, (const uint64_t *) np, &hint);
            int c = climateToBiomeHint(mc, (const uint64_t *) np, NULL);
            differs += (a != b) + (a != c);
            n++;
        }
        ASSERT_EQ(ret, differs, 0);
        ASSERT_EQ(ret, n, 60000);
    }

    return ret;
}

int main() {
    int ret = 0;

    ret += test_biomes_1_16_5();
    ret += test_biomes_1_18_2();
    ret += test_biomes_26_3();
    ret += test_climate_to_biome_hint();

    return ret;
}
