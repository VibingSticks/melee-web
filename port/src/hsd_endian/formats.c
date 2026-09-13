#include "formats.h"

#include <stdlib.h>
#include <string.h>

#include "../port.h"
#include "walker.h"

static uint32_t bswap32(uint32_t v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
}

static uint16_t bswap16(uint16_t v)
{
    return (uint16_t) ((v >> 8) | (v << 8));
}

void port_swap_u32_array(uint32_t* p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        p[i] = bswap32(p[i]);
    }
}

void port_swap_u16_array(uint16_t* p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        p[i] = bswap16(p[i]);
    }
}

size_t port_swap_ssm_table(uint32_t* table, uint32_t groups)
{
    uint32_t* p = table;
    for (uint32_t g = 0; g < groups; g++) {
        uint32_t n;
        p[0] = bswap32(p[0]);
        p[1] = bswap32(p[1]);
        n = p[0];
        p += 2;
        for (uint32_t k = 0; k < n; k++) {
            port_swap_u16_array((uint16_t*) p, 32); /* the whole 0x40 block */
            p += 16;
        }
    }
    return (size_t) ((uint8_t*) p - (uint8_t*) table);
}

void port_swap_hps_file_header(uint32_t* header)
{
    port_swap_u32_array(header + 2, 2);                 /* rate, channels */
    port_swap_u16_array((uint16_t*) (header + 4), 56);  /* two channels of AXPBADDR + AXPBADPCM */
}

void port_swap_hps_block_header(uint32_t* header)
{
    port_swap_u32_array(header, 3);                     /* length, end, next */
    port_swap_u16_array((uint16_t*) (header + 3), 8);   /* two channels of AXPBADPCMLOOP + pad */
}

size_t port_swap_sem_header(uint32_t* file)
{
    uint32_t* p = file;
    for (int run = 0; run < 4; run++) {
        uint32_t n;
        p[0] = bswap32(p[0]);
        n = p[0];
        port_swap_u32_array(p + 1, n);
        p += 1 + n;
    }
    return (size_t) ((uint8_t*) p - (uint8_t*) file);
}

/* --- particle banks (sysdolphin particle.c) ---------------------------------
 *
 * Command bank: u16 version, u16 pad, then
 *   version 0:        u32 count, u32 offsets[count]
 *   version 0x40..43: u32 num, u32 count, u32 offsets[count]
 * each offset naming an HSD_PSCmdList: four u16, then u32 kind and twelve
 * floats, then a byte command stream.
 *
 * Texture bank: u32 group_count, u32 offsets[group_count], each naming an
 * HSD_PSTexGroup: five u32 (num, fmt, tlutfmt, width, height), u16 palnum,
 * u16 palflag, then the texel/palette offset table.
 *
 * Form bank: offsets indexed 1..group_count (index 0 is unused), each naming an
 * HSD_PSFormGroup: u32 num, then num offsets to byte-code texture forms. It has
 * no count of its own; psInitDataBankLocate walks it with the texture bank's
 * group count. */
static void swap_cmd_list(uint8_t* base, uint32_t offset, uint32_t size)
{
    if (offset == 0 || offset + 0x3C > size) {
        return;
    }
    uint32_t* p = (uint32_t*) (base + offset);
    port_swap_u16_array((uint16_t*) p, 4);  /* type, texGroup, genLife, life */
    port_swap_u32_array(p + 2, 13);         /* kind, then twelve floats */
}

static void swap_form_group(uint8_t* base, uint32_t offset, uint32_t size)
{
    if (offset == 0 || offset + 4 > size) {
        return;
    }
    uint32_t* g = (uint32_t*) (base + offset);
    g[0] = bswap32(g[0]);
    uint32_t num = g[0];
    if (num < 0x10000 && offset + 4 + num * 4 <= size) {
        port_swap_u32_array(g + 1, num);
    }
}

static void swap_tex_group(uint8_t* base, uint32_t offset, uint32_t size)
{
    if (offset == 0 || offset + 0x18 > size) {
        return;
    }
    uint32_t* g = (uint32_t*) (base + offset);
    port_swap_u32_array(g, 5); /* num, fmt, tlutfmt, width, height */
    port_swap_u16_array((uint16_t*) (g + 5), 2); /* palnum, palflag */
    uint32_t num = g[0];
    uint32_t fmt = g[1];
    uint16_t palnum = ((uint16_t*) (g + 5))[0];
    uint16_t palflag = ((uint16_t*) (g + 5))[1];
    uint32_t entries = num;
    if (fmt == 8 || fmt == 9 || fmt == 10) { /* colour-indexed: palettes follow */
        entries = (palflag & 1) ? num + 1 : (palnum != 0 ? num + palnum : num * 2);
    }
    if (offset + 0x18 + entries * 4 <= size) {
        port_swap_u32_array(g + 6, entries);
    }
}

