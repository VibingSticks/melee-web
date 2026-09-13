/* Host tool: convert archives straight from a disc image with the generated
 * schema and report what the schema does not cover.
 *
 *   archive_coverage <disc.iso> [--strict] [--slots N] <file.dat|--all> ...
 *
 * For each archive: root symbols without a schema, walker violations, and the
 * relocated pointer slots the walk never reached, each attributed to the
 * visited object that contains it ("HSD_TObjDesc+0x14") so the missing
 * annotation is obvious. Nothing from the disc is written anywhere. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sysdolphin/baselib/archive.h>

#include "dvd_web/fst.h"
#include "hsd_endian/archive_swap.h"
#include "hsd_endian/walker.h"

static FILE* g_iso;

static uint32_t be32(const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }

static uint8_t* read_at(uint32_t off, uint32_t len)
{
    uint8_t* b = malloc(len + 32);
    if (fseek(g_iso, off, SEEK_SET) != 0 || fread(b, 1, len, g_iso) != len) {
        fprintf(stderr, "read failed at %#x\n", off);
        exit(2);
    }
    memset(b + len, 0, 32);
    return b;
}

typedef struct {
    const uint8_t* obj;
    const port_type* type;
} vis;
static vis* g_vis;
static size_t g_nvis, g_capvis;
static void collect(void* user, const uint8_t* obj, const port_type* t)
{
    (void) user;
    if (g_nvis == g_capvis) {
        g_capvis = g_capvis ? g_capvis * 2 : 4096;
        g_vis = realloc(g_vis, g_capvis * sizeof *g_vis);
    }
    g_vis[g_nvis].obj = obj;
    g_vis[g_nvis].type = t;
    g_nvis++;
}
static int cmp_vis(const void* a, const void* b)
{
    const vis* x = a;
    const vis* y = b;
    return x->obj < y->obj ? -1 : x->obj > y->obj;
}

/* Longest prefix+suffix root match, duplicated from archive_swap.c (static there). */
static const port_root* find_root(const char* sym)
{
    const port_root* best = NULL;
    size_t best_len = 0, n = strlen(sym);
    for (const port_root* r = port_roots; r->type != NULL; r++) {
        size_t lp = r->prefix ? strlen(r->prefix) : 0, ls = r->suffix ? strlen(r->suffix) : 0;
        if (lp + ls > n || lp + ls == 0) continue;
        if (lp && strncmp(sym, r->prefix, lp) != 0) continue;
        if (ls && strcmp(sym + n - ls, r->suffix) != 0) continue;
        if (!best || lp + ls > best_len) { best = r; best_len = lp + ls; }
    }
    return best;
}

static int g_strict, g_max_slots = 12;
static int g_total_unknown, g_total_violations, g_total_unreached;

