/* port_swap_ft_cmd_scripts: fighter subaction bytecode is repacked from the
 * MSB-first bitfields CodeWarrior wrote to the LSB-first ones this build
 * reads, following subroutines and gotos and converting shared streams once.
 * The words are read back through the game's own union CmdUnion, so the width
 * table in formats.c is checked against the struct declarations the
 * interpreter uses. */
#include "check.h"
#include "hsd_endian/formats.h"

#include <melee/lb/types.h>

static uint8_t buf[512];

/* Writes a big-endian word built from MSB-first fields: (width, value) pairs. */
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

/* A relocated pointer slot: the value port_archive_fixup leaves behind. */
static void put_ptr(uint32_t at, uint32_t target_off)
{
    uint32_t v = (uint32_t) (uintptr_t) buf + target_off;
    memcpy(buf + at, &v, 4);
}

/* union CmdUnion holds a pointer member, so on a 64-bit host it is 8 bytes and
 * cannot be stepped through; words are addressed by byte offset instead. */
#define U(off) ((const union CmdUnion*) (buf + (off)))

#define F(...) be_fields(sizeof((int[]){ __VA_ARGS__ }) / (2 * sizeof(int)), (const int[]){ __VA_ARGS__ })

enum { TABLE = 0, SCRIPT_A = 0x40, SUB = 0x100, TAIL = 0x140, SCRIPT_B = TAIL };