void port_swap_ptcl_banks(void* cmd_bank, void* tex_bank, void* form_bank)
{
    /* The banks sit inside an archive whose size we do not know here; the
     * offsets are bounded by the largest one seen, which is enough to keep a
     * malformed bank from running off. */
    const uint32_t kMaxBank = 0x400000;

    if (cmd_bank != NULL) {
        uint8_t* base = cmd_bank;
        uint32_t* w = cmd_bank;
        port_swap_u16_array((uint16_t*) w, 2);
        uint16_t version = ((uint16_t*) w)[0];
        uint32_t first, count;
        if (version == 0) {
            w[1] = bswap32(w[1]);
            first = 2;
            count = w[1];
        } else if (version >= 0x40 && version < 0x44) {
            w[1] = bswap32(w[1]);
            w[2] = bswap32(w[2]);
            first = 3;
            count = w[2];
        } else {
            count = 0;
            first = 0;
        }
        if (count != 0 && count < 0x10000) {
            port_swap_u32_array(w + first, count);
            for (uint32_t i = 0; i < count; i++) {
                swap_cmd_list(base, w[first + i], kMaxBank);
            }
        }
    }

    if (tex_bank != NULL) {
        uint8_t* base = tex_bank;
        uint32_t* w = tex_bank;
        w[0] = bswap32(w[0]);
        uint32_t groups = w[0];
        if (groups != 0 && groups < 0x10000) {
            port_swap_u32_array(w + 1, groups);
            for (uint32_t i = 0; i < groups; i++) {
                swap_tex_group(base, w[1 + i], kMaxBank);
            }
            if (form_bank != NULL) {
                uint8_t* fbase = form_bank;
                uint32_t* f = form_bank;
                port_swap_u32_array(f + 1, groups); /* index 0 is unused */
                for (uint32_t i = 1; i <= groups; i++) {
                    swap_form_group(fbase, f[i], kMaxBank);
                }
            }
        }
    }
}

void port_swap_ft_anim_entries(void* entries, uint32_t count)
{
    uint32_t* e = entries;
    if (entries == NULL) {
        return;
    }
    for (uint32_t i = 0; i < count; i++, e += 6) {
        e[1] = bswap32(e[1]); /* x4 */
        e[2] = bswap32(e[2]); /* x8 */
        e[4] = bswap32(e[4]); /* x10 */
        e[5] = bswap32(e[5]); /* x14 */
    }
}

/* --- fighter subaction scripts --- */

/* One word of a command: the widths of the bitfields the game reads it
 * through (n == 0: a relocated pointer, already native). The widths come from
 * the structs in melee/lb/types.h named in the comments; a word the game reads
 * as a plain u32 is one 32-bit field. */
typedef struct {
    uint8_t n;
    uint8_t w[8];
} ft_cmd_word;

typedef struct {
    uint8_t nwords;
    ft_cmd_word word[7];
} ft_cmd_layout;

#define B(...) { sizeof((uint8_t[]){ __VA_ARGS__ }), { __VA_ARGS__ } }
#define PTR { 0, { 0 } }
#define OP(...) B(6, __VA_ARGS__)
#define OP1 OP(26) /* an opcode and one 26-bit operand, or an opcode alone */

/* Indexed by opcode: 0-9 are the generic lbcommand.c commands, 10-58 the
 * fighter ones (ftAction_803C06E8 / ftAction_803C07AC, whose word counts are
 * ftAction_803C0870). */
