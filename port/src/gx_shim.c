/* GX and PAD entry points the game uses that Aurora leaves out. */
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <string.h>

void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, f32 projmtx[4][4])
{
    /* Fog range adjustment corrects a hardware artifact; Aurora's fog needs none. */
    (void) width;
    (void) projmtx;
    memset(table, 0, sizeof *table);
}

void GXSetTevClampMode(int stage, int mode)
{
    (void) stage;
    (void) mode; /* only GX_TC_LINEAR is used by Melee; Aurora's TEV clamps linearly */
}

void GXWaitDrawDone(void)
{
    /* Nothing is asynchronous; run the draw-done callback so HSD's XFB state advances. */
    GXDrawDone();
}

void PADSetSamplingRate(u32 msec)
{
    (void) msec;
}
