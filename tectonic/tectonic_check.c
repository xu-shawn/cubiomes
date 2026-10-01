/*
 * tectonic_check.c - compares tectonic.c with values produced by the real Minecraft 26.3 generator.
 *
 *   <harness> <pack> points <seed> <id,id,...> < points.txt | tectonic_check <mod|dp> <seed> <id,id,...>
 *
 * stdin: lines "x y z v1 v2 ..." with one value per id, in the order of the id list. Ids are density
 * function ids of the generated graph (getTectonicFieldName) or router:temperature, router:vegetation,
 * router:continents, router:erosion, router:depth, router:ridges for the climate the biome source sees.
 * A line "B x y z <biome name>" is compared with getTectonicBiomeAt(scale 1). Where the biome differs the
 * checker tests whether the game's biome fits the climate exactly as well as ours (an exact tie between two
 * parameter boxes, which the game resolves by search order and by its previous result).
 * Exit status 0 only if every compared value is bit-identical, every biome difference is such a tie, and
 * something was compared for every id.
 */
#include "../tectonic.h"
#include "../biomenoise.h"
#include "../util.h"
#include "../tables/btree263.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXF 96

/* Is `name` one of the biomes whose parameter box is closest to this climate (an exact tie)? */
static int is_tied(const int64_t np[6], const char *name)
{
    uint32_t len = sizeof(btree263_nodes) / sizeof(uint64_t), i, k;
    uint64_t best = (uint64_t)-1, ds[2];
    int pass, found = 0;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < len; i++)
        {
            uint64_t node = btree263_nodes[i];
            if ((node >> 56) != 0xFF) continue;     /* leaves only */
            ds[0] = 0;
            for (k = 0; k < 6; k++)
            {
                int idx = (node >> 8*k) & 0xFF;
                int64_t a = np[k] - btree263_param[idx][1], b = btree263_param[idx][0] - np[k];
                int64_t d = a > 0 ? a : b > 0 ? b : 0;
                ds[0] += (uint64_t)(d * d);
            }
            if (pass == 0) { if (ds[0] < best) best = ds[0]; }
            else if (ds[0] == best && strcmp(biome2str(MC_26_3, (node >> 48) & 0xFF), name) == 0) found = 1;
        }
    return found;
}

int main(int argc, char **argv)
{
    static const char *router[6] = {"router:temperature", "router:vegetation", "router:continents",
                                    "router:erosion", "router:depth", "router:ridges"};
    static char line[1 << 16];
    char *names[MAXF];
    int field[MAXF], clim[MAXF], nf = 0, i, ok;
    long n = 0, cmp[MAXF] = {0}, exact[MAXF] = {0}, bn = 0, bsame = 0, bties = 0, shown = 0;
    double maxd[MAXF] = {0};
    TectonicNoise *tn = (TectonicNoise *)malloc(sizeof *tn);
    int variant;
    int64_t seed;
    char *tok;

    if (argc < 3) { fprintf(stderr, "usage: tectonic_check <mod|dp> <seed> [id,id,...] < harness output\n"); return 2; }
    variant = strcmp(argv[1], "dp") == 0 ? TECTONIC_DP_3_0_29 : TECTONIC_MOD_3_0_31;
    seed = strtoll(argv[2], NULL, 10);
    if (initTectonic(tn, variant, (uint64_t)seed)) { fprintf(stderr, "bad variant\n"); return 2; }
    if (argc > 3 && strcmp(argv[3], "--fields") == 0)
    {   /* the ids this variant can be checked on, as a comma separated list */
        for (i = 0; i < getTectonicFieldCount(); i++)
            if (!isnan(sampleTectonicField(tn, i, 0, 0, 0)))
                printf("%s,", getTectonicFieldName(i));
        for (i = 0; i < 6; i++) printf("%s%s", router[i], i < 5 ? "," : "\n");
        return 0;
    }
    for (tok = argc > 3 ? strtok(argv[3], ",") : NULL; tok && nf < MAXF; tok = strtok(NULL, ","))
    {
        names[nf] = tok; field[nf] = getTectonicFieldId(tok); clim[nf] = -1;
        for (i = 0; i < 6; i++) if (strcmp(tok, router[i]) == 0) clim[nf] = i;
        if (field[nf] < 0 && clim[nf] < 0) { fprintf(stderr, "unknown id %s\n", tok); return 2; }
        nf++;
    }

    while (fgets(line, sizeof line, stdin))
    {
        int x, y, z, used = 0;
        char *p = line;
        if (line[0] == 'B')
        {
            char name[128];
            if (sscanf(line + 1, "%d %d %d %127s", &x, &y, &z, name) == 4)
            {
                const char *mine = biome2str(MC_26_3, getTectonicBiomeAt(tn, 1, x, y, z));
                const char *theirs = strncmp(name, "minecraft:", 10) == 0 ? name + 10 : name;
                bn++;
                if (mine && strcmp(mine, theirs) == 0) bsame++;
                else
                {
                    int64_t np[6];
                    int tie;
                    sampleTectonicClimate(tn, np, NULL, (x >> 2) * 4, (y >> 2) * 4, (z >> 2) * 4);
                    tie = is_tied(np, theirs);
                    if (tie) bties++;
                    if (!tie || shown++ < 4)
                        printf("  biome at %d %d %d: mine %s game %s (%s)\n", x, y, z, mine ? mine : "?", theirs,
                               tie ? "exact tie: both fit this climate equally well" : "NOT a tie");
                }
            }
            continue;
        }
        if (sscanf(p, "%d %d %d%n", &x, &y, &z, &used) != 3) continue;
        p += used;
        n++;
        {
            float nv[6];
            int have_clim = 0;
            for (i = 0; i < nf; i++)
            {
                char *end;
                double r = strtod(p, &end);
                float mine;
                double d;
                if (end == p) break;
                p = end;
                if (clim[i] >= 0)
                {
                    if (!have_clim) { sampleTectonicClimate(tn, NULL, nv, x, y, z); have_clim = 1; }
                    mine = nv[clim[i]];
                }
                else mine = sampleTectonicField(tn, field[i], x, y, z);
                cmp[i]++;
                d = fabs((double)mine - (double)(float)r);      /* the game prints floats; compare as float */
                if ((float)r == mine) exact[i]++;
                else if (shown++ < 30) printf("  diff %-44s at %d %d %d: mine %.9g game %.9g\n", names[i], x, y, z, mine, r);
                if (!(d <= maxd[i])) maxd[i] = d;
            }
        }
    }
    printf("%ld points, %d functions\n", n, nf);
    ok = n > 0 || bn > 0;
    for (i = 0; i < nf; i++)
    {
        int good = cmp[i] > 0 && exact[i] == cmp[i];    /* nothing compared is a failure, not a pass */
        if (!good) ok = 0;
        printf("  %-46s compared %7ld  bit-exact %7ld  max |diff| %.3g%s\n", names[i], cmp[i], exact[i], maxd[i], good ? "" : "   <-- MISMATCH");
    }
    if (bn > 0)
    {
        printf("  biomes: compared %ld  identical %ld  different %ld, of which exact ties %ld\n", bn, bsame, bn - bsame, bties);
        if (bsame + bties != bn) ok = 0;
    }
    else if (nf == 0) ok = 0;
    printf(ok ? "OK\n" : "MISMATCH\n");
    free(tn);
    return ok ? 0 : 1;
}