static const ft_cmd_layout ft_cmd_layouts[] = {
    /*  0 */ { 1, { OP1 } },                                 /* Command_00: end of script */
    /*  1 */ { 1, { OP1 } },                                 /* Command_01: synchronous timer */
    /*  2 */ { 1, { OP1 } },                                 /* Command_02: asynchronous timer */
    /*  3 */ { 1, { OP1 } },                                 /* Command_03: set loop */
    /*  4 */ { 1, { OP1 } },                                 /* Command_04: execute loop */
    /*  5 */ { 2, { OP1, PTR } },                            /* Command_05: subroutine */
    /*  6 */ { 1, { OP1 } },                                 /* Command_06: return */
    /*  7 */ { 2, { OP1, PTR } },                            /* Command_07: goto */
    /*  8 */ { 1, { OP1 } },                                 /* Command_08: set timer animation */
    /*  9 */ { 1, { OP(8, 18) } },                           /* Command_09 */
    /* 10 */ { 5, { OP(8, 1, 1, 1, 15), B(16, 16), B(16, 16), B(16, 16), B(16, 16) } }, /* spawn_gfx_0..4 */
    /* 11 */ { 5, { OP(3, 3, 1, 8, 1, 10), B(16, 16), B(16, 16), B(9, 9, 9, 1, 1, 1, 1, 1),
                    B(9, 5, 8, 3, 5, 1, 1) } },              /* spawn_hitbox_0..4 */
    /* 12 */ { 1, { OP(3, 23) } },                           /* set_hitbox_damage */
    /* 13 */ { 1, { OP(3, 23) } },                           /* set_hitbox_scale */
    /* 14 */ { 1, { OP(24, 1, 1) } },                        /* set_hitbox_x42_b57 */
    /* 15 */ { 1, { OP1 } },                                 /* set_throw_flags */
    /* 16 */ { 1, { OP1 } },                                 /* ftAction_800717D8 */
    /* 17 */ { 3, { OP(8, 18), B(32), B(16, 8, 8) } },       /* sound_effect_0..2 */
    /* 18 */ { 1, { OP1 } },                                 /* ftAction_80071CCC */
    /* 19 */ { 1, { OP(2, 24) } },                           /* set_cmd_var */
    /* 20 */ { 1, { OP1 } },                                 /* set_throw_flags */
    /* 21 */ { 1, { OP1 } },                                 /* ftAction_80071908 */
    /* 22 */ { 1, { OP1 } },                                 /* ftAction_8007192C */
    /* 23 */ { 1, { OP1 } },                                 /* ftAction_80071950 */
    /* 24 */ { 1, { OP1 } },                                 /* ftAction_80071974 */
    /* 25 */ { 1, { OP1 } },                                 /* set_airborne_state */
    /* 26 */ { 1, { OP1 } },                                 /* set_airborne_state */
    /* 27 */ { 1, { OP1 } },                                 /* set_airborne_state */
    /* 28 */ { 1, { OP(8, 18) } },                           /* set_hurt_state */
    /* 29 */ { 1, { OP1 } },                                 /* set_jab_combo */
    /* 30 */ { 1, { OP1 } },                                 /* set_jab_rapid */
    /* 31 */ { 1, { OP(7, 19) } },                           /* set_dobj_flags */
    /* 32 */ { 1, { OP1 } },                                 /* ftAction_80071D94 */
    /* 33 */ { 1, { OP1 } },                                 /* ftAction_80071DCC */
    /* 34 */ { 3, { OP(3, 23), B(9, 9, 9), B(9, 4, 3, 4) } }, /* set_throw_hitbox_0..2 */
    /* 35 */ { 1, { OP1 } },                                 /* unk27 */
    /* 36 */ { 1, { OP1 } },                                 /* set_article_vis */
    /* 37 */ { 1, { OP1 } },                                 /* set_fighter_vis */
    /* 38 */ { 7, { OP(8, 8, 4, 6), B(32), B(32), B(32), B(32), B(32), B(32) } }, /* pseudo_random_sfx_0..1 */
    /* 39 */ { 4, { OP(10, 8, 8), B(32), B(16, 16), B(16, 8, 8) } }, /* stage_sfx_0..3 */
    /* 40 */ { 1, { OP(1, 7, 7, 11) } },                     /* set_tex_anim */
    /* 41 */ { 1, { OP(7, 7, 12) } },                        /* part_anim */
    /* 42 */ { 1, { OP(13, 13) } },                          /* unk9 */
    /* 43 */ { 1, { OP(1, 12, 13) } },                       /* unk10 */
    /* 44 */ { 1, { OP1 } },                                 /* unk11 */
    /* 45 */ { 1, { OP(2, 10, 14) } },                       /* unk12 */
    /* 46 */ { 1, { OP(8, 18) } },                           /* unk13 */
    /* 47 */ { 1, { OP(8) } },                               /* unk14 */
    /* 48 */ { 1, { OP1 } },                                 /* unk15 */
    /* 49 */ { 1, { OP(1, 25) } },                           /* unk16 */
    /* 50 */ { 1, { OP1 } },                                 /* unk17 */
    /* 51 */ { 1, { OP1 } },                                 /* unk18 */
    /* 52 */ { 1, { OP1 } },                                 /* unk19 */
    /* 53 */ { 1, { OP1 } },                                 /* unk20 */
    /* 54 */ { 3, { OP(8, 1, 1, 8, 8), B(32), B(16, 8, 8) } }, /* footstep_fx_0, then sound_effect_1..2 */
    /* 55: ftAction_80072E4C reads its first word three ways: sound_effect_0.behavior
     * (bits 6-13, through ftAction_80071B50), a flag that is big-endian bit 15,
     * and the low half as one u16. The layout follows those reads, not unk_fx_0's
     * byte fields, and the flag read is footstep_fx_0.x1_b7 under TARGET_PC. */
    /* 55 */ { 3, { OP(8, 1, 1, 16), B(32), B(16, 8, 8) } }, /* then sound_effect_1..2 */
    /* 56 */ { 2, { OP(10, 16), B(8, 24) } },                /* smash_charge_0..1 */
    /* 57 */ { 1, { OP(1, 8) } },                            /* unk21 */
    /* 58 */ { 4, { OP(18, 8), B(16, 16), B(16, 16), B(16, 16) } }, /* wind_fx_0..3 */
};
#define FT_CMD_OPCODES (sizeof ft_cmd_layouts / sizeof ft_cmd_layouts[0])