static void check_archive(const char* name, uint8_t* file, uint32_t size)
{
    port_archive_hdr hdr;
    uint32_t* rs = NULL;
    uint32_t rn = 0;
    if (port_archive_fixup(file, size, &hdr, &rs, &rn) < 0) {
        printf("%s: not an archive\n", name);
        return;
    }
    uint8_t* data = file + 0x20;
    const HSD_ArchivePublicInfo* pub = (const HSD_ArchivePublicInfo*) (data + hdr.data_size + hdr.nb_reloc * 4);
    const char* syms = (const char*) (pub + hdr.nb_public + hdr.nb_extern);
    port_walk_ctx c;
    port_walk_ctx_init(&c, data, hdr.data_size, rs, rn, g_strict);
    int unknown = 0, violations = 0;
    for (uint32_t i = 0; i < hdr.nb_public; i++) {
        const char* sym = syms + pub[i].symbol;
        const port_root* r = find_root(sym);
        if (r == NULL) {
            printf("%s: no schema for root '%s'\n", name, sym);
            unknown++;
            continue;
        }
        c.error = NULL;
        c.nlogged = 0;
        if (port_walk(&c, r->type, data + pub[i].offset) != 0 || c.error != NULL) {
            printf("%s: violation under '%s' (%s): %s\n", name, sym, r->type->name, c.error ? c.error : "?");
            violations++;
        }
    }
    /* coverage: relocated slots not inside any visited object */
    g_nvis = 0;
    port_archive_convert_scripts(&c, name);
    port_walk_visited_foreach(&c, collect, NULL);
    qsort(g_vis, g_nvis, sizeof *g_vis, cmp_vis);
    int unreached = 0;
    for (uint32_t i = 0; i < rn; i++) {
        const uint8_t* slot = data + rs[i];
        /* binary search the last visited object starting at or before slot */
        size_t lo = 0, hi = g_nvis;
        while (lo < hi) {
            size_t m = (lo + hi) / 2;
            if (g_vis[m].obj <= slot) lo = m + 1; else hi = m;
        }
        int covered = 0;
        const vis* container = NULL;
        size_t kmin = lo > 64 ? lo - 64 : 0;
        for (size_t k = lo; k > kmin; k--) { /* objects can nest; look back a little */
            const vis* v = &g_vis[k - 1];
            /* zero-size types are open-ended list roots (inline terminated arrays) */
            if (slot >= v->obj && slot < v->obj + (v->type->size ? v->type->size : 0x100000)) {
                if (!container) container = v;
                /* covered if some visited object's descriptor names this slot as a pointer */
                for (uint32_t f = 0; f < v->type->nfields; f++) {
                    const port_field* fd = &v->type->fields[f];
                    if ((fd->kind == F_PTR || fd->kind == F_PTR_ARRAY || fd->kind == F_PTR_LIST || fd->kind == F_WORD || fd->kind == F_UNION)
                        && v->obj + fd->offset == slot) covered = 1;
                    if (fd->kind == F_ARRAY && fd->type->nfields == 1 && (fd->type->fields[0].kind == F_PTR || fd->type->fields[0].kind == F_WORD)
                        && slot >= v->obj + fd->offset && ((size_t) (slot - v->obj - fd->offset) % fd->type->size) == 0) covered = 1;
                }
            }
        }
        if (!covered) {
            unreached++;
            if (unreached <= g_max_slots) {
                if (container) {
                    printf("%s: unreached pointer at +%#x inside %s+%#x\n", name, rs[i], container->type->name,
                           (unsigned) (slot - container->obj));
                } else if (lo > 0) {
                    const vis* v = &g_vis[lo - 1];
                    printf("%s: unreached pointer at +%#x, %u bytes past %s@+%#x (size %u)\n", name, rs[i],
                           (unsigned) (slot - v->obj), v->type->name, (unsigned) (v->obj - data), v->type->size);
                } else {
                    printf("%s: unreached pointer at +%#x before any visited object\n", name, rs[i]);
                }
            }
        }
    }
    printf("%s: %u roots, %u relocs, %d unknown, %d violations, %d unreached slots\n", name, hdr.nb_public, rn, unknown,
           violations, unreached);
    g_total_unknown += unknown;
    g_total_violations += violations;
    g_total_unreached += unreached;
    port_walk_ctx_free(&c);
    free(rs);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <disc.iso> [--strict] [--slots N] <file.dat|--all> ...\n", argv[0]);
        return 2;
    }
    g_iso = fopen(argv[1], "rb");
    if (!g_iso) { perror(argv[1]); return 2; }
    uint8_t hdr[0x440];
    fseek(g_iso, 0, SEEK_SET);
    fread(hdr, 1, sizeof hdr, g_iso);
    uint32_t fst_off = be32(hdr + 0x424), fst_size = be32(hdr + 0x428);
    uint8_t* fst_bytes = read_at(fst_off, fst_size);
    port_fst* fst = port_fst_parse(fst_bytes, fst_size);
    if (!fst) { fprintf(stderr, "bad FST\n"); return 2; }
    int all = 0;
    for (int a = 2; a < argc; a++) {
        if (strcmp(argv[a], "--strict") == 0) { g_strict = 1; continue; }
        if (strcmp(argv[a], "--slots") == 0) { g_max_slots = atoi(argv[++a]); continue; }
        if (strcmp(argv[a], "--all") == 0) { all = 1; continue; }
        int e = port_fst_lookup(fst, argv[a]);
        if (e < 0) { printf("%s: not on the disc\n", argv[a]); continue; }
        uint8_t* f = read_at(port_fst_file_offset(fst, e), port_fst_file_length(fst, e));
        check_archive(argv[a], f, port_fst_file_length(fst, e));
        free(f);
    }
    if (all) {
        int n = port_fst_entry_count(fst);
        for (int e = 1; e < n; e++) {
            if (port_fst_is_dir(fst, e)) continue;
            const char* nm = port_fst_entry_name(fst, e);
            size_t l = strlen(nm);
            if (l < 4 || (strcmp(nm + l - 4, ".dat") != 0 && strcmp(nm + l - 4, ".usd") != 0)) continue;
            uint8_t* f = read_at(port_fst_file_offset(fst, e), port_fst_file_length(fst, e));
            check_archive(nm, f, port_fst_file_length(fst, e));
            free(f);
        }
    }
    printf("total: %d unknown roots, %d violations, %d unreached slots\n", g_total_unknown, g_total_violations, g_total_unreached);
    return (g_total_violations || g_total_unreached) ? 1 : 0;
}
