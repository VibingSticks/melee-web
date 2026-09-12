/* port_archive_swap_roots: root symbols are matched by prefix/suffix against a
 * hand-written port_roots[] (no generated tables) and their graphs converted. */
#include "check.h"
#include "hsd_endian/archive_swap.h"
#include "hsd_endian/walker.h"

#include <stdlib.h>

/* Leaf { u16 a; u16 b; f32 f; } ; Root { u32 n; Leaf* items; } */
static const port_field leaf_fields[] = { { F_U16, 0 }, { F_U16, 2 }, { F_F32, 4 } };
static const port_type Leaf_t = { "Leaf", 8, leaf_fields, 3 };
static const port_field root_fields[] = { { F_U32, 0 }, { F_PTR_ARRAY, 4, &Leaf_t, LEN_FIELD_U32, 0 } };
static const port_type Root_t = { "Root", 8, root_fields, 2 };
const port_root port_roots[] = {
    { "leaf", NULL, &Leaf_t },        /* prefix only */
    { NULL, "_root", &Root_t },       /* suffix only */
    { "x", "_root", &Leaf_t },        /* prefix+suffix: longer match, wins for "x_root" */
    { NULL, NULL, NULL },
};

static void be32(uint8_t* p, uint32_t v) { p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v; }
static void be16(uint8_t* p, uint16_t v) { p[0] = (uint8_t) (v >> 8); p[1] = (uint8_t) v; }

/* Build a big-endian archive: data = Root at 0 (8 bytes), Leaf[2] at 8..23, a lone Leaf "leafB" at 24..31;
 * one relocation (Root.items at offset 4); public symbols: "a_root" -> 0, "leafB" -> 24, "mystery" -> 24. */
static uint8_t* build(uint32_t* size_out)
{
    const char syms[] = "a_root\0leafB\0mystery\0";
    uint32_t data_size = 32, nb_reloc = 1, nb_public = 3, nb_extern = 0;
    uint32_t file_size = 0x20 + data_size + nb_reloc * 4 + nb_public * 8 + sizeof syms;
    uint8_t* f = calloc(1, file_size + 32);
    be32(f + 0, file_size); be32(f + 4, data_size); be32(f + 8, nb_reloc); be32(f + 12, nb_public); be32(f + 16, nb_extern);
    uint8_t* d = f + 0x20;
    be32(d + 0, 2); be32(d + 4, 8); /* Root.n = 2, Root.items -> data+8 */
    for (int i = 0; i < 2; i++) { be16(d + 8 + i * 8, 0x0102 + i); be16(d + 10 + i * 8, 0x0304); be32(d + 12 + i * 8, 0x3F800000u); }
    be16(d + 24, 0xAABB); be16(d + 26, 0xCCDD); be32(d + 28, 0x40000000u);
    uint8_t* r = d + data_size;
    be32(r, 4);
    uint8_t* pubs = r + 4;
    be32(pubs + 0, 0); be32(pubs + 4, 0);
    be32(pubs + 8, 24); be32(pubs + 12, 7);
    be32(pubs + 16, 24); be32(pubs + 20, 13);
    memcpy(pubs + 24, syms, sizeof syms);
    *size_out = file_size;
    return f;
}

static void run(void)
{
    uint32_t size;
    uint8_t* file = build(&size);
    port_archive_hdr hdr;
    uint32_t* rs = NULL;
    uint32_t rn = 0;
    CHECK_EQ_U32(port_archive_fixup(file, size, &hdr, &rs, &rn), 0);
    CHECK_EQ_U32(rn, 1);

    HSD_Archive ar;
    memset(&ar, 0, sizeof ar);
    ar.header.data_size = hdr.data_size;
    ar.header.nb_public = hdr.nb_public;
    ar.data = file + 0x20;
    ar.public_info = (HSD_ArchivePublicInfo*) (file + 0x20 + hdr.data_size + hdr.nb_reloc * 4);
    ar.symbols = (char*) (ar.public_info + hdr.nb_public);
    ar.name = "test.dat";

    CHECK_EQ_U32(port_archive_swap_roots(&ar, rs, rn), 0); /* "mystery" only logs in non-strict mode */
    uint8_t* d = ar.data;
    uint32_t n; memcpy(&n, d, 4); CHECK_EQ_U32(n, 2);
    uint16_t a; memcpy(&a, d + 8, 2); CHECK_EQ_U32(a, 0x0102);
    memcpy(&a, d + 16, 2); CHECK_EQ_U32(a, 0x0103);
    float fl; memcpy(&fl, d + 20, 4); CHECK(fl == 1.0f);
    memcpy(&a, d + 24, 2); CHECK_EQ_U32(a, 0xAABB); /* leafB via prefix match */
    memcpy(&fl, d + 28, 4); CHECK(fl == 2.0f);
    /* the relocated pointer slot was not touched by the walker */
    uint32_t items; memcpy(&items, d + 4, 4); CHECK_EQ_U32(items, (uint32_t) (uintptr_t) (d + 8));
    free(rs);
    free(file);
}

TEST_MAIN(run)