#undef B
#undef PTR
#undef OP
#undef OP1

enum { FT_CMD_END = 0, FT_CMD_SUBROUTINE = 5, FT_CMD_RETURN = 6, FT_CMD_GOTO = 7 };

/* Words already converted, so a subroutine two scripts share, or a goto into
 * a stream walked earlier, is repacked once. Open addressing on the address. */
typedef struct {
    uintptr_t* slots;
    uint32_t cap;
    uint32_t n;
} word_set;

static int word_set_add(word_set* s, uintptr_t a) /* 1 if new, 0 if present, -1 out of memory */
{
    if ((s->n + 1) * 4 > s->cap * 3) {
        uint32_t ncap = s->cap != 0 ? s->cap * 2 : 4096;
        uintptr_t* nslots = calloc(ncap, sizeof *nslots);
        if (nslots == NULL) {
            return -1;
        }
        for (uint32_t i = 0; i < s->cap; i++) {
            if (s->slots[i] != 0) {
                uint32_t j = (uint32_t) ((s->slots[i] >> 2) * 2654435761u) & (ncap - 1);
                while (nslots[j] != 0) {
                    j = (j + 1) & (ncap - 1);
                }
                nslots[j] = s->slots[i];
            }
        }
        free(s->slots);
        s->slots = nslots;
        s->cap = ncap;
    }
    uint32_t j = (uint32_t) ((a >> 2) * 2654435761u) & (s->cap - 1);
    while (s->slots[j] != 0) {
        if (s->slots[j] == a) {
            return 0;
        }
        j = (j + 1) & (s->cap - 1);
    }
    s->slots[j] = a;
    s->n++;
    return 1;
}

typedef struct {
    const uint8_t* base;
    word_set seen;
    int errors;
} ft_script_ctx;

static uint8_t* resolve_slot(const ft_script_ctx* c, const uint8_t* slot)
{
    uint32_t v;
    memcpy(&v, slot, 4);
    if (v == 0) {
        return NULL;
    }
    uintptr_t base = (uintptr_t) c->base;
    return (uint8_t*) (base + (uint32_t) (v - (uint32_t) base));
}

#define FT_SCRIPT_MAX_WORDS 0x20000 /* a stream longer than this is not a script */

static void convert_stream(ft_script_ctx* c, uint8_t* p, unsigned depth)
{
    uint32_t walked = 0;
    if (depth > 64) {
        port_log("ft_script: subroutines nested too deep at %p", (void*) p);
        c->errors++;
        return;
    }
    while (p != NULL) {
        int added = word_set_add(&c->seen, (uintptr_t) p);
        if (added <= 0) {
            if (added < 0) {
                c->errors++;
            }
            return; /* converted by an earlier walk (or out of memory) */
        }
        unsigned op = p[0] >> 2; /* the top 6 bits of the big-endian word */
        if (op >= FT_CMD_OPCODES) {
            port_log("ft_script: unknown opcode %u at %p", op, (void*) p);
            c->errors++;
            return;
        }
        const ft_cmd_layout* l = &ft_cmd_layouts[op];
        for (unsigned k = 0; k < l->nwords; k++) {
            if (k != 0) {
                word_set_add(&c->seen, (uintptr_t) (p + 4 * k));
            }
            if (l->word[k].n != 0) {
                port_repack_bits(p + 4 * k, 4, l->word[k].w, l->word[k].n);
            }
        }
        if (op == FT_CMD_END || op == FT_CMD_RETURN) {
            return;
        }
        if (op == FT_CMD_SUBROUTINE || op == FT_CMD_GOTO) {
            convert_stream(c, resolve_slot(c, p + 4), depth + 1);
            if (op == FT_CMD_GOTO) {
                return;
            }
        }
        p += 4 * l->nwords;
        walked += l->nwords;
        if (walked > FT_SCRIPT_MAX_WORDS) {
            port_log("ft_script: no end of script within %u words of %p", walked, (void*) p);
            c->errors++;
            return;
        }
    }
}