static void run(void)
{
    memset(buf, 0xEE, sizeof buf); /* bytes nothing should touch stay 0xEE */

    /* Two Fighter_WaitAnimData entries, only ::xC matters here. */
    memset(buf + TABLE, 0, 48);
    put_ptr(TABLE + 12, SCRIPT_A);
    put_ptr(TABLE + 24 + 12, SCRIPT_B);

    /* SCRIPT_A: set_hurt_state, subroutine SUB, spawn_hitbox, unk_fx (sound), goto TAIL, junk */
    uint32_t p = SCRIPT_A;
    put_be32(p, F(6, 28, 8, 3, 18, 1)); p += 4;                 /* set_hurt_state bone 3 state 1 */
    put_be32(p, F(6, 5, 26, 0)); put_ptr(p + 4, SUB); p += 8;   /* subroutine */
    put_be32(p, F(6, 11, 3, 2, 3, 5, 1, 0, 8, 0x21, 1, 1, 10, 0x155)); p += 4; /* spawn_hitbox_0 */
    put_be32(p, F(16, 0x0500, 16, 0xFFF0)); p += 4;             /* size 0x500, z_offset -16 */
    put_be32(p, F(16, 0x0123, 16, 0x8001)); p += 4;             /* y_offset, x_offset (s16) */
    put_be32(p, F(9, 361, 9, 100, 9, 50, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0)); p += 4; /* spawn_hitbox_3 */
    put_be32(p, F(9, 70, 5, 3, 8, 0xF9, 3, 2, 5, 9, 1, 1, 1, 0)); p += 4;         /* spawn_hitbox_4 */
    put_be32(p, F(6, 55, 8, 5, 1, 0, 1, 1, 16, 0x1234)); p += 4; /* landing fx: behavior 5, flag bit 15 set, gfx id 0x1234 */
    put_be32(p, 0xAABBCCDD); p += 4;                            /* sound_effect_1.sfx_id */
    put_be32(p, F(16, 0, 8, 0x7F, 8, 0x40)); p += 4;            /* sound_effect_2 volume, panning */
    put_be32(p, F(6, 7, 26, 0)); put_ptr(p + 4, TAIL); p += 8;  /* goto TAIL */
    const uint32_t junk = p;                                    /* unreachable: must stay 0xEE */

    /* SUB: synchronous timer 12, spawn_gfx with negative offsets, return */
    p = SUB;
    put_be32(p, F(6, 1, 26, 12)); p += 4;
    put_be32(p, F(6, 10, 8, 0x42, 1, 1, 1, 0, 1, 1, 15, 0)); p += 4; /* spawn_gfx_0 */
    put_be32(p, F(16, 0x0300, 16, 0x3F80)); p += 4;                  /* gfxID, unkFloat */
    put_be32(p, F(16, 0xFFFE, 16, 0x0002)); p += 4;                  /* offsetZ -2, offsetY 2 */
    put_be32(p, F(16, 0x8000, 16, 0x7FFF)); p += 4;                  /* offsetX, rangeZ */
    put_be32(p, F(16, 0x0010, 16, 0x0020)); p += 4;                  /* rangeY, rangeX */
    put_be32(p, F(6, 6, 26, 0)); p += 4;                             /* return */

    /* TAIL (also SCRIPT_B): subroutine SUB again, end */
    p = TAIL;
    put_be32(p, F(6, 5, 26, 0)); put_ptr(p + 4, SUB); p += 8;
    put_be32(p, F(6, 0, 26, 0)); p += 4;
    const uint32_t after_end = p;

    port_swap_ft_cmd_scripts(buf, buf + TABLE, 2, NULL, 0);

    /* Read back exactly as ftaction.c / lbcommand.c do. */
    uint32_t w = SCRIPT_A;
    CHECK_EQ_U32(U(w)->set_hurt_state.opcode, 28);
    CHECK_EQ_U32(U(w)->set_hurt_state.bone_idx, 3);
    CHECK_EQ_U32(U(w)->set_hurt_state.state, 1);
    w += 4;
    CHECK_EQ_U32(U(w)->Command_00.code, 5);
    CHECK_EQ_U32(*(const uint32_t*) (buf + w + 4), (uint32_t) (uintptr_t) buf + SUB); /* the slot is left alone */
    w += 8;
    CHECK_EQ_U32(U(w)->create_hitbox_0.opcode, 11);
    CHECK_EQ_U32(U(w)->create_hitbox_0.id, 2);
    CHECK_EQ_U32(U(w)->create_hitbox_0.hit_group, 5);
    CHECK_EQ_U32(U(w)->create_hitbox_0.only_hit_grabbed, 0);
    CHECK_EQ_U32(U(w)->create_hitbox_0.bone, 0x21);
    CHECK_EQ_U32(U(w)->create_hitbox_0.use_common_bone_ids, 1);
    CHECK_EQ_U32(U(w)->create_hitbox_0.damage, 0x155);
    CHECK_EQ_U32(U(w + 4)->create_hitbox_1.size, 0x500);
    CHECK(U(w + 4)->create_hitbox_1.z_offset == -16);
    CHECK(U(w + 8)->create_hitbox_2.y_offset == 0x0123);
    CHECK(U(w + 8)->create_hitbox_2.x_offset == -32767);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.angle, 361);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.knockback_growth, 100);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.weight_set_knockback, 50);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.item_hit_interaction, 0);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.ignore_thrown_fighters, 1);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.ignore_fighter_scale, 0);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.clank, 1);
    CHECK_EQ_U32(U(w + 12)->create_hitbox_3.rebound, 0);
    CHECK_EQ_U32(((const struct spawn_hitbox_skip*) (buf + w))->xF_b4, 1); /* = ignore_thrown_fighters */
    CHECK_EQ_U32(((const struct spawn_hitbox_skip*) (buf + w))->xF_b3, 0);
    CHECK_EQ_U32(U(w + 16)->create_hitbox_4.base_knockback, 70);
    CHECK_EQ_U32(U(w + 16)->create_hitbox_4.element, 3);
    CHECK(U(w + 16)->create_hitbox_4.shield_damage == -7);
    CHECK_EQ_U32(U(w + 16)->create_hitbox_4.hit_sfx_severity, 2);
    CHECK_EQ_U32(U(w + 16)->create_hitbox_4.hit_sfx_kind, 9);
    CHECK_EQ_U32(U(w + 16)->create_hitbox_4.hit_grounded, 1);
    CHECK_EQ_U32(U(w + 16)->create_hitbox_4.hit_aerial, 0);
    w += 20;
    CHECK_EQ_U32(U(w)->unk_fx_0.opcode, 55);
    CHECK_EQ_U32(U(w)->sound_effect_0.behavior, 5);       /* ftAction_80071B50's read of the same word */
    CHECK_EQ_U32(U(w)->footstep_fx_0.use_alt_bone, 0);    /* bit 14 */
    CHECK_EQ_U32(U(w)->footstep_fx_0.x1_b7, 1);           /* bit 15: ftAction_80072E4C's flag under TARGET_PC */
    CHECK_EQ_U32(((const u16*) (buf + w))[1], 0x1234);    /* ftAction_80072E4C's gfx id read */
    CHECK_EQ_U32(U(w + 4)->sound_effect_1.sfx_id, 0xAABBCCDD);
    CHECK_EQ_U32(U(w + 8)->sound_effect_2.volume, 0x7F);
    CHECK_EQ_U32(U(w + 8)->sound_effect_2.panning, 0x40);
    w += 12;
    CHECK_EQ_U32(U(w)->Command_00.code, 7);
    CHECK_EQ_U32(*(const uint32_t*) (buf + w + 4), (uint32_t) (uintptr_t) buf + TAIL);
    CHECK_EQ_U32(buf[junk], 0xEE); /* nothing after the goto was walked */

    w = SUB;
    CHECK_EQ_U32(U(w)->Command_00.code, 1);
    CHECK_EQ_U32(U(w)->Command_00.value, 12);
    w += 4;
    CHECK_EQ_U32(U(w)->spawn_gfx_0.opcode, 10);
    CHECK_EQ_U32(U(w)->spawn_gfx_0.boneId, 0x42);
    CHECK_EQ_U32(U(w)->spawn_gfx_0.useCommonBoneIDs, 1);
    CHECK_EQ_U32(U(w)->spawn_gfx_0.destroyOnStateChange, 0);
    CHECK_EQ_U32(U(w)->spawn_gfx_0.useUnkBone, 1);
    CHECK_EQ_U32(U(w + 4)->spawn_gfx_1.gfxID, 0x0300);
    CHECK_EQ_U32(U(w + 4)->spawn_gfx_1.unkFloat, 0x3F80);
    CHECK(U(w + 8)->spawn_gfx_2.offsetZ == -2);
    CHECK(U(w + 8)->spawn_gfx_2.offsetY == 2);
    CHECK(U(w + 12)->spawn_gfx_3.offsetX == -32768);
    CHECK_EQ_U32(U(w + 12)->spawn_gfx_3.rangeZ, 0x7FFF);
    CHECK_EQ_U32(U(w + 16)->spawn_gfx_4.rangeY, 0x10);
    CHECK_EQ_U32(U(w + 16)->spawn_gfx_4.rangeX, 0x20);
    CHECK_EQ_U32(U(w + 20)->Command_00.code, 6);

    w = TAIL;
    CHECK_EQ_U32(U(w)->Command_00.code, 5);
    CHECK_EQ_U32(*(const uint32_t*) (buf + w + 4), (uint32_t) (uintptr_t) buf + SUB);
    CHECK_EQ_U32(U(w + 8)->Command_00.code, 0);
    CHECK_EQ_U32(buf[after_end], 0xEE);

    /* SUB was reached from SCRIPT_A, TAIL and SCRIPT_B and still reads correctly
     * above, so a stream reached more than once within a call is converted once.
     * The table entries themselves are untouched (port_swap_ft_anim_entries owns them). */
    CHECK_EQ_U32(buf[TABLE + 4], 0);
}

TEST_MAIN(run)
