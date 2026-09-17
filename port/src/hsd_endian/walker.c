#include "walker.h"

#include <stdlib.h>
#include <string.h>

#include "../port.h"

/* --- visited set: open addressing on (address, type) --- */
typedef struct {
    uintptr_t addr;
    const port_type* type;
} vkey;

typedef struct {
    vkey* slots;
    uint32_t cap; /* power of two */
    uint32_t n;
} vset;

static uint32_t vhash(uintptr_t a, const port_type* t)
{
    uint64_t h = (uint64_t) a * 0x9E3779B97F4A7C15ull ^ ((uint64_t) (uintptr_t) t * 0xC2B2AE3D27D4EB4Full);
    return (uint32_t) (h >> 32) ^ (uint32_t) h;
}

static int vset_grow(vset* s)
{
    uint32_t ncap = s->cap != 0 ? s->cap * 2 : 1024;
    vkey* nslots = calloc(ncap, sizeof *nslots);
    if (nslots == NULL) {
        return -1;
    }
    for (uint32_t i = 0; i < s->cap; i++) {
        if (s->slots[i].addr != 0) {
            uint32_t j = vhash(s->slots[i].addr, s->slots[i].type) & (ncap - 1);
            while (nslots[j].addr != 0) {
                j = (j + 1) & (ncap - 1);
            }
            nslots[j] = s->slots[i];
        }
    }
    free(s->slots);
    s->slots = nslots;
    s->cap = ncap;
    return 0;
}

/* Returns 1 if newly added, 0 if already present, -1 on allocation failure. */
static int vset_add(vset* s, uintptr_t a, const port_type* t)
{
    if ((s->n + 1) * 4 > s->cap * 3 && vset_grow(s) != 0) {
        return -1;
    }
    uint32_t j = vhash(a, t) & (s->cap - 1);
    while (s->slots[j].addr != 0) {
        if (s->slots[j].addr == a && s->slots[j].type == t) {
            return 0;
        }
        j = (j + 1) & (s->cap - 1);
    }
    s->slots[j].addr = a;
    s->slots[j].type = t;
    s->n++;
    return 1;
}

int port_walk_ctx_init(port_walk_ctx* c, const uint8_t* base, uint32_t size, const uint32_t* rs, uint32_t rn, int strict)
{
    memset(c, 0, sizeof *c);
    c->base = base;
    c->size = size;
    c->reloc_set = rs;
    c->reloc_count = rn;
    c->strict = strict;
    c->visited = calloc(1, sizeof(vset));
    c->converted = calloc((size + 7) / 8 + 1, 1);
    return (c->visited != NULL && c->converted != NULL) ? 0 : -1;
}

void port_walk_ctx_free(port_walk_ctx* c)
{
    vset* s = c->visited;
    if (s != NULL) {
        free(s->slots);
        free(s);
    }
    c->visited = NULL;
    free(c->converted);
    c->converted = NULL;
    free(c->targets);
    c->targets = NULL;
    c->ntargets = 0;
}

/* --- helpers --- */
static int in_reloc(const port_walk_ctx* c, uint32_t off)
{
    uint32_t lo = 0, hi = c->reloc_count;
    while (lo < hi) {
        uint32_t m = lo + (hi - lo) / 2;
        if (c->reloc_set[m] < off) {
            lo = m + 1;
        } else if (c->reloc_set[m] > off) {
            hi = m;
        } else {
            return 1;
        }
    }
    return 0;
}

static int cmp_u32(const void* a, const void* b)
{
    uint32_t x = *(const uint32_t*) a, y = *(const uint32_t*) b;
    return x < y ? -1 : x > y;
}

/* Is `off` the start of an object: the target of some relocated pointer in
 * the archive? The sorted target list is built the first time it is needed,
 * from the (already native) values in the relocation slots. */
