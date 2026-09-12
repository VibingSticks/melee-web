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

#endif
