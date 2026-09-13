/* port_swap_it_cmd_scripts: item scripts (ItemStateDesc::xC_script) are the
 * same bitfield bytecode as fighter subactions with the itanimlist.c command
 * set at opcodes 10-25. The words are read back the way the item handlers
 * read them -- union CmdUnion, itanimlist.c's own itAnimlistCmdUnk, and the
 * two TARGET_PC reads -- so the width table in formats.c is checked against
 * clang's real layout of those structs. Also checks the variable length of
 * opcode 16, that a shared stream is converted once, and that every pointer
 * word left in place is reported. */
#include "check.h"
#include "hsd_endian/formats.h"

#include <melee/lb/types.h>

static uint8_t buf[512];

/* itanimlist.c (file-local there): the colour-animation command header. */
typedef struct {
    u16 x0_b0 : 6;
    u16 opcode : 8;
    u16 x0_b14 : 2;
    u16 x2;
} itAnimlistCmdUnk;

/* itanimlist.c under TARGET_PC: the effect command's first word. */
typedef struct {
    u32 opcode : 6;
    u32 arg : 10;
    u32 x0_b16 : 16;
} itAnimlistGfxCmd;

static uint32_t be_fields(int n, const int* wv)
{
    uint32_t v = 0;
    unsigned used = 0;
    for (int i = 0; i < n; i++) {
        v = (v << wv[2 * i]) | ((uint32_t) wv[2 * i + 1] & ((1u << wv[2 * i]) - 1));
        used += wv[2 * i];
    }
    return v << (32 - used);
}

static void put_be32(uint32_t at, uint32_t v)
{
    buf[at] = (uint8_t) (v >> 24);
    buf[at + 1] = (uint8_t) (v >> 16);
    buf[at + 2] = (uint8_t) (v >> 8);
    buf[at + 3] = (uint8_t) v;
}

static void put_ptr(uint32_t at, uint32_t target_off)
{
    uint32_t v = (uint32_t) (uintptr_t) buf + target_off;
    memcpy(buf + at, &v, 4);
}

#define U(off) ((const union CmdUnion*) (buf + (off)))
#define F(...) be_fields(sizeof((int[]){ __VA_ARGS__ }) / (2 * sizeof(int)), (const int[]){ __VA_ARGS__ })

enum { ROWS = 0, SCRIPT_A = 0x40, SUB = 0x100, TAIL = 0x140, SCRIPT_B = TAIL };

static const void* noted[8];
static unsigned nnoted;

static void note(void* user, const void* slot)
{
    CHECK(user == buf);
    if (nnoted < 8) {
        noted[nnoted] = slot;
    }
    nnoted++;
}

