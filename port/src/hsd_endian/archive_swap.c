#include "archive_swap.h"

#include <stdlib.h>
#include <string.h>

/* Milliseconds, for reporting how long a conversion blocked the frame. Host
 * tests build this file too and have no emscripten.h. */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define PORT_NOW_MS() emscripten_get_now()
#else
#define PORT_NOW_MS() 0.0
#endif

static uint32_t rd_be32(const uint8_t* p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static uint32_t rd_native32(const uint8_t* p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void wr_native32(uint8_t* p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static int cmp_u32(const void* a, const void* b)
{
    uint32_t x = *(const uint32_t*) a, y = *(const uint32_t*) b;
    return x < y ? -1 : x > y;
}

uint32_t port_archive_read_be32(const void* p)
{
    return rd_be32(p);
}

int port_archive_fixup(uint8_t* f, uint32_t n, port_archive_hdr* h, uint32_t** rs_out, uint32_t* rn_out)
{
    if (f == NULL || n < 0x20) {
        return -1;
    }
    int already_native = 0;
    if (rd_be32(f) != n) {
        if (rd_native32(f) == n) {
            already_native = 1; /* converted earlier; only rebuild the reloc set */
        } else {
            return -1;
        }
    }
    uint32_t (*rd)(const uint8_t*) = already_native ? rd_native32 : rd_be32;

    h->file_size = rd(f);
    h->data_size = rd(f + 4);
    h->nb_reloc = rd(f + 8);
    h->nb_public = rd(f + 12);
    h->nb_extern = rd(f + 16);

    uint8_t* data = f + 0x20;
    /* The three counts come straight off the disc, so size the tables in
     * 64-bit: 4 * nb_reloc wraps for counts past 2^30 and would let a tiny
     * buffer pass the check below (and malloc a tiny reloc array). */
    uint64_t need = (uint64_t) 0x20 + h->data_size + 4ull * h->nb_reloc + 8ull * h->nb_public + 8ull * h->nb_extern;
    if (h->data_size > n || need > n) {
        return -1;
    }
    uint8_t* reloc = data + h->data_size;
    uint8_t* pub = reloc + 4u * h->nb_reloc;
    uint8_t* ext = pub + 8u * h->nb_public;
    /* The symbol table follows at ext + 8 * nb_extern; `need` above already
     * accounts for it, and ar->symbols is set from the parsed header. */

    uint32_t* rs = malloc(4u * (h->nb_reloc != 0 ? h->nb_reloc : 1));
    if (rs == NULL) {
        return -1;
    }

    if (!already_native) {
        wr_native32(f, h->file_size);
        wr_native32(f + 4, h->data_size);
        wr_native32(f + 8, h->nb_reloc);
        wr_native32(f + 12, h->nb_public);
        wr_native32(f + 16, h->nb_extern);
        /* version[4] stays as bytes; pad[2] is unused */
    }

    for (uint32_t i = 0; i < h->nb_reloc; i++) {
        uint32_t off = rd(reloc + 4u * i);
        if (h->data_size < 4 || off > h->data_size - 4) {
            free(rs);
            return -1;
        }
        rs[i] = off;
        if (!already_native) {
            wr_native32(reloc + 4u * i, off);
            wr_native32(data + off, rd_be32(data + off) + (uint32_t) (uintptr_t) data);
        }
    }
    if (!already_native) {
        for (uint32_t i = 0; i < h->nb_public; i++) {
            wr_native32(pub + 8u * i, rd_be32(pub + 8u * i));
            wr_native32(pub + 8u * i + 4, rd_be32(pub + 8u * i + 4));
        }
        for (uint32_t i = 0; i < h->nb_extern; i++) {
            wr_native32(ext + 8u * i, rd_be32(ext + 8u * i));
            wr_native32(ext + 8u * i + 4, rd_be32(ext + 8u * i + 4));
        }
    }
    qsort(rs, h->nb_reloc, 4, cmp_u32);
    *rs_out = rs;
    *rn_out = h->nb_reloc;
    return already_native;
}

/* --- which parses converted their buffer ---
 * The game converts a few tables itself on its load path (the fighter
 * animation tables and scripts in ftdata.c) because their lengths live in
 * the code. An archive handed back by the preload cache is parsed again over
 * the same, already native, buffer; the fixup reports that and the root walk
 * is skipped, and those tables must be skipped too, or they swap straight
 * back to big-endian. Each parse records here whether it converted its
 * buffer; the game asks with a pointer into the data, and the answer is
 * consumed: the cache can hand the same parsed archive back without parsing
 * it again (the fighters the intro splash showed are the match's), so only
 * the first load after a converting parse gets a yes. */
typedef struct {
    const uint8_t* data;
    uint32_t size;
    int fresh;
} parse_note;

#define PORT_PARSE_NOTES 64
static parse_note g_parse_notes[PORT_PARSE_NOTES];
static unsigned g_parse_next;

void port_archive_note_parse(const void* data, uint32_t size, int fresh)
{
    const uint8_t* d = data;
    for (unsigned i = 0; i < PORT_PARSE_NOTES; i++) {
        parse_note* n = &g_parse_notes[i];
        if (n->data != NULL && d < n->data + n->size && n->data < d + size) {
            n->data = d; /* the same buffer, or one reusing its memory: the newer parse rules */
            n->size = size;
            n->fresh = fresh;
            return;
        }
    }
    parse_note* n = &g_parse_notes[g_parse_next++ % PORT_PARSE_NOTES];
    n->data = d;
    n->size = size;
    n->fresh = fresh;
}

int port_archive_take_fresh(const void* p)
{
    const uint8_t* q = p;
    for (unsigned i = 0; i < PORT_PARSE_NOTES; i++) {
        parse_note* n = &g_parse_notes[i];
        if (n->data != NULL && q >= n->data && q < n->data + n->size) {
            int fresh = n->fresh;
            n->fresh = 0; /* the caller converts its tables now; a later load of this buffer must not */
            return fresh;
        }
    }
    return 1; /* not a parsed archive we know of: convert, as before */
}

/* --- step 2: root dispatch (plan Task 14) --- */
#include "formats.h"
#include "walker.h"
#include "../port.h"

/* --- bytecode reached through the walk ---
 * Item state rows (ItemStateDesc) point at scripts the walk cannot describe:
 * the words are bitfield commands whose layout depends on the opcode. The rows
 * the walk visited are collected here and their scripts converted afterwards,
 * while the context is alive to record the pointer words the converter leaves
 * in place. */
typedef struct {
    const void** slots;
    uint32_t n, cap;
    int oom;
} script_slots;

static void collect_item_scripts(void* user, const uint8_t* obj, const port_type* t)
{
    script_slots* s = user;
    if (strcmp(t->name, "ItemStateDesc") != 0) {
        return;
    }
    if (s->n == s->cap) {
        uint32_t ncap = s->cap != 0 ? s->cap * 2 : 256;
        const void** p = realloc((void*) s->slots, ncap * sizeof *p);
        if (p == NULL) {
            s->oom = 1;
            return;
        }
        s->slots = p;
        s->cap = ncap;
    }
    s->slots[s->n++] = obj + 12; /* ItemStateDesc::xC_script */
}

static void note_script_ptr(void* user, const void* slot)
{
    port_walk_mark_slot(user, slot);
}

/* ground.c (Ground_801C0800) stores the stage's "ALDYakuAll" entries, from
 * index 1 to the first NULL, as the scripts of the Random-Pokemon article's
 * state rows: they are item scripts like the rows' own, in a table the walk
 * cannot describe (the root stays opaque). */
static void collect_yaku_scripts(const port_walk_ctx* c, const HSD_ArchivePublicInfo* pub, uint32_t nb_public,
                                 const char* syms, script_slots* s)
{
    for (uint32_t i = 0; i < nb_public; i++) {
        if (strcmp(syms + pub[i].symbol, "ALDYakuAll") != 0) {
            continue;
        }
        const uint8_t* list = c->base + pub[i].offset;
        for (uint32_t k = 1;; k++) {
            uint32_t v;
            if (pub[i].offset + 4u * k + 4u > c->size) {
                break;
            }
            memcpy(&v, list + 4u * k, 4);
            if (v == 0) {
                break;
            }
            if (s->n == s->cap) {
                uint32_t ncap = s->cap != 0 ? s->cap * 2 : 256;
                const void** p = realloc((void*) s->slots, ncap * sizeof *p);
                if (p == NULL) {
                    s->oom = 1;
                    return;
                }
                s->slots = p;
                s->cap = ncap;
            }
            s->slots[s->n++] = list + 4u * k;
        }
    }
}

/* Colour-overlay scripts (lb_013B.c): the rows of every Fighter_804D653C_t
 * table the walk visited -- PlCo.dat's ftLoadCommonData colanim tables
 * (ftcolanim.c) and ItCo.dat's itPublicData x14 (itanimlist.c). Each row's
 * first word is its script. */
static void collect_colanim_scripts(void* user, const uint8_t* obj, const port_type* t)
{
    script_slots* s = user;
    if (strcmp(t->name, "Fighter_804D653C_t") != 0) {
        return;
    }
    if (s->n == s->cap) {
        uint32_t ncap = s->cap != 0 ? s->cap * 2 : 256;
        const void** p = realloc((void*) s->slots, ncap * sizeof *p);
        if (p == NULL) {
            s->oom = 1;
            return;
        }
        s->slots = p;
        s->cap = ncap;
    }
    s->slots[s->n++] = obj; /* Fighter_804D653C_t::unk */
}

static int has_root(const HSD_ArchivePublicInfo* pub, uint32_t nb_public, const char* syms, const char* sym)
{
    for (uint32_t i = 0; i < nb_public; i++) {
        if (strcmp(syms + pub[i].symbol, sym) == 0) {
            return 1;
        }
    }
    return 0;
}

void port_archive_convert_scripts(port_walk_ctx* c, const HSD_ArchivePublicInfo* pub, uint32_t nb_public,
                                  const char* syms, const char* name)
{
    script_slots s;
    memset(&s, 0, sizeof s);
    port_walk_visited_foreach(c, collect_item_scripts, &s);
    collect_yaku_scripts(c, pub, nb_public, syms, &s);
    if (s.oom) {
        port_log("hsd_endian: out of memory collecting item scripts in %s", name);
    } else if (s.n != 0) {
        port_swap_it_cmd_scripts(c->base, s.slots, s.n, note_script_ptr, c);
    }
    free((void*) s.slots);

    /* From opcode 21 a colour-overlay script runs the commands of whoever
     * plays it: items for ItCo.dat's tables, fighters for PlCo.dat's. */
    memset(&s, 0, sizeof s);
    port_walk_visited_foreach(c, collect_colanim_scripts, &s);
    if (s.oom) {
        port_log("hsd_endian: out of memory collecting colour-overlay scripts in %s", name);
    } else if (s.n != 0) {
        int item_scripts = has_root(pub, nb_public, syms, "itPublicData");
        port_swap_co_cmd_scripts(c->base, s.slots, s.n, item_scripts, note_script_ptr, c);
    }
    free((void*) s.slots);
}

/* Longest prefix+suffix match of `sym` in port_roots; NULL when none matches. */
static const port_root* find_root(const char* sym)
{
    const port_root* best = NULL;
    size_t best_len = 0;
    size_t n = strlen(sym);
    for (const port_root* r = port_roots; r->type != NULL; r++) {
        size_t lp = r->prefix != NULL ? strlen(r->prefix) : 0;
        size_t ls = r->suffix != NULL ? strlen(r->suffix) : 0;
        if (lp + ls > n || lp + ls == 0) {
            continue;
        }
        if (lp != 0 && strncmp(sym, r->prefix, lp) != 0) {
            continue;
        }
        if (ls != 0 && strcmp(sym + n - ls, r->suffix) != 0) {
            continue;
        }
        if (best == NULL || lp + ls > best_len) {
            best = r;
            best_len = lp + ls;
        }
    }
    return best;
}

int port_archive_swap_roots(HSD_Archive* ar, const uint32_t* reloc_set, uint32_t reloc_count)
{
    port_walk_ctx c;
#ifdef PORT_STRICT_SCHEMA
    const int strict = 1;
#else
    const int strict = 0;
#endif
    /* HSD_ArchiveParse runs before the loader names the archive: fall back to its first symbol */
    const char* name = ar->name != NULL ? ar->name
                       : ar->header.nb_public != 0 ? ar->symbols + ar->public_info[0].symbol : "?";
    int rc = 0;
    if (port_walk_ctx_init(&c, ar->data, ar->header.data_size, reloc_set, reloc_count, strict) != 0) {
        port_log("hsd_endian: out of memory converting %s", name);
        return -1;
    }
    double t0 = PORT_NOW_MS();
    {
        /* The roots are object starts too: an object run must not cross into one. */
        uint32_t n = ar->header.nb_public;
        uint32_t* starts = malloc((n != 0 ? n : 1) * sizeof *starts);
        int set = -1;
        if (starts != NULL) {
            for (uint32_t i = 0; i < n; i++) {
                starts[i] = ar->public_info[i].offset;
            }
            set = port_walk_ctx_set_object_starts(&c, starts, n);
        }
        free(starts);
        if (set != 0) {
            port_log("hsd_endian: out of memory converting %s", name);
            port_walk_ctx_free(&c);
            return -1;
        }
    }
    for (uint32_t i = 0; i < ar->header.nb_public; i++) {
        const char* sym = ar->symbols + ar->public_info[i].symbol;
        const port_root* root = find_root(sym);
        if (root == NULL) {
            port_log("hsd_endian: no schema for root symbol '%s' in %s", sym, name);
            if (strict) {
                abort();
            }
            continue;
        }
        c.error = NULL;
        if (port_walk(&c, root->type, ar->data + ar->public_info[i].offset) != 0 || c.error != NULL) {
            port_log("hsd_endian: %s under '%s' (%s) in %s", c.error != NULL ? c.error : "violation", sym,
                     root->type->name, name);
            rc = -1;
            if (strict) {
                abort();
            }
        }
    }
    port_archive_convert_scripts(&c, ar->public_info, ar->header.nb_public, ar->symbols, name);
    port_walk_ctx_free(&c);
    /* This runs synchronously inside whatever frame asked for the archive, so
     * a large one is a visible pause rather than a slow average. */
    port_log("hsd_endian: converted %s (%u roots, %u bytes) in %.1f ms", name, (unsigned) ar->header.nb_public,
             (unsigned) ar->header.data_size, PORT_NOW_MS() - t0);
    return rc;
}
