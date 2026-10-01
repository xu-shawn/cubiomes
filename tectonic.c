/*
 * tectonic.c - Tectonic (3.0.31 mod / 3.0.29 datapack) terrain on Minecraft 26.3.
 *
 * The noise stack below is a port of the 26.3 classes XoroshiroRandomSource, RandomSupport,
 * GradientNoise, PerlinNoise, NoiseStack, NormalNoise and CubicSpline. 26.3 evaluates noise in
 * float; every operation here is done in the type and order the game uses, which is why this file
 * does not use noise.c (double precision, older semantics). The density functions themselves are
 * generated: see tectonic_gen.h and tectonic/gen_tectonic.py.
 */

// The JVM never fuses a*b+c into an fma; neither may the C compiler. CMakeLists.txt compiles this file with
// -ffp-contract=off; the pragmas cover other build systems, except clang with an explicit -ffp-contract=fast,
// which ignores them (tests/test_tectonic.c fails in that case).
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

#include "tectonic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

//==============================================================================
// Seeding: MD5 (RandomSupport.seedFromHashOf), Xoroshiro128++
//==============================================================================

static void tectMd5(const uint8_t *msg, size_t len, uint8_t out[16])
{
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const uint8_t R[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    uint32_t h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476;
    uint8_t buf[128];
    size_t total, off;
    uint64_t bits = (uint64_t)len * 8;
    int i, j;

    if (len > 119)
        len = 119; // names are short; two blocks are plenty
    memset(buf, 0, sizeof buf);
    memcpy(buf, msg, len);
    buf[len] = 0x80;
    total = (len + 9 <= 64) ? 64 : 128;
    for (i = 0; i < 8; i++)
        buf[total - 8 + i] = (uint8_t)(bits >> (8 * i));
    for (off = 0; off < total; off += 64)
    {
        uint32_t w[16];
        uint32_t a = h0, b = h1, c = h2, d = h3;
        for (i = 0; i < 16; i++)
            w[i] = (uint32_t)buf[off + 4*i] | (uint32_t)buf[off + 4*i + 1] << 8
                 | (uint32_t)buf[off + 4*i + 2] << 16 | (uint32_t)buf[off + 4*i + 3] << 24;
        for (i = 0; i < 64; i++)
        {
            uint32_t f, t, x;
            int g;
            if (i < 16)      { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5*i + 1) & 15; }
            else if (i < 48) { f = b ^ c ^ d;          g = (3*i + 5) & 15; }
            else             { f = c ^ (b | ~d);       g = (7*i) & 15; }
            t = d; d = c; c = b;
            x = a + f + K[i] + w[g];
            b = b + ((x << R[i]) | (x >> (32 - R[i])));
            a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d;
    }
    {
        uint32_t hs[4];
        hs[0] = h0; hs[1] = h1; hs[2] = h2; hs[3] = h3;
        for (i = 0; i < 4; i++)
            for (j = 0; j < 4; j++)
                out[4*i + j] = (uint8_t)(hs[i] >> (8 * j));
    }
}

typedef struct { uint64_t lo, hi; } TectXr;

static inline uint64_t tectRotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static inline TectXr tectXrMake(uint64_t lo, uint64_t hi)
{
    TectXr r;
    if ((lo | hi) == 0) { lo = 0x9E3779B97F4A7C15ULL; hi = 0x6A09E667F3BCC909ULL; }
    r.lo = lo; r.hi = hi;
    return r;
}

static inline uint64_t tectXrNext(TectXr *s)
{
    uint64_t s0 = s->lo, s1 = s->hi;
    uint64_t r = tectRotl(s0 + s1, 17) + s0;
    s1 ^= s0;
    s->lo = tectRotl(s0, 49) ^ s1 ^ (s1 << 21);
    s->hi = tectRotl(s1, 28);
    return r;
}

static inline uint64_t tectStafford13(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// new XoroshiroRandomSource(long seed)
static inline TectXr tectXrFromSeed(uint64_t seed)
{
    uint64_t lo = seed ^ 0x6A09E667F3BCC909ULL;
    uint64_t hi = lo + 0x9E3779B97F4A7C15ULL;
    return tectXrMake(tectStafford13(lo), tectStafford13(hi));
}

static inline double tectXrNextDouble(TectXr *s)
{
    return (double)(tectXrNext(s) >> 11) * 0x1.0p-53;
}

static inline int tectXrNextInt(TectXr *s, uint32_t bound)
{
    uint64_t r = (uint32_t)tectXrNext(s);
    uint64_t m = r * bound;
    uint64_t frac = m & 0xffffffffULL;
    if (frac < bound)
    {
        uint32_t t = (uint32_t)(-bound) % bound;
        while (frac < t)
        {
            r = (uint32_t)tectXrNext(s);
            m = r * bound;
            frac = m & 0xffffffffULL;
        }
    }
    return (int)(m >> 32);
}

// forkPositional(): the positional factory is two longs
static inline TectXr tectXrForkPositional(TectXr *s)
{
    TectXr p;
    p.lo = tectXrNext(s);
    p.hi = tectXrNext(s);
    return p;
}

// PositionalRandomFactory.fromHashOf(name)
static TectXr tectXrFromHashOf(TectXr factory, const char *name)
{
    uint8_t d[16];
    uint64_t lo = 0, hi = 0;
    int i;
    tectMd5((const uint8_t *)name, strlen(name), d);
    for (i = 0; i < 8; i++) { lo = (lo << 8) | d[i]; hi = (hi << 8) | d[8 + i]; }
    return tectXrMake(lo ^ factory.lo, hi ^ factory.hi);
}

//==============================================================================
// PerlinNoise / NoiseStack / NormalNoise (26.3: float arithmetic)
//==============================================================================

static void tectPerlinInit(TectonicPerlin *n, TectXr rng)
{
    int i;
    n->a = tectXrNextDouble(&rng) * 256.0;
    n->b = tectXrNextDouble(&rng) * 256.0;
    n->c = tectXrNextDouble(&rng) * 256.0;
    for (i = 0; i < 256; i++)
        n->d[i] = (uint8_t)i;
    for (i = 0; i < 256; i++)
    {
        int j = tectXrNextInt(&rng, (uint32_t)(256 - i));
        uint8_t t = n->d[i]; n->d[i] = n->d[i + j]; n->d[i + j] = t;
    }
}

static const float TECT_GX[16] = {1,-1,1,-1,1,-1,1,-1,0,0,0,0,1,0,-1,0};
static const float TECT_GY[16] = {1,1,-1,-1,0,0,0,0,1,-1,1,-1,1,-1,1,-1};
static const float TECT_GZ[16] = {0,0,0,0,1,1,-1,-1,1,1,-1,-1,0,1,0,-1};

static inline float tectGrad(int h, float x, float y, float z)
{
    h &= 15;
    return TECT_GX[h] * x + TECT_GY[h] * y + TECT_GZ[h] * z;
}

static inline float tectSmooth(float x) { return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f); }
static inline float tectLerp(float a, float p0, float p1) { return p0 + a * (p1 - p0); }

// GradientNoise.wrap
static inline double tectWrap(double x)
{
    const double half = 16777215.999999998; // Math.nextDown(1.6777216E7)
    return (x >= -half && x < half) ? x : x - floor(x / 3.3554432E7 + 0.5) * 3.3554432E7;
}

// PerlinNoise.get(x, y, z)
static inline float tectPerlinGet(const TectonicPerlin *n, double x, double y, double z)
{
    double fx, fy, fz;
    int ix, iy, iz, x0, x1, xy00, xy01, xy10, xy11;
    float rx, ry, rz, d000, d100, d010, d110, d001, d101, d011, d111, ax, ay, az, lo, hi;
    const uint8_t *p = n->d;

    x = tectWrap(x) + n->a; y = tectWrap(y) + n->b; z = tectWrap(z) + n->c;
    fx = floor(x); fy = floor(y); fz = floor(z);
    ix = (int)fx; iy = (int)fy; iz = (int)fz;
    rx = (float)(x - fx); ry = (float)(y - fy); rz = (float)(z - fz);
    x0 = p[ix & 255]; x1 = p[(ix + 1) & 255];
    xy00 = p[(x0 + iy) & 255]; xy01 = p[(x0 + iy + 1) & 255];
    xy10 = p[(x1 + iy) & 255]; xy11 = p[(x1 + iy + 1) & 255];
    d000 = tectGrad(p[(xy00 + iz) & 255], rx, ry, rz);
    d100 = tectGrad(p[(xy10 + iz) & 255], rx - 1.0f, ry, rz);
    d010 = tectGrad(p[(xy01 + iz) & 255], rx, ry - 1.0f, rz);
    d110 = tectGrad(p[(xy11 + iz) & 255], rx - 1.0f, ry - 1.0f, rz);
    d001 = tectGrad(p[(xy00 + iz + 1) & 255], rx, ry, rz - 1.0f);
    d101 = tectGrad(p[(xy10 + iz + 1) & 255], rx - 1.0f, ry, rz - 1.0f);
    d011 = tectGrad(p[(xy01 + iz + 1) & 255], rx, ry - 1.0f, rz - 1.0f);
    d111 = tectGrad(p[(xy11 + iz + 1) & 255], rx - 1.0f, ry - 1.0f, rz - 1.0f);
    ax = tectSmooth(rx); ay = tectSmooth(ry); az = tectSmooth(rz);
    // Mth.lerp3
    lo = tectLerp(ay, tectLerp(ax, d000, d100), tectLerp(ax, d010, d110));
    hi = tectLerp(ay, tectLerp(ax, d001, d101), tectLerp(ax, d011, d111));
    return tectLerp(az, lo, hi);
}

typedef struct
{
    const char *name;           // registry id, which is also what the noise is seeded with
    int base_octave, octave_count;
    double base_amplitude;
    int legacy;                 // "normalize": "legacy"
    double mods[16];            // amplitude_modifiers
} TectNoiseParams;

// NormalNoise(parameters) + NormalNoise.create(random): appends the layers of noise k to the pool
static int tectNormalNoiseInit(TectonicNoise *tn, int k, const TectNoiseParams *np, TectXr factory)
{
    int n = np->octave_count, nocts = 0, i;
    double amp0 = np->base_amplitude * (pow(0.5, -(double)(n - 1)) / (pow(0.5, -(double)n) - 1.0));
    double oct_amp[32], oct_freq[32];
    int oct_idx[32];
    double frequency = pow(2.0, (double)np->base_octave), amplitude = amp0;
    int min_o = 1 << 30, max_o = -(1 << 30);
    double target = 0, var = 0, input_dev, norm;
    TectXr rng, first, second;
    int l = tn->first[k];

    for (i = 0; i < n; i++)
    {
        double mod = np->mods[i];
        if (mod != 0.0)
        {
            oct_amp[nocts] = amplitude * mod; oct_freq[nocts] = frequency; oct_idx[nocts] = np->base_octave + i;
            nocts++;
            if (i < min_o) min_o = i;
            if (i > max_o) max_o = i;
        }
        frequency *= 2.0; amplitude *= 0.5;
    }
    for (i = 0; i < nocts; i++)
    {
        double dev = 0.2702247831245211 * fabs(oct_amp[i]);
        target += fabs(oct_amp[i]);
        var += dev * dev;
    }
    input_dev = sqrt(var);
    norm = input_dev == 0.0 ? 0.0 : (target * 0.3333333333333333) / (input_dev * sqrt(2.0));
    if (np->legacy && norm != 0.0)
        norm = np->base_amplitude * 0.5 * 0.3333333333333333 / (0.1 * (1.0 + 1.0 / (double)(max_o - min_o + 1)));

    rng = tectXrFromHashOf(factory, np->name);
    first = tectXrForkPositional(&rng);
    second = tectXrForkPositional(&rng);
    if (l + 2 * nocts > TECTONIC_MAX_LAYERS)
        return 1;
    for (i = 0; i < nocts; i++)
    {
        char key[32];
        float vf = (float)(norm * oct_amp[i]);
        snprintf(key, sizeof key, "octave_%d", oct_idx[i]);
        tectPerlinInit(&tn->perlin[l], tectXrFromHashOf(first, key));
        tn->freq[l] = oct_freq[i]; tn->amp[l] = vf;
        tectPerlinInit(&tn->perlin[l + 1], tectXrFromHashOf(second, key));
        tn->freq[l + 1] = oct_freq[i] * 1.0181268882175227; tn->amp[l + 1] = vf;
        l += 2;
    }
    tn->first[k + 1] = l;
    return 0;
}

// NoiseStack.get(x, y, z) of noise k
static inline float tectNoiseGet(const TectonicNoise *tn, int k, double x, double y, double z)
{
    float v = 0.0f;
    int i, end = tn->first[k + 1];
    for (i = tn->first[k]; i < end; i++)
    {
        double f = tn->freq[i];
        v += tn->amp[i] * tectPerlinGet(&tn->perlin[i], x * f, y * f, z * f);
    }
    return v;
}

//==============================================================================
// Density function evaluation
//==============================================================================

enum { TECT_MAX_SLOTS = 96 };

// One sampled position. Functions wrapped in `cache`, named functions and spline coordinates are
// evaluated at most once per position (have/val), like the game's caching samplers.
typedef struct
{
    const TectonicNoise *tn;
    int x, y, z;
    uint8_t have[TECT_MAX_SLOTS];
    float val[TECT_MAX_SLOTS];
} TectEval;

typedef struct { float loc, der; int is_const; float value; int child; } TectSplinePoint;
typedef struct { int coord, first, npts; } TectSplineNode;

typedef struct
{
    const TectNoiseParams *noises;
    int nnoises;
    float (*const *fields)(TectEval *);
    float (*const *roots)(TectEval *);   // offset, then the Climate.Sampler order
    int sea_level;
} TectVariant;

// Math.min / Math.max for floats
static inline float tectJavaMin(float a, float b)
{
    if (a != a) return a;
    if (a == 0.0f && b == 0.0f) return signbit(a) ? a : b;
    return a <= b ? a : b;
}

static inline float tectJavaMax(float a, float b)
{
    if (a != a) return a;
    if (a == 0.0f && b == 0.0f) return signbit(a) ? b : a;
    return a >= b ? a : b;
}

// CubicSpline.Multipoint.sample; coordinates are density functions, evaluated on demand
static float tectSplineEval(const TectSplineNode *nodes, const TectSplinePoint *pts,
    float (*const *coords)(TectEval *), int node, TectEval *e)
{
    const TectSplineNode *nd = &nodes[node];
    const TectSplinePoint *p = &pts[nd->first];
    float input = coords[nd->coord](e);
    int n = nd->npts;
    // Mth.binarySearch(0, n, i -> input < loc[i]) - 1
    int lo = 0, len = n, start;
    float x1, x2, t, d1, d2, y1, y2, a, b;
    while (len > 0)
    {
        int half = len / 2, mid = lo + half;
        if (input < p[mid].loc) len = half; else { lo = mid + 1; len -= half + 1; }
    }
    start = lo - 1;
#define TECT_SPV(i) (p[i].is_const ? p[i].value : tectSplineEval(nodes, pts, coords, p[i].child, e))
    if (start < 0)
    {
        float v = TECT_SPV(0);
        return p[0].der == 0.0f ? v : v + p[0].der * (input - p[0].loc);
    }
    if (start == n - 1)
    {
        float v = TECT_SPV(n - 1);
        return p[n - 1].der == 0.0f ? v : v + p[n - 1].der * (input - p[n - 1].loc);
    }
    x1 = p[start].loc; x2 = p[start + 1].loc;
    t = (input - x1) / (x2 - x1);
    d1 = p[start].der; d2 = p[start + 1].der;
    y1 = TECT_SPV(start); y2 = TECT_SPV(start + 1);
#undef TECT_SPV
    a = d1 * (x2 - x1) - (y2 - y1);
    b = -d2 * (x2 - x1) + (y2 - y1);
    return tectLerp(t, y1, y2) + t * (1.0f - t) * tectLerp(t, a, b);
}

#include "tectonic_gen.h"

#if TECT_GEN_SLOTS > 96 || TECT_GEN_NOISES > 20 || TECT_GEN_LAYERS > 160
#error "tectonic_gen.h needs more memo slots / noises / layers than tectonic.h provides"
#endif

static inline void tectEvalAt(TectEval *e, const TectonicNoise *tn, int x, int y, int z)
{
    e->tn = tn; e->x = x; e->y = y; e->z = z;
    memset(e->have, 0, TECT_GEN_SLOTS);
}

//==============================================================================
// Public API
//==============================================================================

int initTectonic(TectonicNoise *tn, int variant, uint64_t seed)
{
    const TectVariant *v;
    TectXr rng, factory;
    int i;
    if (variant < 0 || variant >= TECT_NVARIANTS)
        return 1;
    v = &tect_variants[variant];
    tn->variant = variant;
    tn->seed = seed;
    // RandomState: new XoroshiroRandomSource(seed).forkPositional()
    rng = tectXrFromSeed(seed);
    factory = tectXrForkPositional(&rng);
    tn->first[0] = 0;
    for (i = 0; i < v->nnoises; i++)
        if (tectNormalNoiseInit(tn, i, &v->noises[i], factory))
            return 1;
    return 0;
}

float sampleTectonicOffset(const TectonicNoise *tn, int x, int z)
{
    TectEval e;
    tectEvalAt(&e, tn, x, 0, z);
    return tect_variants[tn->variant].roots[0](&e);
}

float getTectonicHeight(const TectonicNoise *tn, int x, int z)
{
    return 128.0f * (1.0f + sampleTectonicOffset(tn, x, z));
}

int mapTectonicHeight(float *y, const TectonicNoise *tn, int x, int z, int w, int h, int step)
{
    int i, j;
    if (step < 1 || w < 0 || h < 0)
        return 1;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            y[(size_t)j * w + i] = getTectonicHeight(tn, x + i * step, z + j * step);
    return 0;
}

void sampleTectonicClimate(const TectonicNoise *tn, int64_t np[6], float nv[6], int x, int y, int z)
{
    const TectVariant *v = &tect_variants[tn->variant];
    TectEval e;
    int i;
    tectEvalAt(&e, tn, x, y, z);
    for (i = 0; i < 6; i++)
    {
        float f = v->roots[1 + i](&e);      // roots follow the NP_* order
        if (nv) nv[i] = f;
        if (np) np[i] = (int64_t)(f * 10000.0f);   // Climate.quantizeCoord
    }
}

int sampleTectonicBiome(const TectonicNoise *tn, int64_t *np, int x, int y, int z, uint64_t *dat)
{
    int64_t l_np[6];
    int64_t *p_np = np ? np : l_np;
    // the game samples the climate at the block position of the biome cell's corner
    sampleTectonicClimate(tn, p_np, NULL, x * 4, y * 4, z * 4);
    return climateToBiome(MC_26_3, (const uint64_t *)p_np, dat);
}

int sampleTectonicBiomeHeight(const TectonicNoise *tn, int64_t *np, float *height,
    int x, int y, int z, uint64_t *hint)
{
    const TectVariant *v = &tect_variants[tn->variant];
    int64_t l_np[6];
    int64_t *p_np = np ? np : l_np;
    TectEval e;
    int i;
    tectEvalAt(&e, tn, x * 4, y * 4, z * 4);
    for (i = 0; i < 6; i++)
        p_np[i] = (int64_t)(v->roots[1 + i](&e) * 10000.0f);
    // the offset was evaluated for the depth parameter and is memoised in e
    if (height)
        *height = 128.0f * (1.0f + v->roots[0](&e));
    return climateToBiomeHint(MC_26_3, (const uint64_t *)p_np, hint);
}

int getTectonicBiomeAt(const TectonicNoise *tn, int scale, int x, int y, int z)
{
    if (scale == 1) { x >>= 2; y >>= 2; z >>= 2; }
    else if (scale != 4) return -1;
    return sampleTectonicBiome(tn, NULL, x, y, z, NULL);
}

static void tectGen3D(const TectonicNoise *tn, int *out, Range r, int nptype, int carry)
{
    uint64_t dat = 0;
    int i, j, k;
    int *p = out;
    int scale = r.scale > 4 ? r.scale / 4 : 1;
    int mid = scale / 2;
    for (k = 0; k < r.sy; k++)
    {
        int yk = r.y + k;
        for (j = 0; j < r.sz; j++)
        {
            int zj = (r.z + j) * scale + mid;
            for (i = 0; i < r.sx; i++)
            {
                int xi = (r.x + i) * scale + mid;
                if (nptype < 0)
                    *p = sampleTectonicBiome(tn, NULL, xi, yk, zj, carry ? &dat : NULL);
                else if (nptype == NP_DEPTH)
                    *p = (int)(int64_t)(sampleTectonicOffset(tn, xi * 4, zj * 4) * 10000.0f);
                else
                {
                    int64_t np[6];
                    sampleTectonicClimate(tn, np, NULL, xi * 4, yk * 4, zj * 4);
                    *p = (int)np[nptype];
                }
                p++;
            }
        }
    }
}

int genTectonicBiomes(const TectonicNoise *tn, int *out, Range r, uint64_t sha)
{
    uint64_t siz;
    int i, j, k;

    if (r.sy == 0)
        r.sy = 1;
    siz = (uint64_t)r.sx * r.sy * r.sz;

    if (r.scale == 1)
    {   // same procedure as genBiomeNoiseScaled(): biomes at 1:4, then the voronoi zoom
        Range s = getVoronoiSrcRange(r);
        int *src = NULL;
        int *p = out;
        if (siz > 1)
        {
            src = out + siz;
            tectGen3D(tn, src, s, -1, 0);
        }
        for (k = 0; k < r.sy; k++)
        {
            for (j = 0; j < r.sz; j++)
            {
                for (i = 0; i < r.sx; i++)
                {
                    int x4, z4, y4;
                    voronoiAccess3D(sha, r.x+i, r.y+k, r.z+j, &x4, &y4, &z4);
                    if (src)
                    {
                        x4 -= s.x; y4 -= s.y; z4 -= s.z;
                        *p = src[(int64_t)y4*s.sx*s.sz + (int64_t)z4*s.sx + x4];
                    }
                    else
                    {
                        *p = sampleTectonicBiome(tn, NULL, x4, y4, z4, NULL);
                    }
                    p++;
                }
            }
        }
    }
    else
    {
        tectGen3D(tn, out, r, -1, 0);
    }
    return 0;
}

int genTectonicClimate(const TectonicNoise *tn, int *out, Range r, int nptype)
{
    if (nptype < 0 || nptype >= NP_MAX || r.scale < 4)
        return 1;
    if (r.sy == 0)
        r.sy = 1;
    tectGen3D(tn, out, r, nptype, 0);
    return 0;
}

int mapApproxHeightTectonic(float *y, int *ids, const TectonicNoise *tn, int x, int z, int w, int h)
{
    int i, j;
    if (w < 0 || h < 0)
        return 1;
    for (j = 0; j < h; j++)
    {
        for (i = 0; i < w; i++)
        {
            float f = getTectonicHeight(tn, (x + i) * 4, (z + j) * 4);
            if (y) y[(size_t)j * w + i] = f;
            if (ids)
            {
                int by = (int)floorf(f);
                if (by < TECTONIC_SEA_LEVEL) by = TECTONIC_SEA_LEVEL;
                ids[(size_t)j * w + i] = getTectonicBiomeAt(tn, 4, x + i, by >> 2, z + j);
            }
        }
    }
    return 0;
}

int getTectonicFieldCount(void)
{
    return TECT_NFIELDS;
}

const char *getTectonicFieldName(int field)
{
    return (field >= 0 && field < TECT_NFIELDS) ? tect_field_names[field] : NULL;
}

int getTectonicFieldId(const char *name)
{
    int i;
    for (i = 0; i < TECT_NFIELDS; i++)
        if (strcmp(tect_field_names[i], name) == 0)
            return i;
    return -1;
}

float sampleTectonicField(const TectonicNoise *tn, int field, int x, int y, int z)
{
    TectEval e;
    float (*f)(TectEval *);
    if (field < 0 || field >= TECT_NFIELDS)
        return NAN;
    f = tect_variants[tn->variant].fields[field];
    if (!f)
        return NAN;
    tectEvalAt(&e, tn, x, y, z);
    return f(&e);
}
