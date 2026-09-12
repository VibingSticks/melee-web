/* VI functions Aurora does not implement: the retrace callbacks and the XFB
 * bookkeeping the game expects. There is no real retrace; port_vblank() in
 * main_loop.c calls port_vi_retrace() once per virtual frame. */
#include <dolphin/vi.h>

#include "port_game.h"
#include "vi_shim.h"

static VIRetraceCallback g_pre_cb;
static VIRetraceCallback g_post_cb;
static u32 g_retrace_count;
static void* g_next_fb;
static BOOL g_black;

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb)
{
    VIRetraceCallback old = g_pre_cb;
    g_pre_cb = cb;
    return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb)
{
    VIRetraceCallback old = g_post_cb;
    g_post_cb = cb;
    return old;
}

void VIWaitForRetrace(void)
{
    port_vblank();
}

u32 VIGetNextField(void)
{
    return g_retrace_count & 1u; /* alternate fields like an interlaced signal */
}

u32 VIGetDTVStatus(void)
{
    return 0; /* no progressive-scan cable */
}

void VISetNextFrameBuffer(void* fb)
{
    g_next_fb = fb; /* Aurora presents its own framebuffer; the XFB is unused */
}

void VISetBlack(BOOL black)
{
    g_black = black;
}

u16 VIPadFrameBufferWidth(u16 width)
{
    return (u16) ((width + 15) & ~15);
}

void port_vi_retrace(void)
{
    g_retrace_count++;
    if (g_pre_cb != NULL) {
        g_pre_cb(g_retrace_count);
    }
    if (g_post_cb != NULL) {
        g_post_cb(g_retrace_count);
    }
}

u32 port_vi_retrace_count(void)
{
    return g_retrace_count;
}
