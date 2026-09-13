/* Font atlases, read from the player's own disc at startup.
 *
 * sysdolphin keeps its two font atlases as plain bitmaps in the executable's
 * data section rather than in a DAT file, so the decompilation has nothing to
 * compile them from: sislib_font.c and hsd_3915.c each #include a generated
 * .inc that is not in the tree. Both units were therefore excluded from the
 * build, which is why nothing in this port drew a single character of text.
 *
 * The bytes are on the disc the player already supplied, in the boot DOL, so
 * this reads them from there into the two arrays before the game starts. The
 * data stays on their disc; nothing is baked into the build.
 *
 * Both atlases are flat u8 bitmaps with no pointers and no multi-byte fields,
 * so they need no endian conversion -- a straight copy is correct.
 */
#include "font_dol.h"

#include <stdint.h>
#include <string.h>

#include "dvd_web/disc_io.h"
#include "port.h"

/* Defined by the game's hsd_3915.c and sislib_font.c. Both are arrays of
 * structs whose only member is a u8 bitmap, so a flat byte view names the same
 * object -- and declaring them here keeps the game's headers, which assume its
 * own 4-byte bool, out of this translation unit. */
extern unsigned char HSD_DebugFontAtlas[];
extern unsigned char HSD_SisLib_FontAtlas[];

#define DISC_DOL_OFFSET 0x420 /* boot.bin: where the executable starts */
#define DOL_TEXT_SECTIONS 7
#define DOL_DATA_SECTIONS 11
#define DOL_SECTIONS (DOL_TEXT_SECTIONS + DOL_DATA_SECTIONS)
#define DOL_HEADER_SIZE 0x100

typedef struct {
    int done;
    int status;
} read_state;

static void on_read(void* user, int status)
{
    read_state* s = user;
    s->status = status;
    s->done = 1;
}

/* A disc read that waits. Only used during startup, before the game's own
 * frame loop exists; port_yield runs the browser's event loop meanwhile. */
static int read_blocking(uint32_t offset, uint32_t length, void* dst)
{
    read_state s = { 0, 0 };
    port_disc_read(offset, length, dst, on_read, &s);
    while (!s.done) {
        port_yield();
    }
    return s.status;
}

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

typedef struct {
    uint32_t offset; /* in the disc image */
    uint32_t addr;   /* where the section loads in memory */
    uint32_t size;
} dol_section;

/* Copies `size` bytes of the executable starting at memory address `addr`. */
static int copy_from_dol(const dol_section* sec, uint32_t dol_base, uint32_t addr, uint32_t size, void* dst)
{
    for (int i = 0; i < DOL_SECTIONS; i++) {
        if (sec[i].size == 0 || addr < sec[i].addr || addr - sec[i].addr >= sec[i].size) {
            continue;
        }
        uint32_t within = addr - sec[i].addr;
        if (size > sec[i].size - within) {
            port_log("font: %#x+%#x runs past its DOL section", addr, size);
            return -1;
        }
        return read_blocking(dol_base + sec[i].offset + within, size, dst);
    }
    port_log("font: no DOL section holds %#x", addr);
    return -1;
}

void port_font_load_from_dol(void)
{
    uint8_t buf[DOL_HEADER_SIZE];

    if (read_blocking(DISC_DOL_OFFSET, 4, buf) != 0) {
        port_log("font: could not read the disc header; text will be blank");
        return;
    }
    uint32_t dol_base = be32(buf);
    if (dol_base == 0 || dol_base >= port_disc_size()) {
        port_log("font: the disc header has no usable DOL offset; text will be blank");
        return;
    }

    if (read_blocking(dol_base, DOL_HEADER_SIZE, buf) != 0) {
        port_log("font: could not read the DOL header; text will be blank");
        return;
    }

    dol_section sec[DOL_SECTIONS];
    for (int i = 0; i < DOL_SECTIONS; i++) {
        sec[i].offset = be32(buf + 0x00 + 4 * i);
        sec[i].addr = be32(buf + 0x48 + 4 * i);
        sec[i].size = be32(buf + 0x90 + 4 * i);
    }

    int rc = 0;
    rc |= copy_from_dol(sec, dol_base, PORT_DEBUG_FONT_ADDR, PORT_DEBUG_FONT_SIZE, HSD_DebugFontAtlas);
    rc |= copy_from_dol(sec, dol_base, PORT_SIS_FONT_ADDR, PORT_SIS_FONT_SIZE, HSD_SisLib_FontAtlas);
    if (rc != 0) {
        port_log("font: atlases incomplete; some text will be blank");
    } else {
        port_log("font: atlases loaded from the disc's executable");
    }
}
