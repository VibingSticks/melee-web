#include "walker.h"

#include <stdlib.h>
#include <string.h>

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
    return c->visited != NULL ? 0 : -1;
}

void port_walk_ctx_free(port_walk_ctx* c)
{
    vset* s = c->visited;
    if (s != NULL) {
        free(s->slots);
        free(s);
    }
    c->visited = NULL;
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
    memcpy(&v, slot, 4);
    if (v == 0) {
        *out = NULL;
        return 0;
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
    case LEN_NULL_TERM: return 0xFFFFFFFFu;
    }
    return 0;
}

static int fail(port_walk_ctx* c, const char* what)
{
    if (c->error == NULL) {
        c->error = what;
    }
    return c->strict ? -1 : 0;
}

static int walk_obj(port_walk_ctx* c, const port_type* t, uint8_t* obj);

static int walk_field(port_walk_ctx* c, const port_field* f, uint8_t* obj)
{
    uint8_t* p = obj + f->offset;
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
            return fail(c, "scalar outside the archive");
        }
        if (in_reloc(c, (uint32_t) (p - c->base))) {
            return fail(c, "scalar descriptor on a pointer slot");
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
    case F_STRUCT:
        return walk_obj(c, f->type, p);
    case F_ARRAY: {
        uint32_t n = read_len(f, obj);
        if (f->len_kind == LEN_NULL_TERM) {
            n = 0; /* inline arrays cannot be null-terminated */
        }
        for (uint32_t i = 0; i < n; i++) {
            if (walk_obj(c, f->type, p + i * f->type->size) != 0) {
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
        if (r < 0) {
            return fail(c, "pointer outside the archive");
        }
        return walk_obj(c, f->type, q);
    }
    case F_PTR_ARRAY: {
        uint8_t* q;
        int r = resolve_ptr(c, p, 0, &q);
        if (r == 0) {
            return 0;
        }
        if (r < 0) {
            return fail(c, "array pointer outside the archive");
        }
        uint32_t n = read_len(f, obj);
        for (uint32_t i = 0; i < n; i++) {
            uint8_t* e = q + i * f->type->size;
            if (!inside(c, e, f->type->size)) {
                return fail(c, "array element outside the archive");
            }
            if (f->len_kind == LEN_NULL_TERM) {
                uint32_t first;
                memcpy(&first, e, 4);
                if (first == 0) {
                    break;
                }
            }
            if (walk_obj(c, f->type, e) != 0) {
                return -1;
            }
        }
        return 0;
    }
    case F_UNION: {
        uint32_t d;
        memcpy(&d, obj + f->disc_offset, 4); /* the generator orders the discriminator first */
        for (uint8_t i = 0; i < f->ncases; i++) {
            if (f->disc_values[i] == d) {
                return f->disc_types[i] != NULL ? walk_obj(c, f->disc_types[i], p) : 0;
            }
        }
        return fail(c, "union discriminator has no case");
    }
    }
    return fail(c, "unknown field kind");
}

static int walk_obj(port_walk_ctx* c, const port_type* t, uint8_t* obj)
{
    if (t == NULL) {
        return 0;
    }
    int added = vset_add(c->visited, (uintptr_t) obj, t);
    if (added <= 0) {
        return added == 0 ? 0 : fail(c, "out of memory");
    }
    for (uint32_t i = 0; i < t->nfields; i++) {
        if (walk_field(c, &t->fields[i], obj) != 0) {
            return -1;
        }
    }
    return 0;
}

int port_walk(port_walk_ctx* c, const port_type* t, void* obj)
{
    return walk_obj(c, t, obj);
}
