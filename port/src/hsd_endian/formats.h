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
 * each 0x40-byte block is the DSP parameter blocks for one voice, back to
 * back: { AXPBADDR; AXPBADPCM; AXPBADPCMLOOP; u16 pad } (synth.c reads it
 * through `struct foo` at +0x10, +0x20 and +0x48 of the node the block is
 * copied into). Every field is a u16, including the hi/lo halves of the three
 * addresses, which synth.c relocates through PB_ADD32. Returns the number of
 * bytes covered. */
size_t port_swap_ssm_table(uint32_t* table, uint32_t groups);

/* .hps music stream. The 0x80-byte file header is { "HALPST\0\0"; u32 rate;
 * u32 channels; per channel { AXPBADDR; AXPBADPCM } } and each 0x20-byte
 * block header is { u32 length; u32 end; s32 next (-1 at the end); per
 * channel { AXPBADPCMLOOP; u16 pad } }. The parameter blocks are u16 fields. */
void port_swap_hps_file_header(uint32_t* header);
void port_swap_hps_block_header(uint32_t* header);

/* .sem sound-macro file header (AXDriver_8038DA70): four runs of
 * { u32 count; u32 words[count] }. Returns the number of bytes covered; the
 * macro bytecode after it is u32 commands, which the caller swaps as words. */
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

/* Fighter subaction scripts (Fighter_WaitAnimData::xC of the two tables
 * above): bytecode streams of 32-bit words that ftaction.c and lbcommand.c
 * read through bitfield structs, which CodeWarrior packs MSB-first and this
 * build LSB-first. Each stream is walked as the interpreter would -- the
 * opcode is the top 6 bits of the big-endian word, and the fighter opcodes
 * have fixed word counts -- and every word is repacked with the widths of
 * the struct the game reads it through. Subroutine and goto targets are
 * followed; the second word of those commands is a relocated pointer and is
 * left alone. Streams shared between entries, or between the two tables,
 * are converted once, which is why both tables go in one call.
 * `base` is what port_archive_fixup added to the pointer slots (the archive
 * data), so a slot resolves to base + (slot - (u32) base): in the wasm build
 * a slot already is the pointer and base may be NULL; a 64-bit host test
 * passes its buffer. Call once per archive load, with the entries already
 * swapped by port_swap_ft_anim_entries. */
void port_swap_ft_cmd_scripts(const void* base, void* entries_a, uint32_t count_a, void* entries_b,
                              uint32_t count_b);

/* Item scripts (ItemStateDesc::xC_script): the same kind of bytecode with the
 * item command set of itanimlist.c (opcodes 10-25, one of them of variable
 * length). `slots` are the addresses of the relocated script-pointer words
 * (the walk of an ItCo.dat archive collects them from the state rows it
 * visited); `base` is as for port_swap_ft_cmd_scripts. Streams shared between
 * rows are converted once per call. `note_ptr`, when given, is called with
 * every subroutine or goto pointer word left in place, so a coverage walk can
 * count it as reached. */
void port_swap_it_cmd_scripts(const void* base, const void* const* slots, uint32_t nslots,
                              void (*note_ptr)(void* user, const void* slot), void* user);

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
