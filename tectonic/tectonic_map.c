/*
 * tectonic_map.c - renders a Tectonic height map (or biome map) to a PPM image.
 *
 *   tectonic_map <seed> [options]
 *     -v mod|dp        variant: Tectonic 3.0.31 mod (default) or 3.0.29 datapack
 *     -c <x> <z>       centre block (default 0 0)
 *     -s <blocks>      blocks per pixel (default 16)
 *     -n <w> <h>       image size in pixels (default 1024 1024)
 *     -m <y>           outline columns with base height >= y in red (e.g. 150 for mountains)
 *     -b               biomes at the surface instead of heights (still shaded with the relief)
 *     -o <file.ppm>    output (default tectonic.ppm)
 *
 * Heights are the base surface of tectonic.h (getTectonicHeight); sea level is 63.
 */
#include "../tectonic.h"
#include "../util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* hypsometric tint: height in blocks -> rgb */
static void tint(float h, float rgb[3])
{
    static const float stops[][4] = {
        {-64, 8, 20, 60}, {20, 20, 50, 120}, {62, 60, 110, 190}, {63, 190, 180, 130}, {75, 90, 150, 70},
        {110, 60, 120, 55}, {140, 130, 125, 80}, {170, 120, 100, 85}, {200, 150, 145, 140}, {230, 225, 225, 230},
        {320, 255, 255, 255}};
    int n = sizeof stops / sizeof stops[0], k = 0, c;
    float f;
    while (k < n - 2 && h >= stops[k + 1][0]) k++;
    f = (h - stops[k][0]) / (stops[k + 1][0] - stops[k][0]);
    f = f < 0 ? 0 : (f > 1 ? 1 : f);
    for (c = 0; c < 3; c++) rgb[c] = stops[k][c + 1] + f * (stops[k + 1][c + 1] - stops[k][c + 1]);
}

int main(int argc, char **argv)
{
    int variant = TECTONIC_MOD_3_0_31, cx = 0, cz = 0, step = 16, w = 1024, h = 1024, biomes = 0, i, j, c;
    float mask = -1;
    const char *out = "tectonic.ppm";
    int64_t seed;
    TectonicNoise *tn;
    float *y;
    unsigned char *px, colors[256][3];
    int x0, z0;
    float lo = 1e9f, hi = -1e9f;
    long land = 0, mountain = 0;

    if (argc < 2) { fprintf(stderr, "usage: tectonic_map <seed> [-v mod|dp] [-c x z] [-s blocks/px] [-n w h] [-m y] [-b] [-o out.ppm]\n"); return 2; }
    seed = strtoll(argv[1], NULL, 10);
    for (i = 2; i < argc; i++)
    {
        if (!strcmp(argv[i], "-v") && i + 1 < argc) variant = strcmp(argv[++i], "dp") == 0 ? TECTONIC_DP_3_0_29 : TECTONIC_MOD_3_0_31;
        else if (!strcmp(argv[i], "-c") && i + 2 < argc) { cx = atoi(argv[++i]); cz = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) step = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 2 < argc) { w = atoi(argv[++i]); h = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) mask = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "-b")) biomes = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (step < 1 || w < 3 || h < 3) { fprintf(stderr, "bad size\n"); return 2; }

    tn = (TectonicNoise *)malloc(sizeof *tn);
    y = (float *)malloc(sizeof(float) * (size_t)w * h);
    px = (unsigned char *)malloc((size_t)3 * w * h);
    if (!tn || !y || !px || initTectonic(tn, variant, (uint64_t)seed)) { fprintf(stderr, "init failed\n"); return 1; }
    initBiomeColors(colors);

    x0 = cx - (w / 2) * step; z0 = cz - (h / 2) * step;
    mapTectonicHeight(y, tn, x0, z0, w, h, step);

    for (j = 0; j < h; j++)
    {
        for (i = 0; i < w; i++)
        {
            float v = y[(size_t)j * w + i], rgb[3], shade = 1.0f;
            int il = i > 0 ? i - 1 : i, ir = i < w - 1 ? i + 1 : i, ju = j > 0 ? j - 1 : j, jd = j < h - 1 ? j + 1 : j;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            if (v >= TECTONIC_SEA_LEVEL) land++;
            if (v >= 150) mountain++;
            if (biomes)
            {
                int by = (int)floorf(v) < TECTONIC_SEA_LEVEL ? TECTONIC_SEA_LEVEL : (int)floorf(v);
                int id = getTectonicBiomeAt(tn, 1, x0 + i * step, by, z0 + j * step);
                for (c = 0; c < 3; c++) rgb[c] = id >= 0 && id < 256 ? colors[id][c] : 0;
            }
            else tint(v, rgb);
            if (v >= TECTONIC_SEA_LEVEL)
            {   /* hillshade, light from the north-west; relief is exaggerated beyond 32 blocks per pixel */
                float cell = (float)(step < 32 ? step : 32);
                float gx = (y[(size_t)j * w + ir] - y[(size_t)j * w + il]) / ((ir - il) * cell);
                float gz = (y[(size_t)jd * w + i] - y[(size_t)ju * w + i]) / ((jd - ju) * cell);
                shade = 1.0f + 2.5f * (-gx - gz) / sqrtf(1.0f + 6.0f * (gx * gx + gz * gz));
                shade = shade < 0.45f ? 0.45f : (shade > 1.5f ? 1.5f : shade);
            }
            if (mask >= 0 && v >= mask && !(y[(size_t)j * w + il] >= mask && y[(size_t)j * w + ir] >= mask
                                           && y[(size_t)ju * w + i] >= mask && y[(size_t)jd * w + i] >= mask))
            { rgb[0] = 255; rgb[1] = 30; rgb[2] = 30; shade = 1.0f; }
            for (c = 0; c < 3; c++)
            {
                float f = rgb[c] * shade;
                px[3 * ((size_t)j * w + i) + c] = (unsigned char)(f < 0 ? 0 : (f > 255 ? 255 : f));
            }
        }
    }
    if (savePPM(out, px, w, h)) { fprintf(stderr, "cannot write %s\n", out); return 1; }
    printf("Tectonic %s, seed %lld: blocks x %d..%d, z %d..%d at %d blocks/px\n", variant == TECTONIC_DP_3_0_29 ? "3.0.29 datapack" : "3.0.31 mod",
           (long long)seed, x0, x0 + (w - 1) * step, z0, z0 + (h - 1) * step, step);
    printf("base height y %.1f .. %.1f, land %.1f%%, mountain (>= 150) %.2f%%\n", lo, hi, 100.0 * land / ((double)w * h), 100.0 * mountain / ((double)w * h));
    printf("wrote %s (%d x %d)\n", out, w, h);
    free(tn); free(y); free(px);
    return 0;
}
