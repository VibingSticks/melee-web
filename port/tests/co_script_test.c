/* port_swap_co_cmd_scripts: colour-overlay scripts (lb_013B.c lb_80014258)
 * are the generic bytecode with the lb_803BA248 handlers at opcodes 10-20 and
 * the fighter's or the item's commands from 21. The words are read back the
 * way the handlers read them -- union ColorOverlay_x8_t and union CmdUnion --
 * so the width table in formats.c is checked against clang's real layout of
 * those structs. Also checks that a GXColor word is left as bytes, that
 * opcode 10 ends a script, and that the two command sets differ from 21. */
#include "check.h"
#include "hsd_endian/formats.h"

#include <melee/lb/types.h>

static uint8_t buf[512];

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

#define CO(off) ((const union ColorOverlay_x8_t*) (buf + (off)))
#define U(off) ((const union CmdUnion*) (buf + (off)))
#define F(...) be_fields(sizeof((int[]){ __VA_ARGS__ }) / (2 * sizeof(int)), (const int[]){ __VA_ARGS__ })

enum { ROWS = 0, SCRIPT_FT = 0x40, TAIL = 0xC0, SCRIPT_IT = 0x100, SCRIPT_GR = 0x140 };

/* grMaterial_801C9490's word under TARGET_PC (grmaterial.c): the value the
 * PowerPC build reads as (top halfword >> 2) & 0xFF. */
struct gr_cmd_word {
    u32 opcode : 6;
    u32 value : 8;
    u32 pad : 2;
    u32 rest : 16;
};
#define G(off) ((const struct gr_cmd_word*) (buf + (off)))

static unsigned nnoted;
static void note(void* user, const void* slot)
{
    CHECK(user == buf);
    (void) slot;
    nnoted++;
}

