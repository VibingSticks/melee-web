#include "fst.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

struct port_fst {
    const uint8_t* bytes;
    uint32_t size;
    uint32_t count;
    const char* strings;
    uint32_t strings_size;
};

static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static const uint8_t* ent(const port_fst* f, int32_t i)
{
    return f->bytes + 12u * (uint32_t) i;
}

int port_disc_parse_header(const uint8_t* s, size_t n, port_disc_header* out)
{
    if (n < 0x440 || memcmp(s, "GALE01", 6) != 0) {
        return -1;
    }
    memcpy(out->game_id, s, 6);
    out->game_id[6] = 0;
    out->fst_offset = rd32(s + 0x424);
    out->fst_size = rd32(s + 0x428);
    return 0;
}

port_fst* port_fst_parse(const uint8_t* b, uint32_t size)
{
    if (b == NULL || size < 12) {
        return NULL;
    }
    uint32_t count = rd32(b + 8);
    if (count == 0 || 12u * count > size) {
        return NULL;
    }
    port_fst* f = calloc(1, sizeof *f);
    if (f == NULL) {
        return NULL;
    }
    f->bytes = b;
    f->size = size;
    f->count = count;
    f->strings = (const char*) (b + 12u * count);
    f->strings_size = size - 12u * count;
    return f;
}

void port_fst_free(port_fst* f)
{
    free(f);
}

int32_t port_fst_entry_count(const port_fst* f) { return (int32_t) f->count; }

static int valid(const port_fst* f, int32_t i) { return i >= 0 && (uint32_t) i < f->count; }

int port_fst_is_dir(const port_fst* f, int32_t i) { return valid(f, i) && ent(f, i)[0] != 0; }
uint32_t port_fst_file_offset(const port_fst* f, int32_t i) { return valid(f, i) ? rd32(ent(f, i) + 4) : 0; }
uint32_t port_fst_file_length(const port_fst* f, int32_t i) { return valid(f, i) ? rd32(ent(f, i) + 8) : 0; }

const char* port_fst_entry_name(const port_fst* f, int32_t i)
{
    if (!valid(f, i)) {
        return NULL;
    }
    uint32_t off = rd32(ent(f, i)) & 0x00FFFFFFu;
    return off < f->strings_size ? f->strings + off : "";
}

/* True when the first n chars of a equal b (case-insensitively) and b ends there. */
static int name_eq(const char* a, const char* b, size_t n)
{
    for (size_t k = 0; k < n; k++) {
        if (b[k] == 0 || tolower((unsigned char) a[k]) != tolower((unsigned char) b[k])) {
            return 0;
        }
    }
    return b[n] == 0;
}

int32_t port_fst_lookup(const port_fst* f, const char* path)
{
    int32_t dir = 0;
    while (*path == '/') {
        path++;
    }
    while (*path != 0) {
        const char* seg_end = strchr(path, '/');
        size_t n = seg_end != NULL ? (size_t) (seg_end - path) : strlen(path);
        int32_t end = (int32_t) rd32(ent(f, dir) + 8); /* first entry after this directory */
        if (end > (int32_t) f->count) {
            end = (int32_t) f->count;
        }
        int32_t found = -1;
        for (int32_t i = dir + 1; i < end;) {
            if (name_eq(path, port_fst_entry_name(f, i), n)) {
                found = i;
                break;
            }
            i = port_fst_is_dir(f, i) ? (int32_t) rd32(ent(f, i) + 8) : i + 1;
            if (i <= dir) {
                return -1; /* malformed table */
            }
        }
        if (found < 0) {
            return -1;
        }
        if (seg_end == NULL) {
            return found;
        }
        if (!port_fst_is_dir(f, found)) {
            return -1;
        }
        dir = found;
        path = seg_end + 1;
        while (*path == '/') {
            path++;
        }
    }
    return -1;
}
