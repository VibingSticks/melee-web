#include "archive_swap.h"

#include <stdlib.h>
#include <string.h>

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
    uint8_t* reloc = data + h->data_size;
    uint8_t* pub = reloc + 4u * h->nb_reloc;
    uint8_t* ext = pub + 8u * h->nb_public;
    uint8_t* symbols = ext + 8u * h->nb_extern;
    if (h->data_size > n || (uint32_t) (symbols - f) > n) {
        return -1;
    }

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
        if (off + 4 > h->data_size) {
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

/* --- step 2: root dispatch (plan Task 14) --- */
#include "walker.h"
#include "../port.h"

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
    port_walk_ctx_free(&c);
    port_log("hsd_endian: converted %s (%u roots, %u bytes)", name, (unsigned) ar->header.nb_public,
             (unsigned) ar->header.data_size, (void*) ar->data);
    return rc;
}