static void run(void)
{
    memset(buf, 0xEE, sizeof buf);

    /* Two Fighter_804D653C_t rows {script, u8, u8, pad}. */
    memset(buf + ROWS, 0, 16);
    put_ptr(ROWS, SCRIPT_FT);
    put_ptr(ROWS + 8, SCRIPT_IT);

    uint32_t p = SCRIPT_FT;
    put_be32(p, F(6, 11, 26, 5)); p += 4;                                    /* unk.timer 5 */
    /* 13: light_rot2 -- the opcode is x0_0..x0_5 (001101); light_enable 1, x0_7 0, x -3, yz 7 -- then a GXColor */
    put_be32(p, F(1, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 12, -3, 12, 7)); p += 4;
    put_be32(p, 0x11223344u); p += 4;
    const uint32_t color_word = p - 4;
    put_be32(p, F(6, 16, 13, -100, 13, 200)); p += 4;                       /* light_rot1 x -100, yz 200 */
    put_be32(p, F(6, 23, 1, 1, 8, 0xA5)); p += 4;                           /* fighter: unk21 */
    put_be32(p, F(6, 22, 8, 0x3C, 18, 0x2ABCD)); p += 4;                    /* fighter: sound_effect_0 */
    put_be32(p, 0x01020304u); p += 4;                                       /* sound_effect_1 (u32) */
    put_be32(p, F(16, 0x1234, 8, 0x56, 8, 0x78)); p += 4;                   /* sound_effect_2 */
    put_be32(p, F(6, 7, 26, 0)); put_ptr(p + 4, TAIL); p += 8;              /* goto TAIL */
    const uint32_t junk = p;

    p = TAIL;
    put_be32(p, F(6, 19, 26, 9)); p += 4;                                   /* unk.timer 9, then a GXColor */
    put_be32(p, 0xAABBCCDDu); p += 4;
    put_be32(p, F(6, 10, 26, 0x3FFFFFF)); p += 4;                           /* end of script */
    const uint32_t after_end = p;

    p = SCRIPT_IT;
    put_be32(p, F(6, 2, 26, 7)); p += 4;                                    /* asynchronous timer */
    put_be32(p, F(6, 21, 10, 0x2A5, 16, 0xBEEF)); p += 4;                   /* item opcode 10 */
    put_be32(p, F(16, 0x0123, 16, 0x0007)); p += 4;
    put_be32(p, F(16, 0xFFFE, 16, 0x0002)); p += 4;
    put_be32(p, F(16, 0x8000, 16, 0x7FFF)); p += 4;
    put_be32(p, F(16, 0x0010, 16, 0x0020)); p += 4;
    put_be32(p, F(6, 22, 8, 5, 2, 0, 16, 0xABCD)); p += 4;                  /* item opcode 16, sub-opcode 5: two words */
    put_be32(p, 0x0BADF00Du); p += 4;
    put_be32(p, F(6, 0, 26, 0)); p += 4;                                    /* Command_00 */
    const uint32_t it_after_end = p;

    /* A stage background script, pointed at by a bare word (a yakumono_param field). */
    put_ptr(ROWS + 16, SCRIPT_GR);
    p = SCRIPT_GR;
    put_be32(p, F(6, 11, 26, 3)); p += 4;                                   /* unk.timer 3 */
    put_be32(p, F(6, 21, 8, 0xC3, 2, 1, 16, 0x5A5A)); p += 4;               /* ground opcode 21: value 0xC3 */
    put_be32(p, F(6, 10, 26, 0)); p += 4;                                   /* end */
    const uint32_t gr_after_end = p;

    const void* ft_slots[1] = { buf + ROWS };
    port_swap_co_cmd_scripts(buf, ft_slots, 1, PORT_CO_FIGHTER, note, buf);
    const void* it_slots[1] = { buf + ROWS + 8 };
    port_swap_co_cmd_scripts(buf, it_slots, 1, PORT_CO_ITEM, note, buf);
    const void* gr_slots[1] = { buf + ROWS + 16 };
    port_swap_co_cmd_scripts(buf, gr_slots, 1, PORT_CO_GROUND, note, buf);

    uint32_t w = SCRIPT_FT;
    CHECK_EQ_U32(CO(w)->unk.unk, 11);
    CHECK_EQ_U32(CO(w)->unk.timer, 5);
    w += 4;
    CHECK_EQ_U32(CO(w)->unk.unk, 13);                                       /* the dispatch read */
    CHECK_EQ_U32(CO(w)->light_rot2.light_enable, 1);
    CHECK_EQ_U32(CO(w)->light_rot2.x0_7, 0);
    CHECK(CO(w)->light_rot2.x == -3);
    CHECK(CO(w)->light_rot2.yz == 7);
    CHECK_EQ_U32(CO(color_word)->light_color.r, 0x11);
    CHECK_EQ_U32(CO(color_word)->light_color.g, 0x22);
    CHECK_EQ_U32(CO(color_word)->light_color.b, 0x33);
    CHECK_EQ_U32(CO(color_word)->light_color.a, 0x44);
    w += 8;
    CHECK_EQ_U32(CO(w)->unk.unk, 16);
    CHECK(CO(w)->light_rot1.x == -100);
    CHECK(CO(w)->light_rot1.yz == 200);
    w += 4;
    CHECK_EQ_U32(U(w)->unk21.unk1, 1);
    CHECK_EQ_U32(U(w)->unk21.unk2, 0xA5);
    w += 4;
    CHECK_EQ_U32(U(w)->sound_effect_0.behavior, 0x3C);
    CHECK_EQ_U32(U(w)->sound_effect_0.unknown, 0x2ABCD);
    CHECK_EQ_U32(*(const u32*) (buf + w + 4), 0x01020304u);
    CHECK_EQ_U32(U(w + 8)->sound_effect_2.padding, 0x1234);
    CHECK_EQ_U32(U(w + 8)->sound_effect_2.volume, 0x56);
    CHECK_EQ_U32(U(w + 8)->sound_effect_2.panning, 0x78);
    w += 12;
    CHECK_EQ_U32(U(w)->Command_00.code, 7);
    CHECK_EQ_U32(*(const u32*) (buf + junk), 0xEEEEEEEEu);                   /* nothing past the goto */
    CHECK_EQ_U32(nnoted, 1);                                                /* the goto's pointer word */

    w = TAIL;
    CHECK_EQ_U32(CO(w)->unk.unk, 19);
    CHECK_EQ_U32(CO(w)->unk.timer, 9);
    CHECK_EQ_U32(CO(w + 4)->light_color.r, 0xAA);
    CHECK_EQ_U32(CO(w + 4)->light_color.a, 0xDD);
    CHECK_EQ_U32(CO(w + 8)->unk.unk, 10);
    CHECK_EQ_U32(CO(w + 8)->unk.timer, 0x3FFFFFF);
    CHECK_EQ_U32(*(const u32*) (buf + after_end), 0xEEEEEEEEu);              /* opcode 10 ended the script */

    w = SCRIPT_IT;
    CHECK_EQ_U32(U(w)->Command_02.code, 2);
    CHECK_EQ_U32(U(w)->Command_02.value, 7);
    w += 4;
    CHECK_EQ_U32(CO(w)->unk.unk, 21);
    CHECK_EQ_U32(((const u16*) (buf + w + 4))[0], 0x0123);
    CHECK(((const s16*) (buf + w + 8))[0] == -2);
    CHECK(((const s16*) (buf + w + 16))[1] == 0x20);
    w += 20;
    CHECK_EQ_U32(CO(w)->unk.unk, 22);
    CHECK_EQ_U32(*(const u32*) (buf + w + 4), 0x0BADF00Du);
    w += 8;
    CHECK_EQ_U32(U(w)->Command_00.code, 0);
    CHECK_EQ_U32(*(const u32*) (buf + it_after_end), 0xEEEEEEEEu);

    w = SCRIPT_GR;
    CHECK_EQ_U32(CO(w)->unk.unk, 11);
    CHECK_EQ_U32(CO(w)->unk.timer, 3);
    w += 4;
    CHECK_EQ_U32(CO(w)->unk.unk, 21);                                       /* the dispatch read */
    CHECK_EQ_U32(G(w)->value, 0xC3);                                        /* grMaterial_801C9490's read */
    CHECK_EQ_U32(G(w)->pad, 1);
    CHECK_EQ_U32(G(w)->rest, 0x5A5A);
    w += 4;
    CHECK_EQ_U32(CO(w)->unk.unk, 10);
    CHECK_EQ_U32(*(const u32*) (buf + gr_after_end), 0xEEEEEEEEu);
}

TEST_MAIN(run)
