/* Vertex array sizes for Aurora's GXSetArray.
 *
 * The GameCube GXSetArray only takes a base pointer and stride; Aurora's takes
 * the array's byte size because it uploads the array to the GPU. HSD stores no
 * array lengths, but a PObj's display list only ever references indices below
 * some maximum per attribute, so the required size is (max_index + 1) * stride.
 * The scan runs once per PObj and the result is cached in the PObj (a
 * TARGET_PC-only field, see pobj.h).
 *
 * This file is compiled as part of the game library, with the game's headers
 * and its 4-byte bool. */
#ifndef PORT_HSD_PORT_VTX_ARRAYS_H
#define PORT_HSD_PORT_VTX_ARRAYS_H

#include <sysdolphin/baselib/pobj.h>

/* Scan a GX display list of `dl_len` bytes whose vertices follow `verts`, and
 * write for every attribute the number of array bytes referenced (0 for
 * attributes that are direct or absent). `sizes` has GX_VA_MAX_ATTR entries. */
void port_dl_array_sizes(const HSD_VtxDescList* verts, const u8* dl, u32 dl_len, u32 sizes[GX_VA_MAX_ATTR]);

/* Byte size to pass to GXSetArray for `desc` (one of pobj->verts), computed on
 * first use from pobj->display and cached in the PObj. */
u32 port_pobj_array_size(HSD_PObj* pobj, const HSD_VtxDescList* desc);

/* Release the cache; called from PObjRelease. */
void port_pobj_free_array_sizes(HSD_PObj* pobj);

#endif
