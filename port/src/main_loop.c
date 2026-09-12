/* Entry point and per-frame pacing for the web build.
 *
 * The game keeps its original control flow (nested scene loops, busy-waits on
 * the pad queue and on disc loads). The one thing the browser needs is for the
 * wasm to yield regularly, which Asyncify provides at two points:
 *   - port_vblank(): called from the pad-queue wait and VIWaitForRetrace(). It
 *     presents the frame, paces to 60 Hz, runs the VI retrace callbacks (which
 *     fill the pad queue) and starts the next Aurora frame.
 *   - port_yield(): called from other busy-waits (disc loads).
 */
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <dolphin/os.h>
#include <emscripten.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "dvd_web/dvd_web.h"
#include "os_shim/os_alarm.h"
#include "port.h"
#include "port_game.h"
#include "vi_shim.h"

int melee_main(void); /* the game's main(), renamed under TARGET_PC */

/* Aurora's MEM1 block (lib/dolphin/os/OSMemory.cpp). */
extern void* MEM1Start;
extern void* MEM1End;

int port_is_aram_address(unsigned long addr)
{
    return !(addr >= (unsigned long) MEM1Start && addr < (unsigned long) MEM1End);
}

/* ARAM offsets are below ARAM_DEFAULT_SIZE; make sure no main-memory pointer
 * the game compares can be that small by pushing the heap past that range
 * before Aurora allocates MEM1. */
static void reserve_low_heap(void)
{
    void* probe = malloc(16);
    if ((uintptr_t) probe < ARAM_DEFAULT_SIZE) {
        size_t pad = ARAM_DEFAULT_SIZE - (uintptr_t) probe + 4096;
        void* block = malloc(pad);
        port_log("reserved %zu bytes of low heap so MEM1 sits above the ARAM offset range", pad);
        (void) block; /* intentionally leaked */
    }
    free(probe);
}

static bool g_frame_active;
static bool g_exit_requested;
static bool g_paused;
static double g_next_vblank_ms;
static const double kFrameMs = 1000.0 / 60.0;

void port_log(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    emscripten_log(EM_LOG_CONSOLE, "[melee] %s", buf);
}

void port_request_exit(void)
{
    g_exit_requested = true;
}

static void handle_events(void)
{
    const AuroraEvent* e = aurora_update();
    for (; e != NULL && e->type != AURORA_NONE; ++e) {
        switch (e->type) {
        case AURORA_EXIT: g_exit_requested = true; break;
        case AURORA_PAUSED: g_paused = true; break;
        case AURORA_UNPAUSED: g_paused = false; break;
        default: break;
        }
    }
}

static void shutdown_and_exit(void)
{
    port_log("exiting");
    if (g_frame_active) {
        aurora_end_frame();
        g_frame_active = false;
    }
    aurora_shutdown();
    emscripten_force_exit(0);
}

static void begin_frame_blocking(void)
{
    while (!g_frame_active) {
        handle_events();
        if (g_exit_requested) {
            shutdown_and_exit();
        }
        if (!g_paused && aurora_begin_frame()) {
            g_frame_active = true;
        } else {
            emscripten_sleep(16);
        }
    }
}

void port_yield(void)
{
    emscripten_sleep(0);
    port_dvd_pump();
}

void port_vblank(void)
{
    if (g_frame_active) {
        aurora_end_frame();
        g_frame_active = false;
    }
    handle_events();
    if (g_exit_requested) {
        shutdown_and_exit();
    }

    /* Pace to 60 Hz. After a long stall, resynchronise instead of running
     * several frames back to back. */
    double now = emscripten_get_now();
    if (g_next_vblank_ms == 0.0 || now > g_next_vblank_ms + 4 * kFrameMs) {
        g_next_vblank_ms = now;
    }
    while (now < g_next_vblank_ms) {
        double remaining = g_next_vblank_ms - now;
        emscripten_sleep(remaining >= 2.0 ? (unsigned) (remaining - 1.0) : 0);
        now = emscripten_get_now();
    }
    g_next_vblank_ms += kFrameMs;

    port_dvd_pump();
    port_alarm_tick(OSGetTime());
    port_vi_retrace(); /* pad queue, XFB flip bookkeeping */
    begin_frame_blocking();
}

int main(int argc, char** argv)
{
    AuroraConfig cfg = {
        .appName = "Melee",
        .desiredBackend = BACKEND_WEBGPU,
        .msaa = 1,
        .vsync = true,
        .allowCpuAdapter = true,
        .windowWidth = 1280,
        .windowHeight = 960,
        .logLevel = LOG_INFO,
        .mem1Size = MEM1_DEFAULT_SIZE,
        .mem2Size = ARAM_DEFAULT_SIZE,
    };
    reserve_low_heap();
    aurora_initialize(argc, argv, &cfg);
    port_log("Aurora initialized; starting the game");
    begin_frame_blocking();
    int rc = melee_main(); /* never returns on hardware; scene loops run inside */
    port_log("melee_main returned %d", rc);
    shutdown_and_exit();
    return rc;
}
