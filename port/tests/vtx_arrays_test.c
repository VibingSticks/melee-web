#include "check.h"
#include "hsd_port/vtx_arrays.h"

static u8 dl[256];

static void put16(u32 at, u32 v)
{
    dl[at] = (u8) (v >> 8);
    dl[at + 1] = (u8) v;
}

static void run(void)
{
    /* PNMTXIDX (1 byte), POS index16 stride 12, NRM index8 stride 6,
     * CLR0 direct RGBA8 (4 bytes), TEX0 index16 stride 8 -> 1+2+1+4+2 = 10 bytes per vertex */
    HSD_VtxDescList verts[] = {
        { GX_VA_PNMTXIDX, GX_DIRECT, 0, 0, 0, 0, NULL },
        { GX_VA_POS, GX_INDEX16, GX_POS_XYZ, GX_F32, 0, 12, NULL },
        { GX_VA_NRM, GX_INDEX8, GX_NRM_XYZ, GX_S16, 0, 6, NULL },
        { GX_VA_CLR0, GX_DIRECT, GX_CLR_RGBA, GX_RGBA8, 0, 0, NULL },
        { GX_VA_TEX0, GX_INDEX16, GX_TEX_ST, GX_S16, 0, 8, NULL },
        { GX_VA_NULL, 0, 0, 0, 0, 0, NULL },
    };
    memset(dl, 0, sizeof dl);
    u32 p = 0;
    /* triangles, 3 vertices */
    dl[p++] = GX_TRIANGLES | 0; put16(p, 3); p += 2;
    u32 pos_idx[3] = { 5, 0, 2 }, nrm_idx[3] = { 1, 2, 0 }, tex_idx[3] = { 300, 7, 12 };
    for (int v = 0; v < 3; v++) {
        dl[p++] = 0;                 /* mtx index */
        put16(p, pos_idx[v]); p += 2;
        dl[p++] = (u8) nrm_idx[v];
        dl[p] = 1; dl[p + 1] = 2; dl[p + 2] = 3; dl[p + 3] = 4; p += 4; /* color, direct */
        put16(p, tex_idx[v]); p += 2;
    }
    /* triangle strip, 2 vertices, one with a higher position index */
    dl[p++] = GX_TRIANGLESTRIP | 0; put16(p, 2); p += 2;
    for (int v = 0; v < 2; v++) {
        dl[p++] = 0;
        put16(p, v == 1 ? 9 : 1); p += 2;
        dl[p++] = 0;
        p += 4;
        put16(p, 3); p += 2;
    }
    /* NOP padding follows (already zero) */
    u32 len = ((p + 31) / 32) * 32;

    u32 sizes[GX_VA_MAX_ATTR];
    port_dl_array_sizes(verts, dl, len, sizes);
    CHECK_EQ_U32(sizes[GX_VA_POS], (9 + 1) * 12);
    CHECK_EQ_U32(sizes[GX_VA_NRM], (2 + 1) * 6);
    CHECK_EQ_U32(sizes[GX_VA_TEX0], (300 + 1) * 8);
    CHECK_EQ_U32(sizes[GX_VA_CLR0], 0);     /* direct */
    CHECK_EQ_U32(sizes[GX_VA_PNMTXIDX], 0); /* direct */
    CHECK_EQ_U32(sizes[GX_VA_TEX1], 0);     /* absent */

    /* NBT3 normals carry three indices per vertex; the max spans all three */
    HSD_VtxDescList nbt[] = {
        { GX_VA_NRM, GX_INDEX8, GX_NRM_NBT3, GX_S16, 0, 6, NULL },
        { GX_VA_NULL, 0, 0, 0, 0, 0, NULL },
    };
    memset(dl, 0, sizeof dl);
    dl[0] = GX_TRIANGLES; put16(1, 1);
    dl[3] = 4; dl[4] = 40; dl[5] = 2;
    port_dl_array_sizes(nbt, dl, 32, sizes);
    CHECK_EQ_U32(sizes[GX_VA_NRM], (40 + 1) * 6);

    /* an empty or NOP-only list yields zero sizes */
    memset(dl, 0, sizeof dl);
    port_dl_array_sizes(verts, dl, 32, sizes);
    CHECK_EQ_U32(sizes[GX_VA_POS], 0);

    /* a truncated vertex count does not read past the buffer */
    dl[0] = GX_TRIANGLES; put16(1, 1000);
    port_dl_array_sizes(verts, dl, 64, sizes);
    CHECK(sizes[GX_VA_POS] <= 65536u * 12);

    /* per-PObj caching */
    HSD_PObj pobj;
    memset(&pobj, 0, sizeof pobj);
    memset(dl, 0, sizeof dl);
    dl[0] = GX_TRIANGLES; put16(1, 1);
    dl[3] = 0; put16(4, 6); dl[6] = 1; put16(11, 2);
    pobj.verts = verts;
    pobj.display = dl;
    pobj.n_display = 1; /* 32 bytes */
    CHECK_EQ_U32(port_pobj_array_size(&pobj, &verts[1]), 7 * 12);
    CHECK(pobj.port_array_sizes != NULL);
    dl[4] = 0; dl[5] = 1; /* changing the list afterwards must not change the cached answer */
    CHECK_EQ_U32(port_pobj_array_size(&pobj, &verts[1]), 7 * 12);
    CHECK_EQ_U32(port_pobj_array_size(&pobj, &verts[3]), 0);
    port_pobj_free_array_sizes(&pobj);
    CHECK(pobj.port_array_sizes == NULL);
}

TEST_MAIN(run)
