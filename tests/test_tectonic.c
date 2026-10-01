#include "testing.h"
#include "../tectonic.h"
#include "../biomenoise.h"

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

int main(void) {
    int ret = 0;
    ret += test_tectonic_reference();
    ret += test_tectonic_height_and_biomes();
    return ret;
}