static void convert_table(ft_script_ctx* c, void* entries, uint32_t count)
{
    uint32_t* e = entries;
    if (entries == NULL) {
        return;
    }
    for (uint32_t i = 0; i < count; i++, e += 6) {
        convert_stream(c, resolve_slot(c, (const uint8_t*) (e + 3)), 0); /* Fighter_WaitAnimData::xC */
    }
}

void port_swap_ft_cmd_scripts(const void* base, void* entries_a, uint32_t count_a, void* entries_b,
                              uint32_t count_b)
{
    ft_script_ctx c;
    memset(&c, 0, sizeof c);
    c.base = base;
    convert_table(&c, entries_a, count_a);
    convert_table(&c, entries_b, count_b);
    if (c.errors != 0) {
        port_log("ft_script: %d problems converting %u+%u scripts", c.errors, (unsigned) count_a,
                 (unsigned) count_b);
    }
    free(c.seen.slots);
}

/* Remembers a block that has already been converted; returns 0 when it has.
 * The table is small and the block counts are tiny, so a linear scan is fine. */
static int mark_seen(const void** seen, unsigned* n, const void* p)
{
    for (unsigned i = 0; i < *n; i++) {
        if (seen[i] == p) {
            return 0;
        }
    }
    if (*n < 512) {
        seen[(*n)++] = p;
    }
    return 1;
}

void port_swap_ft_costume_tobjs(void* ftdata_x8, uint32_t costumes)
{
    if (ftdata_x8 == NULL) {
        return;
    }
    /* ftData_x8: FtPartsDesc x0 (8 bytes), then { u32 count; u16** lists } */
    uint32_t count = ((uint32_t*) ftdata_x8)[2];
    uint16_t** lists = (uint16_t**) ((uint32_t*) ftdata_x8)[3];
    if (lists == NULL || count == 0 || count > 0x1000) {
        return;
    }
    for (uint32_t c = 0; c < costumes; c++) {
        if (lists[c] != NULL) {
            port_swap_u16_array(lists[c], count);
        }
    }
}

void port_swap_ft_parts_vis(void* ftdata_x8, uint32_t costumes)
{
    if (ftdata_x8 == NULL) {
        return;
    }

    /* FtPartsDesc: { u32 model_num; void* (*vis_table)[4] } */
    uint32_t* desc = ftdata_x8;
    uint32_t model_num = desc[0];
    uint32_t** rows = (uint32_t**) (uintptr_t) desc[1];
    if (rows == NULL || model_num == 0 || model_num > 64) {
        return;
    }

    /* Costumes and slots share both the per-model tables and the entry lists
     * inside them, so every block is converted exactly once. */
    const void* seen[512];
    unsigned nseen = 0;
    for (uint32_t c = 0; c < costumes; c++) {
        for (unsigned slot = 0; slot < 4; slot++) {
            uint32_t* lookup = rows[c * 4 + slot];
            if (lookup == NULL || !mark_seen(seen, &nseen, lookup)) {
                continue;
            }
            for (uint32_t m = 0; m < model_num; m++) {
                uint32_t* entry = lookup + m * 2; /* { s32 count; TempS* list } */
                entry[0] = bswap32(entry[0]);
                uint32_t n = entry[0];
                uint32_t* list = (uint32_t*) (uintptr_t) entry[1];
                if (list == NULL || n > 0x1000 || !mark_seen(seen, &nseen, list)) {
                    continue;
                }
                for (uint32_t j = 0; j < n; j++) {
                    list[j * 2] = bswap32(list[j * 2]); /* { s32 count; u8* } */
                }
            }
        }
    }
}
