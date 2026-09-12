#include "check.h"
#include "hsd_endian/formats.h"

static void be32(uint8_t* p, uint32_t v) { p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v; }
static void be16(uint8_t* p, uint16_t v) { p[0] = (uint8_t) (v >> 8); p[1] = (uint8_t) v; }

static void run(void)
{
    /* two groups: {n=2, x, 2 blocks}, {n=1, x, 1 block} = 8 + 128 + 8 + 64 = 208 bytes */
    uint8_t buf[208];
    uint8_t* p = buf;
    memset(buf, 0, sizeof buf);
    be32(p, 2); be32(p + 4, 0xAABBCCDD); p += 8;
    for (int k = 0; k < 3; k++) {
        if (k == 2) { be32(p, 1); be32(p + 4, 0x11223344); p += 8; }
        be32(p + 0x00, 0x01020304u + k); be32(p + 0x04, 0x0000BEEF); be32(p + 0x08, 2); be32(p + 0x0C, 0x8000);
        be16(p + 0x10, 1); be16(p + 0x12, 0);
        be32(p + 0x14, 0x00012345); be32(p + 0x18, 0x00067890); be32(p + 0x1C, 0x00000010);
        for (int c = 0; c < 16; c++) be16(p + 0x20 + c * 2, (uint16_t) (0x0100 * c + c));
        p += 0x40;
    }
    CHECK_EQ_U32(port_swap_ssm_table((uint32_t*) buf, 2), sizeof buf);
    uint32_t* w = (uint32_t*) buf;
    CHECK_EQ_U32(w[0], 2); CHECK_EQ_U32(w[1], 0xAABBCCDD);
    uint32_t* b0 = w + 2;
    CHECK_EQ_U32(b0[0], 0x01020304); CHECK_EQ_U32(b0[1], 0xBEEF); CHECK_EQ_U32(b0[2], 2); CHECK_EQ_U32(b0[3], 0x8000);
    CHECK_EQ_U32(((uint16_t*) b0)[8], 1); CHECK_EQ_U32(((uint16_t*) b0)[9], 0);
    CHECK_EQ_U32(b0[5], 0x12345); CHECK_EQ_U32(b0[6], 0x67890); CHECK_EQ_U32(b0[7], 0x10);
    CHECK_EQ_U32(((uint16_t*) b0)[16 + 3], 0x0303);
    uint32_t* b1 = b0 + 16;
    CHECK_EQ_U32(b1[0], 0x01020305);
    uint32_t* g1 = b1 + 16;
    CHECK_EQ_U32(g1[0], 1); CHECK_EQ_U32(g1[1], 0x11223344);
    CHECK_EQ_U32(g1[2], 0x01020306);
    CHECK_EQ_U32(((uint16_t*) (g1 + 2))[16 + 15], 0x0F0F);

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
