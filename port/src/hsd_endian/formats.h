/* Byte-order fixups for raw (non-archive) game data the game DVD-reads straight
 * into memory and then indexes as native integers: .ssm sound-bank headers,
 * and later .sem/.hps headers (plan Task 18). Each helper is idempotent only
 * in the sense that the caller must apply it exactly once per read. */
#ifndef PORT_HSD_ENDIAN_FORMATS_H
#define PORT_HSD_ENDIAN_FORMATS_H

#include <stddef.h>
#include <stdint.h>

void port_swap_u32_array(uint32_t* p, size_t n);
void port_swap_u16_array(uint16_t* p, size_t n);

/* .ssm sound-bank table: `groups` runs of { u32 n; u32 x; block[n] }, where
 * each 0x40-byte block is
 *   { u32 w[4]; u16 loopFlag, format; u32 loopAddr, endAddr, currentAddr; s16 coef[16] }.
 * The three addresses are AXPBADDR hi/lo u16 pairs on the GameCube, but
 * synth.c adds the bank base to them as whole u32s, so they are swapped as
 * u32s; the port's AX layer reads them the same way. Returns the number of
 * bytes covered. */
size_t port_swap_ssm_table(uint32_t* table, uint32_t groups);

/* .sem sound-macro file header (AXDriver_8038DA70): four runs of
 * { u32 count; u32 words[count] }. The macro bytecode after the header is
 * left alone. Returns the number of bytes covered. */
size_t port_swap_sem_header(uint32_t* file);

/* Particle banks (`map_ptcl` / `map_texg`, and the two blocks an effect data
 * table points at). They are self-relocating blobs of 32-bit offsets that
 * psInitDataBankLocate() rewrites into pointers, so every word it treats as an
 * integer must be native first. Converts both banks in place; either may be
 * NULL. Call once per bank, immediately before the relocation. */
void port_swap_ptcl_banks(void* cmd_bank, void* tex_bank, void* form_bank);

/* Fighter animation tables (ftData::xC / ::x14): arrays of
 * { char* name; s32 x4; s32 x8; CmdUnion* cmds; s32 flags; u32 x14 }
 * whose length lives in a table in the code rather than in the archive, so the
 * schema walker cannot reach them. Swaps the four scalar words of each entry
 * and leaves the two relocated pointers alone. */
void port_swap_ft_anim_entries(void* entries, uint32_t count);

/* Per-costume texture-animation id lists (ftData::x8->x8.xC): an array of
 * `costumes` pointers, each to `count` u16 ids. Their lengths come from the
 * costume table in the code, so the schema walker cannot reach them. Call once
 * per archive load: the caller owns that, since converting twice would undo it. */
void port_swap_ft_costume_tobjs(void* ftdata_x8, uint32_t costumes);

/* Per-costume model visibility tables (FtPartsDesc::vis_table): rows of four
 * pointers, each to `model_num` { count, entries } pairs, each entry itself a
 * { count, u8* } pair. Rows share tables, so each distinct one is converted
 * once within the call. Call once per archive load, as above. */
void port_swap_ft_parts_vis(void* ftdata_x8, uint32_t costumes);

#endif
