/* A portable decoder for the movie frames Melee plays (.mth movies, .thp
 * stills): baseline JPEG, YUV 4:2:0, entropy data without 0xFF00 stuffing.
 *
 * The SDK's own THP decoder does its Huffman decoding and inverse DCTs in
 * PowerPC assembly, so on this target the game's THPVideoDecode entry points
 * call this instead (libs/dolphin/src/dolphin/thp/THPDec.c, TARGET_PC). */
#ifndef PORT_THP_JPEG_H
#define PORT_THP_JPEG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decode one frame starting at its SOI marker into planar Y (w x h) and U, V
 * (w/2 x h/2) buffers. `avail` bounds the read. Returns 0 on success, else a
 * nonzero reason; the planes may then hold a partly decoded picture. */
int port_thp_decode(const uint8_t* data, size_t avail, uint8_t* y, uint8_t* u, uint8_t* v, int w, int h);

/* The frame's size from its SOF0 marker, or 0 when there is none. */
int port_thp_frame_size(const uint8_t* data, size_t avail, int* w, int* h);

/* Copy a w x h plane into GX I8 tile order (8x4 texel tiles, row-major). */
void port_thp_tile_i8(const uint8_t* src, uint8_t* dst, int w, int h);

#ifdef __cplusplus
}
#endif

#endif