static int is_target(port_walk_ctx* c, uint32_t off)
{
    if (c->targets == NULL) {
        c->targets = malloc((c->reloc_count + 1) * sizeof *c->targets);
        if (c->targets == NULL) {
            return 0;
        }
        c->ntargets = 0;
        for (uint32_t i = 0; i < c->reloc_count; i++) {
            uint32_t v, t;
            memcpy(&v, c->base + c->reloc_set[i], 4);
            t = v - (uint32_t) (uintptr_t) c->base;
            if (v != 0 && t < c->size) {
                c->targets[c->ntargets++] = t;
            }
        }
        qsort(c->targets, c->ntargets, sizeof *c->targets, cmp_u32);
    }
    uint32_t lo = 0, hi = c->ntargets;
    while (lo < hi) {
        uint32_t m = lo + (hi - lo) / 2;
        if (c->targets[m] < off) {
            lo = m + 1;
        } else if (c->targets[m] > off) {
            hi = m;
        } else {
            return 1;
        }
    }
    return 0;
}

/* Claims `n` bytes at `p` for conversion. Returns 0 when any of them has been
 * converted already: two descriptors can reach the same bytes by different
 * paths (a union case, a shared table), and swapping twice restores the
 * original byte order. */
static int claim(port_walk_ctx* c, const uint8_t* p, uint32_t n)
{
    uint32_t off = (uint32_t) (p - c->base);
    for (uint32_t i = 0; i < n; i++) {
        if (c->converted[(off + i) >> 3] & (1u << ((off + i) & 7))) {
            return 0;
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        c->converted[(off + i) >> 3] |= (uint8_t) (1u << ((off + i) & 7));
    }
    return 1;
}

static int inside(const port_walk_ctx* c, const void* p, uint32_t n)
{
    const uint8_t* q = p;
    return q >= c->base && n <= c->size && q <= c->base + c->size - n;
}

/* Pointer slots are 4 bytes (the on-disc pointer size, relocated to native
 * addresses by port_archive_fixup). Resolve one relative to the archive base
 * so the walker also works in 64-bit host tests. Returns 0 for NULL, 1 when
 * the target lies inside the archive, -1 when it does not. */
static int resolve_ptr(const port_walk_ctx* c, const uint8_t* slot, uint32_t need, uint8_t** out)
{
    uint32_t v;
    if (!inside(c, slot, 4)) {
        *out = NULL;
        return -1;
    }
    memcpy(&v, slot, 4);
    if (v == 0) {
        *out = NULL;
        return 0;
    }
    if (!in_reloc(c, (uint32_t) (slot - c->base))) {
        /* Every real pointer in an archive is in the relocation table; a slot that is not
         * cannot be followed (an over-long array, or a scalar mis-described as a pointer). */
        *out = NULL;
        return -2;
    }
    uint32_t off = v - (uint32_t) (uintptr_t) c->base;
    if (off > c->size || need > c->size - off) {
        *out = NULL;
        return -1;
    }
    *out = (uint8_t*) c->base + off;
    return 1;
}

static void swap16(uint8_t* p)
{
    uint8_t t = p[0];
    p[0] = p[1];
    p[1] = t;
}

static void swap32(uint8_t* p)
{
    uint8_t t = p[0];
    p[0] = p[3];
    p[3] = t;
    t = p[1];
    p[1] = p[2];
    p[2] = t;
}

static void swap64(uint8_t* p)
{
    for (int i = 0; i < 4; i++) {
        uint8_t t = p[i];
        p[i] = p[7 - i];
        p[7 - i] = t;
    }
}

void port_repack_bits(uint8_t* p, uint8_t storage, const uint8_t* widths, uint8_t n)
{
    uint64_t v = 0;
    for (int i = 0; i < storage; i++) { /* big-endian numeric value of the unit */
        v = (v << 8) | p[i];
    }
    unsigned total = storage * 8u;
    unsigned used = 0;
    for (uint8_t i = 0; i < n; i++) {
        used += widths[i];
    }
    if (used > total) {
        return; /* malformed descriptor; leave the bytes alone */
    }
    uint64_t out = 0;
    unsigned msb_pos = total; /* MSB-first: first field occupies the top bits */
    unsigned lsb_pos = 0;     /* LSB-first: first field occupies the low bits */
    for (uint8_t i = 0; i < n; i++) {
        unsigned w = widths[i];
        msb_pos -= w;
        uint64_t field = (v >> msb_pos) & ((1ull << w) - 1);
        out |= field << lsb_pos;
        lsb_pos += w;
    }
    if (used < total) { /* unused low bits of the BE unit become the unused high bits */
        out |= (v & ((1ull << (total - used)) - 1)) << used;
    }
    for (int i = 0; i < storage; i++) { /* little-endian */
        p[i] = (uint8_t) (out >> (8 * i));
    }
}

static uint32_t read_len(const port_field* f, const uint8_t* obj)
{
    switch (f->len_kind) {
    case LEN_CONST: return f->len;
    case LEN_FIELD_U32: {
        uint32_t v;
        memcpy(&v, obj + f->len, 4);
        return v;
    }
    case LEN_FIELD_U16: {
        uint16_t v;
        memcpy(&v, obj + f->len, 2);
        return v;
    }
    case LEN_FIELD_U8: return obj[f->len];
    case LEN_NULL_TERM:
    case LEN_TERM_VALUE:
    case LEN_RELOC_RUN:
    case LEN_OBJECT_RUN: return 0xFFFFFFFFu;
    }
    return 0;
}

/* Does `t` hold any pointer (directly or in an inline struct or array)?
 * Decides whether an object run can ask looks_like() about its elements. */
static int type_has_pointers(const port_type* t, unsigned depth)
{
    if (t == NULL || depth > 8) {
        return 0;
    }
    for (uint32_t i = 0; i < t->nfields; i++) {
        const port_field* f = &t->fields[i];
        if (f->kind == F_PTR || f->kind == F_PTR_ARRAY || f->kind == F_PTR_LIST) {
            return 1;
        }
        if ((f->kind == F_STRUCT || f->kind == F_ARRAY) && type_has_pointers(f->type, depth + 1)) {
            return 1;
        }
    }
    return 0;
}

static int looks_like(const port_walk_ctx* c, const port_type* t, const uint8_t* obj);

/* One more element of an object run at `e` (element `i`)? The run stops at the
 * end of the archive, at the start of another object, and, when the element
 * type can be recognised by its pointers, at the first element that is not one. */
static int object_run_continues(port_walk_ctx* c, const port_type* t, const uint8_t* e, uint32_t i)
{
    if (!inside(c, e, t->size)) {
        return 0;
    }
    if (i != 0 && is_target(c, (uint32_t) (e - c->base))) {
        return 0;
    }
    return !type_has_pointers(t, 0) || looks_like(c, t, e);
}

/* Does `obj` still look like an instance of `t`? Every pointer it holds,
 * including the ones in nested structs and arrays, must be null or a slot the
 * archive's relocation table named. Pointers are the only evidence available,
 * so a type that contains none cannot be recognised at all: looks_like() then
 * reports failure rather than accepting every byte pattern. */
static int looks_like_rec(const port_walk_ctx* c, const port_type* t, const uint8_t* obj, unsigned depth,
                          int* saw_pointer)
{
    if (t == NULL || depth > 8 || !inside(c, obj, t->size)) {
        return 0;
    }
    for (uint32_t i = 0; i < t->nfields; i++) {
        const port_field* f = &t->fields[i];
        const uint8_t* p = obj + f->offset;
        switch (f->kind) {
        case F_PTR:
        case F_PTR_ARRAY:
        case F_PTR_LIST: {
            /* One slot in every case. An F_PTR_ARRAY is a single pointer TO
             * an array, not an inline run of slots -- reading read_len() of
             * them here walks off into the sibling fields and makes any type
             * with such a field fail to match. */
            uint32_t n = 1;
            for (uint32_t k = 0; k < n; k++) {
                const uint8_t* q = p + 4 * k;
                uint32_t v;
                if (!inside(c, q, 4)) {
                    return 0;
                }
                memcpy(&v, q, 4);
                *saw_pointer = 1;
                if (v != 0 && !in_reloc(c, (uint32_t) (q - c->base))) {
                    return 0;
                }
            }
            break;
        }
        case F_STRUCT:
            if (!looks_like_rec(c, f->type, p, depth + 1, saw_pointer)) {
                return 0;
            }
            break;
        case F_ARRAY: {
            uint32_t n = read_len(f, obj);
            if (f->type == NULL || n == 0xFFFFFFFFu) {
                break;
            }
            for (uint32_t k = 0; k < n; k++) {
                if (!looks_like_rec(c, f->type, p + (size_t) k * f->type->size, depth + 1, saw_pointer)) {
                    return 0;
                }
            }
            break;
        }
        default: break;
        }
    }
    return 1;
}

static int looks_like(const port_walk_ctx* c, const port_type* t, const uint8_t* obj)
{
    int saw_pointer = 0;
    return looks_like_rec(c, t, obj, 0, &saw_pointer) && saw_pointer;
}

/* `slot` is the byte the walk choked on; its archive offset is what you need to
 * go look at the data, so the message carries it. */
static int fail(port_walk_ctx* c, const char* what, const void* slot)
{
    if (c->error == NULL) {
        c->error = what;
    }
    if (c->nlogged < 16) {
        const uint8_t* q = slot;
        c->nlogged++;
        port_log("hsd_endian: %s at %s.%s+%u (archive +0x%x)", what, c->cur_type != NULL ? c->cur_type->name : "?",
                 c->cur_field != NULL && c->cur_field->name != NULL ? c->cur_field->name : "?",
                 c->cur_field != NULL ? (unsigned) c->cur_field->offset : 0u,
                 q != NULL ? (unsigned) (q - c->base) : 0u);
    }
    return c->strict ? -1 : 0;
}

static int walk_obj(port_walk_ctx* c, const port_type* t, uint8_t* obj);

static int walk_field(port_walk_ctx* c, const port_field* f, uint8_t* obj)
{
    uint8_t* p = obj + f->offset;
    c->cur_field = f;
    switch (f->kind) {
    case F_U8:
    case F_OPAQUE:
        return 0;
    case F_U16:
    case F_U32:
    case F_U64:
    case F_F32:
    case F_F64:
    case F_BITS:
        if (!inside(c, p, 1)) {
            return fail(c, "scalar outside the archive", p);
        }
        if (in_reloc(c, (uint32_t) (p - c->base))) {
            return fail(c, "scalar descriptor on a pointer slot", p);
        }
        {
            uint32_t width = f->kind == F_U16 ? 2
                             : (f->kind == F_U64 || f->kind == F_F64) ? 8
                             : f->kind == F_BITS ? f->bits_storage
                                                 : 4;
            if (!inside(c, p, width) || !claim(c, p, width)) {
                return 0;
            }
        }
        if (f->kind == F_U16) {
            swap16(p);
        } else if (f->kind == F_U32 || f->kind == F_F32) {
            swap32(p);
        } else if (f->kind == F_U64 || f->kind == F_F64) {
            swap64(p);
        } else {
            port_repack_bits(p, f->bits_storage, f->bit_widths, f->nbits);
        }
        return 0;
    case F_WORD: {
        uint8_t* q;
        if (!inside(c, p, 4)) {
            return fail(c, "word outside the archive", p);
        }
        if (!in_reloc(c, (uint32_t) (p - c->base))) {
            if (claim(c, p, 4)) {
                swap32(p);
            }
            return 0;
        }
        if (f->type == NULL) {
            return 0;
        }
        int r = resolve_ptr(c, p, f->type->size, &q);
        if (r == 0) {
            return 0;
        }
        if (r < 0) {
            return fail(c, "word pointer outside the archive", p);
        }
        return walk_obj(c, f->type, q);
    }
    case F_STRUCT:
        return walk_obj(c, f->type, p);
    case F_ARRAY: {
        uint32_t n = read_len(f, obj);
        if (f->len_kind == LEN_RELOC_RUN) {
            n = 0;
            while (n < 0x1000 && looks_like(c, f->type, p + n * f->type->size)) {
                n++;
            }
        } else if (f->len_kind == LEN_OBJECT_RUN) {
            n = 0;
            while (n < 0x100000 && object_run_continues(c, f->type, p + n * f->type->size, n)) {
                n++;
            }
        } else if (f->len_kind == LEN_NULL_TERM || f->len_kind == LEN_TERM_VALUE) {
            /* inline terminated array (a root symbol that is a list): count elements first */
            n = 0;
            for (;;) {
                const uint8_t* e = p + n * f->type->size;
                if (!inside(c, e, f->type->size) || !inside(c, e, 4)) {
                    break;
                }
                uint32_t be = ((uint32_t) e[0] << 24) | ((uint32_t) e[1] << 16) | ((uint32_t) e[2] << 8) | e[3];
                uint32_t le = ((uint32_t) e[3] << 24) | ((uint32_t) e[2] << 16) | ((uint32_t) e[1] << 8) | e[0];
                if (f->len_kind == LEN_NULL_TERM ? be == 0 : (be == f->len || le == f->len)) {
                    if (f->len_kind == LEN_TERM_VALUE && be == f->len && !in_reloc(c, (uint32_t) (e - c->base))
                        && claim(c, e, 4)) {
                        swap32((uint8_t*) e);
                    }
                    break;
                }
                n++;
            }
        }
        if (f->type->nfields == 1 && f->type->fields[0].offset == 0 && f->type->fields[0].kind != F_STRUCT
            && f->type->fields[0].kind != F_ARRAY) {
            /* Plain array of scalars, pointers or words: walk the one field per
             * element rather than paying for a visited-set entry each time.
             * Elements holding a pointer are still recorded, because that set is
             * what tells a caller which relocated slots the walk reached -- and
             * without them every slot past the first looks unvisited. */
            int is_ptr = f->type->fields[0].kind == F_PTR || f->type->fields[0].kind == F_PTR_ARRAY;
            for (uint32_t i = 0; i < n; i++) {
                uint8_t* e = p + i * f->type->size;
                if (!inside(c, e, f->type->size)) {
                    return fail(c, "array element outside the archive", e);
                }
                if (is_ptr && vset_add(c->visited, (uintptr_t) e, f->type) < 0) {
                    return fail(c, "out of memory", e);
                }
                if (walk_field(c, &f->type->fields[0], e) != 0) {
                    return -1;
                }
            }
            return 0;
        }
        for (uint32_t i = 0; i < n; i++) {
            uint8_t* e = p + i * f->type->size;
            if (!inside(c, e, f->type->size)) {
                return fail(c, "array element outside the archive", e);
            }
            if (walk_obj(c, f->type, e) != 0) {
                return -1;
            }
        }
        return 0;
    }
    case F_PTR: {
        uint8_t* q;
        if (f->type == NULL) {
            return 0;
        }
        int r = resolve_ptr(c, p, f->type->size, &q);
        if (r == 0) {
            return 0;
        }
        if (r == -2) {
            return fail(c, "pointer descriptor on an unrelocated slot", p);
        }
        if (r < 0) {
            return fail(c, "pointer outside the archive", p);
        }
        return walk_obj(c, f->type, q);
    }
    case F_PTR_ARRAY: {
        uint8_t* q;
        int r = resolve_ptr(c, p, 0, &q);
        if (r == 0) {
            return 0;
        }
        if (r == -2) {
            return fail(c, "array pointer descriptor on an unrelocated slot", p);
        }
        if (r < 0) {
            return fail(c, "array pointer outside the archive", p);
        }
        uint32_t n = read_len(f, obj);
        for (uint32_t i = 0; i < n; i++) {
            uint8_t* e = q + i * f->type->size;
            if (f->len_kind == LEN_OBJECT_RUN) {
                if (!object_run_continues(c, f->type, e, i)) {
                    break;
                }
            } else if (!inside(c, e, f->type->size)) {
                return fail(c, "array element outside the archive", e);
            }
            if (f->len_kind == LEN_NULL_TERM) {
                uint32_t first;
                memcpy(&first, e, 4);
                if (first == 0) {
                    break;
                }
            }
            if (f->len_kind == LEN_TERM_VALUE) {
                /* A list shared by several roots was converted on the first visit, so the
                 * terminator may already be native: accept either byte order. */
                uint32_t be = ((uint32_t) e[0] << 24) | ((uint32_t) e[1] << 16) | ((uint32_t) e[2] << 8) | e[3];
                uint32_t le = ((uint32_t) e[3] << 24) | ((uint32_t) e[2] << 16) | ((uint32_t) e[1] << 8) | e[0];
                if (be == f->len) {
                    if (!in_reloc(c, (uint32_t) (e - c->base)) && claim(c, e, 4)) {
                        swap32(e); /* the terminator is compared natively by the game */
                    }
                    break;
                }
                if (le == f->len) {
                    break;
                }
            }
            if (walk_obj(c, f->type, e) != 0) {
                return -1;
            }
        }
        return 0;
    }
    case F_PTR_LIST: {
        uint8_t* q;
        int r = resolve_ptr(c, p, 0, &q);
        if (r == 0) {
            return 0;
        }
        if (r == -2) {
            return fail(c, "list pointer descriptor on an unrelocated slot", p);
        }
        if (r < 0) {
            return fail(c, "list pointer outside the archive", p);
        }
        for (uint32_t i = 0; i < f->len; i++) {
            uint8_t* slot = q + 4 * i;
            uint8_t* e;
            const port_type* t = f->disc_types[i];
            if (!inside(c, slot, 4)) {
                return fail(c, "list element outside the archive", slot);
            }
            /* Recorded like the elements of a pointer array: the visited set is
             * what says which relocated slots the walk reached. */
            if (vset_add(c->visited, (uintptr_t) slot, f->type) < 0) {
                return fail(c, "out of memory", slot);
            }
            if (t == NULL) {
                continue;
            }
            r = resolve_ptr(c, slot, t->size, &e);
            if (r == 0) {
                continue;
            }
            if (r == -2) {
                return fail(c, "list element descriptor on an unrelocated slot", slot);
            }
            if (r < 0) {
                return fail(c, "list element outside the archive", slot);
            }
            if (walk_obj(c, t, e) != 0) {
                return -1;
            }
        }
        return 0;
    }
    case F_UNION: {
        const uint8_t* dp = obj + f->disc_offset;
        unsigned sz = f->disc_size != 0 ? f->disc_size : 4;
        uint32_t d = 0;
        if (!inside(c, dp, sz)) {
            return fail(c, "union discriminator outside the archive", dp);
        }
        if (f->disc_be) { /* not converted yet: it lives inside the union itself */
            for (unsigned i = 0; i < sz; i++) {
                d = (d << 8) | dp[i];
            }
        } else { /* a sibling that precedes the union, already native */
            for (unsigned i = 0; i < sz; i++) {
                d |= (uint32_t) dp[i] << (8 * i);
            }
        }
        if (f->disc_mask != 0) {
            d &= f->disc_mask;
        }
        for (uint8_t i = 0; i < f->ncases; i++) {
            if (f->disc_values[i] == d) {
                return f->disc_types[i] != NULL ? walk_obj(c, f->disc_types[i], p) : 0;
            }
        }
        return fail(c, "union discriminator has no case", dp);
    }
    }
    return fail(c, "unknown field kind", obj);
}

static int walk_obj(port_walk_ctx* c, const port_type* t, uint8_t* obj)
{
    if (t == NULL) {
        return 0;
    }
    if (!inside(c, obj, t->size)) {
        return fail(c, "object outside the archive", obj);
    }
    int added = vset_add(c->visited, (uintptr_t) obj, t);
    if (added <= 0) {
        return added == 0 ? 0 : fail(c, "out of memory", obj);
    }
    /* Two passes: scalars first, then everything that follows pointers or
     * reads a sibling (array lengths, union discriminators). A count declared
     * after the array it sizes is then already native when it is read. */
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < t->nfields; i++) {
            const port_field* f = &t->fields[i];
            int scalar = f->kind == F_U8 || f->kind == F_U16 || f->kind == F_U32 || f->kind == F_U64 || f->kind == F_F32
                         || f->kind == F_F64 || f->kind == F_BITS || f->kind == F_OPAQUE;
            if (scalar != (pass == 0)) {
                continue;
            }
            const port_type* saved = c->cur_type;
            c->cur_type = t;
            int r = walk_field(c, f, obj);
            c->cur_type = saved;
            if (r != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int port_walk(port_walk_ctx* c, const port_type* t, void* obj)
{
    return walk_obj(c, t, obj);
}

static const port_field marked_slot_fields[] = { { .kind = F_PTR, .offset = 0, .type = NULL } };
static const port_type marked_slot_type = { "script pointer", 4, marked_slot_fields, 1 };

int port_walk_mark_slot(port_walk_ctx* c, const void* slot)
{
    return vset_add(c->visited, (uintptr_t) slot, &marked_slot_type) < 0 ? -1 : 0;
}

void port_walk_visited_foreach(const port_walk_ctx* c, void (*fn)(void*, const uint8_t*, const port_type*), void* user)
{
    const vset* s = c->visited;
    if (s == NULL) {
        return;
    }
    for (uint32_t i = 0; i < s->cap; i++) {
        if (s->slots[i].addr != 0) {
            fn(user, (const uint8_t*) s->slots[i].addr, s->slots[i].type);
        }
    }
}
