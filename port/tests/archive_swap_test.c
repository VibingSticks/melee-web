#include "check.h"
#include "hsd_endian/archive_swap.h"

#include <stdlib.h>

static void be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

/* data (24 bytes): [0] ptr -> data+8 ; [4] u32 0x11223344 ; [8] ptr -> data+16 ; [12] u16 pair ; [16..23] leaf
 * reloc: offsets 8, 0 (deliberately unsorted)
 * public: "root" -> data+0
 * extern: "ext_sym" -> chain head at data+20 (holds BE 0xFFFFFFFF = end) */
static uint8_t file[0x20 + 24 + 8 + 8 + 8 + 16];

static void build(void)
{
    memset(file, 0, sizeof file);
    uint32_t data_size = 24, nb_reloc = 2, nb_public = 1, nb_extern = 1;
    be32(file + 0, sizeof file);
    be32(file + 4, data_size);
    be32(file + 8, nb_reloc);
    be32(file + 12, nb_public);
    be32(file + 16, nb_extern);
    memcpy(file + 20, "001B", 4);
    uint8_t* data = file + 0x20;
    be32(data + 0, 8);
    be32(data + 4, 0x11223344u);
    be32(data + 8, 16);
    data[12] = 0xAB; data[13] = 0xCD; data[14] = 0x12; data[15] = 0x34;
    be32(data + 16, 0x01020304u);
    be32(data + 20, 0xFFFFFFFFu);
    uint8_t* reloc = data + data_size;
    be32(reloc + 0, 8);
    be32(reloc + 4, 0);
    uint8_t* pub = reloc + 8;
    be32(pub + 0, 0);
    be32(pub + 4, 0);
    uint8_t* ext = pub + 8;
    be32(ext + 0, 20);
    be32(ext + 4, 5);
    memcpy(ext + 8, "root\0ext_sym\0", 13);
}

static void run(void)
{
    build();
    port_archive_hdr h;
    uint32_t* rs;
    uint32_t rn;
    CHECK_EQ_U32(port_archive_fixup(file, sizeof file, &h, &rs, &rn), 0);
    CHECK_EQ_U32(h.file_size, sizeof file);
    CHECK_EQ_U32(h.data_size, 24);
    CHECK_EQ_U32(h.nb_reloc, 2);
    CHECK_EQ_U32(h.nb_public, 1);
    CHECK_EQ_U32(h.nb_extern, 1);
    CHECK_EQ_U32(rn, 2);
    CHECK_EQ_U32(rs[0], 0); /* sorted */
    CHECK_EQ_U32(rs[1], 8);

    uint8_t* data = file + 0x20;
    void* p0;
    void* p8;
    memcpy(&p0, data + 0, sizeof p0 == 4 ? 4 : 4);
    memcpy(&p8, data + 8, 4);
    uint32_t v0, v8, v4, v16;
    memcpy(&v0, data, 4);
    memcpy(&v8, data + 8, 4);
    CHECK(v0 == (uint32_t) (uintptr_t) (data + 8));  /* relocated */
    CHECK(v8 == (uint32_t) (uintptr_t) (data + 16));
    memcpy(&v4, data + 4, 4);
    CHECK_EQ_U32(v4, 0x44332211u); /* scalar untouched: still big-endian bytes */
    memcpy(&v16, data + 16, 4);
    CHECK_EQ_U32(v16, 0x04030201u);
    CHECK_EQ_U32(data[12], 0xAB);   /* u16 pair untouched */

    /* tables are native now */
    uint32_t reloc0, pub_off, pub_sym, ext_off, ext_sym;
    memcpy(&reloc0, data + 24, 4);
    memcpy(&pub_off, data + 24 + 8, 4);
    memcpy(&pub_sym, data + 24 + 12, 4);
    memcpy(&ext_off, data + 24 + 16, 4);
    memcpy(&ext_sym, data + 24 + 20, 4);
    CHECK_EQ_U32(reloc0, 8);
    CHECK_EQ_U32(pub_off, 0);
    CHECK_EQ_U32(pub_sym, 0);
    CHECK_EQ_U32(ext_off, 20);
    CHECK_EQ_U32(ext_sym, 5);
    CHECK(memcmp(file + 20, "001B", 4) == 0); /* version bytes untouched */
    CHECK_EQ_U32(port_archive_read_be32(data + 20), 0xFFFFFFFFu);
    free(rs);

    /* a second call sees a native header and only rebuilds the reloc set */
    CHECK_EQ_U32(port_archive_fixup(file, sizeof file, &h, &rs, &rn), 1);
    CHECK_EQ_U32(rn, 2);
    CHECK_EQ_U32(rs[1], 8);
    memcpy(&v0, data, 4);
    CHECK(v0 == (uint32_t) (uintptr_t) (data + 8)); /* not relocated twice */
    free(rs);

    /* malformed inputs */
    build();
    be32(file + 0, 1234);
    CHECK_EQ_U32(port_archive_fixup(file, sizeof file, &h, &rs, &rn), (uint32_t) -1); /* size mismatch */
    build();
    be32(file + 0x20 + 24, 100); /* reloc offset past the data section */
    CHECK_EQ_U32(port_archive_fixup(file, sizeof file, &h, &rs, &rn), (uint32_t) -1);
    build();
    be32(file + 8, 100000); /* absurd reloc count */
    CHECK_EQ_U32(port_archive_fixup(file, sizeof file, &h, &rs, &rn), (uint32_t) -1);
    CHECK_EQ_U32(port_archive_fixup(file, 8, &h, &rs, &rn), (uint32_t) -1);
}

TEST_MAIN(run)
