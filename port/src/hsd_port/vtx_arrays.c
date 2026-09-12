#include "vtx_arrays.h"

#include <dolphin/gx/GXCommandList.h>
#include <stdlib.h>
#include <string.h>

static u32 comp_type_size(GXAttr attr, GXCompType type)
{
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        switch (type) {
        case GX_RGB565: return 2;
        case GX_RGB8: return 3;
        case GX_RGBX8: return 4;
        case GX_RGBA4: return 2;
        case GX_RGBA6: return 3;
        case GX_RGBA8: return 4;
        default: return 4;
        }
    }
    switch (type) {
    case GX_U8:
    case GX_S8: return 1;
    case GX_U16:
    case GX_S16: return 2;
    case GX_F32: return 4;
    default: return 4;
    }
}

static u32 comp_cnt_count(GXAttr attr, GXCompCnt cnt)
{
    switch (attr) {
    case GX_VA_POS: return cnt == GX_POS_XY ? 2 : 3;
    case GX_VA_NRM:
    case GX_VA_NBT: return cnt == GX_NRM_XYZ ? 3 : 9;
    case GX_VA_CLR0:
    case GX_VA_CLR1: return 1;
    default: /* texture coordinates */ return cnt == GX_TEX_S ? 1 : 2;
    }
}

/* Bytes one vertex occupies in the display list for this attribute, and how
 * many indices it carries (0 for direct data, 1 or 3 for indexed). */
static u32 attr_dl_size(const HSD_VtxDescList* desc, u32* index_count)
{
    *index_count = 0;
    if (desc->attr >= GX_VA_PNMTXIDX && desc->attr <= GX_VA_TEX7MTXIDX) {
        return 1; /* matrix indices are always one direct byte */
    }
    switch (desc->attr_type) {
    case GX_NONE: return 0;
    case GX_DIRECT: return comp_type_size(desc->attr, desc->comp_type) * comp_cnt_count(desc->attr, desc->comp_cnt);
    case GX_INDEX8:
    case GX_INDEX16: {
        u32 per_index = desc->attr_type == GX_INDEX8 ? 1 : 2;
        *index_count = (desc->attr == GX_VA_NRM && desc->comp_cnt == GX_NRM_NBT3) ? 3 : 1;
        return per_index * *index_count;
    }
    default: return 0;
    }
}

void port_dl_array_sizes(const HSD_VtxDescList* verts, const u8* dl, u32 dl_len, u32 sizes[GX_VA_MAX_ATTR])
{
    u32 max_index[GX_VA_MAX_ATTR];
    int seen[GX_VA_MAX_ATTR];
    u32 vtx_size = 0;
    memset(max_index, 0, sizeof max_index);
    memset(seen, 0, sizeof seen);
    memset(sizes, 0, sizeof(u32) * GX_VA_MAX_ATTR);

    for (const HSD_VtxDescList* d = verts; d->attr != GX_VA_NULL; d++) {
        u32 nidx;
        vtx_size += attr_dl_size(d, &nidx);
    }
    if (vtx_size == 0) {
        return;
    }

    u32 pos = 0;
    while (pos < dl_len) {
        u8 op = dl[pos];
        if (op == GX_NOP) {
            break; /* HSD pads display lists to 32 bytes with NOPs */
        }
        u8 opcode = op & GX_OPCODE_MASK;
        if (opcode < GX_QUADS) {
            /* Non-primitive commands never appear in HSD display lists; stop
             * rather than misparse a register load as vertices. */
            break;
        }
        if (pos + 3 > dl_len) {
            break;
        }
        u32 nverts = (u32) dl[pos + 1] << 8 | dl[pos + 2];
        pos += 3;
        if (pos + nverts * vtx_size > dl_len) {
            nverts = (dl_len - pos) / vtx_size; /* truncated list: use what is there */
        }
        for (u32 v = 0; v < nverts; v++) {
            for (const HSD_VtxDescList* d = verts; d->attr != GX_VA_NULL; d++) {
                u32 nidx;
                u32 size = attr_dl_size(d, &nidx);
                if (nidx != 0 && d->attr < GX_VA_MAX_ATTR) {
                    u32 per_index = size / nidx;
                    for (u32 k = 0; k < nidx; k++) {
                        const u8* p = dl + pos + k * per_index;
                        u32 idx = per_index == 1 ? p[0] : ((u32) p[0] << 8 | p[1]);
                        if (!seen[d->attr] || idx > max_index[d->attr]) {
                            max_index[d->attr] = idx;
                            seen[d->attr] = 1;
                        }
                    }
                }
                pos += size;
            }
        }
    }

    for (const HSD_VtxDescList* d = verts; d->attr != GX_VA_NULL; d++) {
        if (d->attr < GX_VA_MAX_ATTR && seen[d->attr]) {
            sizes[d->attr] = (max_index[d->attr] + 1) * d->stride;
        }
    }
}

u32 port_pobj_array_size(HSD_PObj* pobj, const HSD_VtxDescList* desc)
{
    if (pobj->port_array_sizes == NULL) {
        pobj->port_array_sizes = calloc(GX_VA_MAX_ATTR, sizeof(u32));
        if (pobj->port_array_sizes == NULL) {
            return 0;
        }
        port_dl_array_sizes(pobj->verts, pobj->display, (u32) pobj->n_display << 5, pobj->port_array_sizes);
    }
    return desc->attr < GX_VA_MAX_ATTR ? pobj->port_array_sizes[desc->attr] : 0;
}

void port_pobj_free_array_sizes(HSD_PObj* pobj)
{
    free(pobj->port_array_sizes);
    pobj->port_array_sizes = NULL;
}
