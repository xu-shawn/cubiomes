#ifndef TECTONIC_H_
#define TECTONIC_H_

/*
 * Terrain of the Tectonic world generation mod (https://modrinth.com/mod/tectonic) on Minecraft 26.3.
 *
 * Tectonic replaces the overworld's terrain shaping density functions. This module evaluates its
 * complete base surface (tectonic:terrain_spline/offset/final: continents, erosion-ridge mountains,
 * plateau regions, islands, ocean floor) and the six climate parameters it feeds to the biome source,
 * for the two published variants:
 *
 *   TECTONIC_MOD_3_0_31   the Fabric/NeoForge mod 3.0.31 with its default config
 *   TECTONIC_DP_3_0_29    the datapack 3.0.29
 *
 * The density functions in tectonic_gen.h are generated from the pack json by
 * tectonic/gen_tectonic.py; nothing is hand copied. Minecraft 26.3 evaluates density functions and
 * noise in single precision, so this module has its own float noise stack instead of noise.c, and it
 * reproduces the game bit for bit (see tectonic/README.md for the measured agreement).
 * It must be compiled without floating point contraction (-ffp-contract=off; CMakeLists.txt and the
 * pragmas in tectonic.c take care of that).
 */

#include "biomenoise.h"

#ifdef __cplusplus
extern "C"
{
#endif

enum TectonicVariant
{
    TECTONIC_MOD_3_0_31 = 0,
    TECTONIC_DP_3_0_29  = 1,
};

enum
{
    TECTONIC_MAX_NOISES = 20,
    TECTONIC_MAX_LAYERS = 160,  // Perlin layers of all noises together (two per octave)
    TECTONIC_SEA_LEVEL  = 63,
};

STRUCT(TectonicPerlin)
{
    double a, b, c;             // offsets
    uint8_t d[256];             // permutation
};

// The NormalNoises of a variant as Minecraft 26.3 builds them: every noise is a stack of Perlin
// layers with float amplitudes; noise i owns the layers [first[i], first[i+1]).
STRUCT(TectonicNoise)
{
    int variant;
    uint64_t seed;
    int first[TECTONIC_MAX_NOISES + 1];
    double freq[TECTONIC_MAX_LAYERS];
    float amp[TECTONIC_MAX_LAYERS];
    TectonicPerlin perlin[TECTONIC_MAX_LAYERS];
};

/**
 * Initializes the Tectonic noises for a world seed. Returns zero on success and non-zero if the
 * variant is unknown. A TectonicNoise is about 47 kB; it is not modified by sampling, so one
 * instance can be shared between threads.
 */
int initTectonic(TectonicNoise *tn, int variant, uint64_t seed);

/**
 * The base surface offset, tectonic:terrain_spline/offset/final, at a block column.
 * The terrain density changes sign at depth = 1 - y/128 + offset = 0.
 */
float sampleTectonicOffset(const TectonicNoise *tn, int x, int z);

/**
 * The base surface height in blocks, 128 * (1 + offset): the height of the terrain before
 * Tectonic's peaks (jaggedness) and the 3D noise are added. Sea level is 63. On land the real
 * surface is 0-7 blocks above it below y 160 and up to ~130 blocks above it on mountain crests.
 */
float getTectonicHeight(const TectonicNoise *tn, int x, int z);

/**
 * Fills y[w*h] (indexed y[i + j*w]) with the base surface heights of the block columns
 * (x + i*step, z + j*step). Returns zero on success.
 */
int mapTectonicHeight(float *y, const TectonicNoise *tn, int x, int z, int w, int h, int step);

/**
 * The climate of a block position as the game's Climate.Sampler sees it with Tectonic:
 * np[] is indexed by NP_TEMPERATURE .. NP_WEIRDNESS (biomenoise.h) and quantized like the game
 * ((int64_t)(value * 10000.0f)), so it can be passed to climateToBiome().
 * If nv is non-null it receives the unquantized values.
 */
void sampleTectonicClimate(const TectonicNoise *tn, int64_t np[6], float nv[6], int x, int y, int z);

/**
 * The biome the vanilla 26.3 overworld biome source (multi-noise preset `overworld`) selects with
 * Tectonic's climate. scale is 4 (x, y, z are biome coordinates, exactly what the game samples)
 * or 1 (block coordinates; the containing 4x4x4 cell is used, without the fuzzy biome zoom).
 *
 * The climate is bit-exact; the biome is found with climateToBiome(MC_26_3, ...). Where two biomes
 * fit a climate equally well (the value lies exactly on the border between their parameter ranges,
 * which Tectonic's splines produce more often than vanilla noise: about 1 column in 2000) the game
 * takes whichever its search meets first, or its previous result, and cubiomes' tree may pick the
 * other one.
 */
int getTectonicBiomeAt(const TectonicNoise *tn, int scale, int x, int y, int z);

/**
 * Like sampleBiomeNoise(): biome at the biome coordinates (x, y, z) at scale 1:4. If np is non-null
 * it receives the quantized climate. dat is the lookup state of climateToBiome() (may be NULL).
 */
int sampleTectonicBiome(const TectonicNoise *tn, int64_t *np, int x, int y, int z, uint64_t *dat);

/**
 * Biome and base surface height from one evaluation: the biome of the 1:4 cell (x, y, z), exactly
 * as sampleTectonicBiome() returns it without dat, and in *height the base surface height of the
 * block column (x*4, z*4) at which that cell's climate is sampled, exactly as getTectonicHeight()
 * returns it. The climate needs the surface offset anyway, so the height comes for free.
 * hint is the search hint of climateToBiomeHint() (start with 0, pass the same variable for
 * neighbouring positions): it speeds the biome lookup up without changing its result.
 * np and hint may be NULL.
 */
int sampleTectonicBiomeHeight(const TectonicNoise *tn, int64_t *np, float *height,
    int x, int y, int z, uint64_t *hint);

/**
 * Counterpart of genBiomeNoiseScaled(): biomes for a Range at scale 1, 4, 16, 64 or 256. Scale 1
 * applies the voronoi zoom with the given sha (getVoronoiSHA(seed)) and needs the cache size
 * getMinCacheSize() reports for 1.18+; scales above 4 sample the centre of each cell.
 * This is what genBiomes() calls for a Generator set up with a TECTONIC_* flag.
 */
int genTectonicBiomes(const TectonicNoise *tn, int *out, Range r, uint64_t sha);

/**
 * Fills out with one climate parameter (nptype = NP_TEMPERATURE .. NP_WEIRDNESS), quantized like
 * sampleTectonicClimate(), for a Range at scale 4 or above. For NP_DEPTH the terrain offset is
 * written (the depth parameter without its y gradient), as cubiomes does for vanilla.
 */
int genTectonicClimate(const TectonicNoise *tn, int *out, Range r, int nptype);

/**
 * Counterpart of mapApproxHeight() for Tectonic: horizontal scaling 1:4, y[w*h] receives the base
 * surface height in blocks and, if non-null, ids[w*h] the biomes at that surface (never below sea
 * level). Returns zero on success.
 */
int mapApproxHeightTectonic(float *y, int *ids, const TectonicNoise *tn, int x, int z, int w, int h);

/**
 * Access to every named density function of the generated graph, e.g.
 * "tectonic:noise/raw_continents" or "tectonic:terrain_spline/offset/regions".
 * getTectonicFieldId() returns -1 for unknown names; sampleTectonicField() returns NaN if the
 * variant does not have the function. y only matters for minecraft:overworld/depth.
 */
int getTectonicFieldCount(void);
const char *getTectonicFieldName(int field);
int getTectonicFieldId(const char *name);
float sampleTectonicField(const TectonicNoise *tn, int field, int x, int y, int z);

#ifdef __cplusplus
}
#endif

#endif /* TECTONIC_H_ */
