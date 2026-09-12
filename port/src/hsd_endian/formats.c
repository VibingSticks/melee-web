#include "formats.h"

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
            port_swap_u32_array(p, 4);                     /* 0x00..0x0F */
            port_swap_u16_array((uint16_t*) (p + 4), 2);   /* loopFlag, format */
            port_swap_u32_array(p + 5, 3);                 /* loop, end, current */
            port_swap_u16_array((uint16_t*) (p + 8), 16);  /* ADPCM coefficients */
            p += 16;
        }
    }
    return (size_t) ((uint8_t*) p - (uint8_t*) table);
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
 * u16 palflag, then the texel/palette offset table. */
static void swap_cmd_list(uint8_t* base, uint32_t offset, uint32_t size)
{
    if (offset == 0 || offset + 0x3C > size) {
        return;
    }
    uint32_t* p = (uint32_t*) (base + offset);
    port_swap_u16_array((uint16_t*) p, 4);  /* type, texGroup, genLife, life */
    port_swap_u32_array(p + 2, 13);         /* kind, then twelve floats */
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

void port_swap_ptcl_banks(void* cmd_bank, void* tex_bank)
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
