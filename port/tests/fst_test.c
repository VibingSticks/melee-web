#include "check.h"
#include "dvd_web/fst.h"

static void be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

/* Layout:
 *  0 root      dir  next=5
 *  1 "audio"   dir  parent=0 next=3
 *  2 "smash2.sem"  file @0x1000 len 0x40    (inside audio)
 *  3 "PlMr.dat"    file @0x2000 len 0x100   (root)
 *  4 "IfAll.usd"   file @0x3000 len 0x200   (root)
 */
static uint8_t fst[12 * 5 + 48];

static void build(void)
{
    const char* names = "\0audio\0smash2.sem\0PlMr.dat\0IfAll.usd\0";
    be32(fst + 0, 0x01000000u); be32(fst + 4, 0); be32(fst + 8, 5);
    be32(fst + 12, 0x01000001u); be32(fst + 16, 0); be32(fst + 20, 3);
    be32(fst + 24, 0x00000007u); be32(fst + 28, 0x1000); be32(fst + 32, 0x40);
    be32(fst + 36, 0x00000012u); be32(fst + 40, 0x2000); be32(fst + 44, 0x100);
    be32(fst + 48, 0x0000001Bu); be32(fst + 52, 0x3000); be32(fst + 56, 0x200);
    memcpy(fst + 60, names, 37);
}

static void run(void)
{
    build();
    port_fst* f = port_fst_parse(fst, sizeof fst);
    CHECK(f != NULL);
    CHECK_EQ_U32(port_fst_entry_count(f), 5);

    CHECK_EQ_U32(port_fst_lookup(f, "audio/smash2.sem"), 2);
    CHECK_EQ_U32(port_fst_lookup(f, "AUDIO/SMASH2.SEM"), 2);
    CHECK_EQ_U32(port_fst_lookup(f, "/audio/smash2.sem"), 2);
    CHECK_EQ_U32(port_fst_lookup(f, "PlMr.dat"), 3);
    CHECK_EQ_U32(port_fst_lookup(f, "plmr.dat"), 3);
    CHECK_EQ_U32(port_fst_lookup(f, "IfAll.usd"), 4);
    CHECK_EQ_U32(port_fst_lookup(f, "audio"), 1);
    CHECK_EQ_U32(port_fst_lookup(f, "smash2.sem"), (uint32_t) -1);       /* not at root */
    CHECK_EQ_U32(port_fst_lookup(f, "audio/PlMr.dat"), (uint32_t) -1);   /* not in audio */
    CHECK_EQ_U32(port_fst_lookup(f, "PlMr.dat/x"), (uint32_t) -1);       /* file is not a dir */
    CHECK_EQ_U32(port_fst_lookup(f, "PlMr.da"), (uint32_t) -1);          /* prefix must not match */
    CHECK_EQ_U32(port_fst_lookup(f, ""), (uint32_t) -1);

    CHECK_EQ_U32(port_fst_file_offset(f, 2), 0x1000);
    CHECK_EQ_U32(port_fst_file_length(f, 2), 0x40);
    CHECK_EQ_U32(port_fst_file_offset(f, 4), 0x3000);
    CHECK(port_fst_is_dir(f, 1));
    CHECK(!port_fst_is_dir(f, 2));
    CHECK(!port_fst_is_dir(f, 99));
    CHECK(strcmp(port_fst_entry_name(f, 3), "PlMr.dat") == 0);
    port_fst_free(f);

    /* malformed tables */
    CHECK(port_fst_parse(fst, 8) == NULL);
    uint8_t bad[24] = {0};
    be32(bad + 8, 1000); /* count larger than the buffer */
    CHECK(port_fst_parse(bad, sizeof bad) == NULL);

    /* disc header */
    uint8_t hdr[0x440] = {0};
    memcpy(hdr, "GALE01", 6);
    be32(hdr + 0x424, 0x456E00);
    be32(hdr + 0x428, 0x7A00);
    port_disc_header h;
    CHECK_EQ_U32(port_disc_parse_header(hdr, sizeof hdr, &h), 0);
    CHECK(strcmp(h.game_id, "GALE01") == 0);
    CHECK_EQ_U32(h.fst_offset, 0x456E00);
    CHECK_EQ_U32(h.fst_size, 0x7A00);
    memcpy(hdr, "GALP01", 6);
    CHECK_EQ_U32(port_disc_parse_header(hdr, sizeof hdr, &h), (uint32_t) -1);
    memcpy(hdr, "GALE01", 6);
    CHECK_EQ_U32(port_disc_parse_header(hdr, 0x100, &h), (uint32_t) -1);
}

TEST_MAIN(run)