static void run(void)
{
    memset(buf, 0xEE, sizeof buf);

    /* Two ItemStateDesc rows; only xC_script matters here. */
    memset(buf + ROWS, 0, 32);
    put_ptr(ROWS + 12, SCRIPT_A);
    put_ptr(ROWS + 16 + 12, SCRIPT_B);

    uint32_t p = SCRIPT_A;
    /* 10: effect. arg 0x2A5 in the 10 bits after the opcode, junk in the low half */
    put_be32(p, F(6, 10, 10, 0x2A5, 16, 0xBEEF)); p += 4;
    put_be32(p, F(16, 0x0123, 16, 0x0007)); p += 4;               /* ef_id, arg6 */
    put_be32(p, F(16, 0xFFFE, 16, 0x0002)); p += 4;               /* sp20.x = -2, sp20.y = 2 */
    put_be32(p, F(16, 0x8000, 16, 0x7FFF)); p += 4;               /* sp20.z, sp14.x */
    put_be32(p, F(16, 0x0010, 16, 0x0020)); p += 4;               /* sp14.y, sp14.z */
    /* 11: hitbox, six words */
    put_be32(p, F(6, 11, 3, 2, 3, 5, 7, 0x21, 13, 0x155)); p += 4; /* it_create_hitbox_0 */
    put_be32(p, F(16, 0x0500, 16, 0xFFF0)); p += 4;               /* size, z_offset -16 */
    put_be32(p, F(16, 0x0123, 16, 0x8001)); p += 4;               /* y_offset, x_offset */
    put_be32(p, F(9, 361, 9, 100, 9, 50, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0)); p += 4; /* create_hitbox_3 */
    put_be32(p, F(9, 70, 5, 3, 1, 1, 8, 0xF9, 3, 2, 4, 9, 1, 1, 1, 0)); p += 4;  /* it_create_hitbox_4 */
    put_be32(p, 0xA5C3F00Fu); p += 4;                             /* read as bytes: left alone */
    const uint32_t bytes_word = p - 4;
    /* 12: set_hitbox_damage idx 3, value 0x7E1234 (only the low 13 bits matter) */
    put_be32(p, F(6, 12, 3, 3, 23, 0x7E1234)); p += 4;
    /* subroutine */
    put_be32(p, F(6, 5, 26, 0)); put_ptr(p + 4, SUB); p += 8;
    /* 16 with sub-opcode 1: three words (u16 bitfields 6/8/2, u16; u32; bytes) */
    put_be32(p, F(6, 16, 8, 1, 2, 2, 16, 0x4321)); p += 4;
    put_be32(p, 0x11223344u); p += 4;
    put_be32(p, 0x99887766u); p += 4;
    const uint32_t bytes_word2 = p - 4;
    /* 16 with sub-opcode 5: two words */
    put_be32(p, F(6, 16, 8, 5, 2, 0, 16, 0xABCD)); p += 4;
    put_be32(p, 0x0BADF00Du); p += 4;
    /* 21: unk33 */
    put_be32(p, F(6, 21, 13, 0x1ABC, 13, 0x0DEF)); p += 4;
    /* 24: unk13 */
    put_be32(p, F(6, 24, 8, 0x5A, 18, 0x2F0F0)); p += 4;
    /* goto TAIL, then junk that must stay untouched */
    put_be32(p, F(6, 7, 26, 0)); put_ptr(p + 4, TAIL); p += 8;
    const uint32_t junk = p;

    p = SUB;
    put_be32(p, F(6, 1, 26, 12)); p += 4;                         /* synchronous timer 12 */
    put_be32(p, F(6, 14, 26, 0x3ABCDEF)); p += 4;                 /* set_throw_flags.hit_idx */
    put_be32(p, F(6, 6, 26, 0)); p += 4;                          /* return */

    p = TAIL;
    put_be32(p, F(6, 5, 26, 0)); put_ptr(p + 4, SUB); p += 8;
    put_be32(p, F(6, 0, 26, 0)); p += 4;
    const uint32_t after_end = p;

    const void* slots[2] = { buf + ROWS + 12, buf + ROWS + 16 + 12 };
    port_swap_it_cmd_scripts(buf, slots, 2, note, buf);

    uint32_t w = SCRIPT_A;
    CHECK_EQ_U32(U(w)->unk0.opcode, 10);
    CHECK_EQ_U32(((const itAnimlistGfxCmd*) (buf + w))->arg, 0x2A5);
    CHECK_EQ_U32(((const u16*) (buf + w + 4))[0], 0x0123);       /* ef_id */
    CHECK_EQ_U32(((const u16*) (buf + w + 4))[1], 0x0007);       /* arg6 */
    CHECK(((const s16*) (buf + w + 8))[0] == -2);
    CHECK(((const s16*) (buf + w + 8))[1] == 2);
    CHECK(((const s16*) (buf + w + 12))[0] == -32768);
    CHECK(((const s16*) (buf + w + 12))[1] == 0x7FFF);
    CHECK(((const s16*) (buf + w + 16))[0] == 0x10);
    CHECK(((const s16*) (buf + w + 16))[1] == 0x20);
    w += 20;
    CHECK_EQ_U32(U(w)->it_create_hitbox_0.opcode, 11);
    CHECK_EQ_U32(U(w)->it_create_hitbox_0.id, 2);
    CHECK_EQ_U32(U(w)->it_create_hitbox_0.hit_group, 5);
    CHECK_EQ_U32(U(w)->it_create_hitbox_0.bone, 0x21);
    CHECK_EQ_U32(U(w)->it_create_hitbox_0.damage, 0x155);
    CHECK_EQ_U32(U(w + 4)->create_hitbox_1.size, 0x500);
    CHECK(U(w + 4)->create_hitbox_1.z_offset == -16);
    CHECK(U(w + 8)->create_hitbox_2.y_offset == 0x0123);
    CHECK(U(w + 8)->create_hitbox_2.x_offset == -32767);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.angle, 361);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.knockback_growth, 100);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.weight_set_knockback, 50);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.ignore_thrown_fighters, 1);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.clank, 1);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.rebound, 0);
    CHECK_EQ_U32(U(w + 16)->it_create_hitbox_4.base_knockback, 70);
    CHECK_EQ_U32(U(w + 16)->it_create_hitbox_4.element, 3);
    CHECK_EQ_U32(U(w + 16)->it_create_hitbox_4.x40_b0, 1);
    CHECK(U(w + 16)->it_create_hitbox_4.shield_damage == -7);
    CHECK_EQ_U32(U(w + 16)->it_create_hitbox_4.sfx_severity, 2);
    CHECK_EQ_U32(U(w + 16)->it_create_hitbox_4.sfx_kind, 9);
    CHECK_EQ_U32(U(w + 16)->it_create_hitbox_4.x40_b3, 1);
    CHECK_EQ_U32(U(w + 16)->it_create_hitbox_4.x40_b2, 0);
    CHECK_EQ_U32(buf[bytes_word], 0xA5);                          /* ((u8*) cmd->u)[0] */
    CHECK_EQ_U32(buf[bytes_word + 1], 0xC3);
    CHECK_EQ_U32(buf[bytes_word + 2], 0xF0);
    w += 24;
    CHECK_EQ_U32(U(w)->set_hitbox_damage.opcdoe, 12);
    CHECK_EQ_U32(U(w)->set_hitbox_damage.idx, 3);
    CHECK_EQ_U32(U(w)->set_hitbox_damage.value & 0x1FFF, 0x1234); /* the TARGET_PC read */
    w += 4;
    CHECK_EQ_U32(U(w)->Command_00.code, 5);
    CHECK_EQ_U32(*(const uint32_t*) (buf + w + 4), (uint32_t) (uintptr_t) buf + SUB);
    w += 8;
    CHECK_EQ_U32(((const itAnimlistCmdUnk*) (buf + w))->x0_b0, 16);
    CHECK_EQ_U32(((const itAnimlistCmdUnk*) (buf + w))->opcode, 1);
    CHECK_EQ_U32(((const itAnimlistCmdUnk*) (buf + w))->x0_b14, 2);
    CHECK_EQ_U32(((const itAnimlistCmdUnk*) (buf + w))->x2, 0x4321);
    CHECK_EQ_U32(*(const uint32_t*) (buf + w + 4), 0x11223344u);  /* arg1 = *(u32*) cmd->u */
    CHECK_EQ_U32(buf[bytes_word2 + 2], 0x77);                     /* arg2 = ((u8*) cmd->u)[2] */
    CHECK_EQ_U32(buf[bytes_word2 + 3], 0x66);
    w += 12;
    CHECK_EQ_U32(((const itAnimlistCmdUnk*) (buf + w))->opcode, 5);
    CHECK_EQ_U32(((const itAnimlistCmdUnk*) (buf + w))->x2, 0xABCD);
    CHECK_EQ_U32(*(const uint32_t*) (buf + w + 4), 0x0BADF00Du);  /* swapped, though it_8027978C skips it */
    w += 8;
    CHECK_EQ_U32(U(w)->unk33.opcode, 21);
    CHECK_EQ_U32(U(w)->unk33.unk0, 0x1ABC);
    CHECK_EQ_U32(U(w)->unk33.unk1, 0x0DEF);
    w += 4;
    CHECK_EQ_U32(U(w)->unk13.unk0, 24);
    CHECK_EQ_U32(U(w)->unk13.unk1, 0x5A);
    CHECK_EQ_U32(U(w)->unk13.unk2, 0x2F0F0);
    w += 4;
    CHECK_EQ_U32(U(w)->Command_00.code, 7);
    CHECK_EQ_U32(*(const uint32_t*) (buf + w + 4), (uint32_t) (uintptr_t) buf + TAIL);
    CHECK_EQ_U32(buf[junk], 0xEE);

    w = SUB;
    CHECK_EQ_U32(U(w)->Command_00.code, 1);
    CHECK_EQ_U32(U(w)->Command_00.value, 12);
    CHECK_EQ_U32(U(w + 4)->set_throw_flags.opcode, 14);
    CHECK_EQ_U32(U(w + 4)->set_throw_flags.hit_idx, 0x3ABCDEF);
    CHECK_EQ_U32(U(w + 8)->Command_00.code, 6);

    w = TAIL;
    CHECK_EQ_U32(U(w)->Command_00.code, 5);
    CHECK_EQ_U32(U(w + 8)->Command_00.code, 0);
    CHECK_EQ_U32(buf[after_end], 0xEE);

    /* Three pointer words were left in place: subroutine and goto in SCRIPT_A,
     * subroutine in TAIL (TAIL and SUB reached twice, converted once). */
    CHECK_EQ_U32(nnoted, 3);
    CHECK(noted[0] == buf + SCRIPT_A + 20 + 24 + 4 + 4);
    CHECK(noted[2] == buf + TAIL + 4);
    /* The rows themselves are untouched. */
    CHECK_EQ_U32(*(const uint32_t*) (buf + ROWS), 0);
}

TEST_MAIN(run)
