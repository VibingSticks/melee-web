#include "check.h"
#include "hsd_endian/formats.h"

static void be32(uint8_t* p, uint32_t v) { p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v; }
static void be16(uint8_t* p, uint16_t v) { p[0] = (uint8_t) (v >> 8); p[1] = (uint8_t) v; }

static void run(void)
{
    /* two groups: {n=2, x, 2 blocks}, {n=1, x, 1 block} = 8 + 128 + 8 + 64 = 208 bytes.
     * A block is AXPBADDR, AXPBADPCM, AXPBADPCMLOOP and a pad word, all u16. */
    uint8_t buf[208];
    uint8_t* p = buf;
    memset(buf, 0, sizeof buf);
    be32(p, 2); be32(p + 4, 0xAABBCCDD); p += 8;
    for (int k = 0; k < 3; k++) {
        if (k == 2) { be32(p, 1); be32(p + 4, 0x11223344); p += 8; }
        be16(p + 0x00, 1); be16(p + 0x02, 0x0A);                                  /* loopFlag, format */
        be16(p + 0x04, 0x0001); be16(p + 0x06, 0x2345);                           /* loop hi, lo */
        be16(p + 0x08, 0x0006); be16(p + 0x0A, 0x7890);                           /* end hi, lo */
        be16(p + 0x0C, 0x0000); be16(p + 0x0E, (uint16_t) (0x0010 + k));         /* current hi, lo */
        for (int c = 0; c < 16; c++) be16(p + 0x10 + c * 2, (uint16_t) (0x0100 * c + c));
        be16(p + 0x30, 0x0800); be16(p + 0x32, 0x0027); be16(p + 0x34, 0xFF01); be16(p + 0x36, 0xFE02);
        be16(p + 0x38, 0x0035); be16(p + 0x3A, 0x1234); be16(p + 0x3C, 0x5678);
        p += 0x40;
    }
    CHECK_EQ_U32(port_swap_ssm_table((uint32_t*) buf, 2), sizeof buf);
    uint32_t* w = (uint32_t*) buf;
    CHECK_EQ_U32(w[0], 2); CHECK_EQ_U32(w[1], 0xAABBCCDD);
    uint16_t* b0 = (uint16_t*) (w + 2);
    CHECK_EQ_U32(b0[0], 1); CHECK_EQ_U32(b0[1], 0x0A);
    CHECK_EQ_U32(b0[2], 0x0001); CHECK_EQ_U32(b0[3], 0x2345);
    CHECK_EQ_U32(b0[4], 0x0006); CHECK_EQ_U32(b0[5], 0x7890);
    CHECK_EQ_U32(b0[6], 0); CHECK_EQ_U32(b0[7], 0x10);
    CHECK_EQ_U32(b0[8], 0); CHECK_EQ_U32(b0[8 + 3], 0x0303); CHECK_EQ_U32(b0[8 + 15], 0x0F0F);
    CHECK_EQ_U32(b0[24], 0x0800); CHECK_EQ_U32(b0[25], 0x0027); CHECK_EQ_U32(b0[26], 0xFF01); CHECK_EQ_U32(b0[27], 0xFE02);
    CHECK_EQ_U32(b0[28], 0x0035); CHECK_EQ_U32(b0[29], 0x1234); CHECK_EQ_U32(b0[30], 0x5678); CHECK_EQ_U32(b0[31], 0);
    uint16_t* b1 = b0 + 32;
    CHECK_EQ_U32(b1[7], 0x11);
    uint32_t* g1 = (uint32_t*) (b1 + 32);
    CHECK_EQ_U32(g1[0], 1); CHECK_EQ_U32(g1[1], 0x11223344);
    CHECK_EQ_U32(((uint16_t*) (g1 + 2))[7], 0x12);
    CHECK_EQ_U32(((uint16_t*) (g1 + 2))[8 + 15], 0x0F0F);

    /* .hps headers */
    uint8_t hps[0x80];
    memset(hps, 0, sizeof hps);
    memcpy(hps, "HALPST\0\0", 8);
    be32(hps + 8, 32000); be32(hps + 12, 2);
    be16(hps + 0x10, 1); be16(hps + 0x12, 0);                 /* channel 0 AXPBADDR: loopFlag, format */
    be16(hps + 0x14, 0x0002); be16(hps + 0x16, 0x0004);       /* loop hi, lo */
    be16(hps + 0x20, 0x0123);                                 /* first ADPCM coefficient */
    be16(hps + 0x48, 0x0B0B);                                 /* channel 1 AXPBADDR: loopFlag */
    be16(hps + 0x7E, 0x7E7E);                                 /* last word */
    port_swap_hps_file_header((uint32_t*) hps);
    CHECK_EQ_U32(memcmp(hps, "HALPST\0\0", 8), 0);
    CHECK_EQ_U32(((uint32_t*) hps)[2], 32000); CHECK_EQ_U32(((uint32_t*) hps)[3], 2);
    CHECK_EQ_U32(((uint16_t*) hps)[8], 1); CHECK_EQ_U32(((uint16_t*) hps)[9], 0);
    CHECK_EQ_U32(((uint16_t*) hps)[10], 2); CHECK_EQ_U32(((uint16_t*) hps)[11], 4);
    CHECK_EQ_U32(((uint16_t*) hps)[16], 0x0123);
    CHECK_EQ_U32(((uint16_t*) hps)[0x24], 0x0B0B);
    CHECK_EQ_U32(((uint16_t*) hps)[0x3F], 0x7E7E);

    uint8_t blk[0x20];
    memset(blk, 0, sizeof blk);
    be32(blk, 0x10000); be32(blk + 4, 0x1FFF0); be32(blk + 8, 0xFFFFFFFFu);
    be16(blk + 0x0C, 0x0017); be16(blk + 0x0E, 0x0102); be16(blk + 0x10, 0x0304);
    be16(blk + 0x14, 0x0027); be16(blk + 0x1A, 0x0FF0);
    port_swap_hps_block_header((uint32_t*) blk);
    CHECK_EQ_U32(((uint32_t*) blk)[0], 0x10000); CHECK_EQ_U32(((uint32_t*) blk)[1], 0x1FFF0); CHECK_EQ_U32(((uint32_t*) blk)[2], 0xFFFFFFFFu);
    CHECK_EQ_U32(((uint16_t*) blk)[6], 0x17); CHECK_EQ_U32(((uint16_t*) blk)[7], 0x0102); CHECK_EQ_U32(((uint16_t*) blk)[8], 0x0304);
    CHECK_EQ_U32(((uint16_t*) blk)[10], 0x27); CHECK_EQ_U32(((uint16_t*) blk)[13], 0x0FF0);
    CHECK_EQ_U32(((uint16_t*) blk)[14], 0); CHECK_EQ_U32(((uint16_t*) blk)[15], 0);

    /* .sem header: {2: a, b} {1: c} {0} {3: d, e, f} then bytecode that must stay put */
    uint8_t sem[64];
    memset(sem, 0x5A, sizeof sem);
    uint8_t* q = sem;
    be32(q, 2); be32(q + 4, 0x0A); be32(q + 8, 0x0B); q += 12;
    be32(q, 1); be32(q + 4, 0x0C); q += 8;
    be32(q, 0); q += 4;
    be32(q, 3); be32(q + 4, 0x0D); be32(q + 8, 0x0E); be32(q + 12, 0x0F); q += 16;
    CHECK_EQ_U32(port_swap_sem_header((uint32_t*) sem), 40);
    uint32_t* sw = (uint32_t*) sem;
    CHECK_EQ_U32(sw[0], 2); CHECK_EQ_U32(sw[2], 0x0B); CHECK_EQ_U32(sw[3], 1); CHECK_EQ_U32(sw[4], 0x0C);
    CHECK_EQ_U32(sw[5], 0); CHECK_EQ_U32(sw[6], 3); CHECK_EQ_U32(sw[9], 0x0F);
    CHECK_EQ_U32(sem[40], 0x5A); CHECK_EQ_U32(sem[63], 0x5A);

    uint32_t arr[2]; be32((uint8_t*) arr, 0xDEADBEEF); be32((uint8_t*) (arr + 1), 0x00000001);
    port_swap_u32_array(arr, 2);
    CHECK_EQ_U32(arr[0], 0xDEADBEEF); CHECK_EQ_U32(arr[1], 1);
}

TEST_MAIN(run)
