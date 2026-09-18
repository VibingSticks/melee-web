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
#include <dolphin/gx/GXAurora.h> /* AuroraSetViewportPolicy */
#include <dolphin/os.h>
#include <emscripten.h>
#include <emscripten/heap.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h> /* memset, for the frame-time accumulator */
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>

#include "ax_hle/ax_hle.h"
#include "dvd_web/dvd_web.h"
#include "font_dol.h"
#include "os_shim/os_alarm.h"
#include "port.h"
#include "pad_web.h"
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
static unsigned* g_yields_since_frame;
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
    unsigned waits = 0;
    while (!g_frame_active) {
        handle_events();
        if (g_exit_requested) {
            shutdown_and_exit();
        }
        if (!g_paused && aurora_begin_frame()) {
            g_frame_active = true;
        } else {
            if (++waits % 60 == 1) {
                port_log("waiting for a frame: %s", g_paused ? "paused by the window system" : "aurora_begin_frame failed");
            }
            emscripten_sleep(16);
        }
    }
}

void port_yield(void)
{
    /* A wait loop that never reaches a frame means something it is waiting for
     * never completes; say so rather than hanging silently. */
    static unsigned s_since_frame;
    if (++s_since_frame % 20000 == 0) {
        port_log("stuck: %u yields without a frame (%u disc reads in flight)", s_since_frame, port_dvd_pending());
    }
    g_yields_since_frame = &s_since_frame;
    emscripten_sleep(0);
    port_dvd_pump();
    port_ax_pump(0); /* keep audio flowing while the game blocks on a load */
}

/* Where a frame's milliseconds actually go.
 *
 * The game runs its own scene loops and calls back in here once per frame, so
 * "game" is the time spent outside this function -- simulation, collision,
 * display-list building -- and the rest is the port's own per-frame work.
 * Printed once a second next to the heap line. `pace` is the deliberate wait
 * to hit 60 Hz: while it is large the frame has headroom, and when it falls to
 * zero the frame is late and the other columns say why. */
static struct {
    double game, present, events, pace, dvd, alarm, vi, audio, begin;
    double worst;
    double window_start; /* wall clock when this reporting window opened */
    unsigned frames;
} g_prof;
static double g_prof_left; /* when the previous port_vblank returned */

static void prof_report(unsigned frame)
{
    if (g_prof.frames == 0) {
        return;
    }
    double n = g_prof.frames;
    /* Measured against the wall clock over the reporting window, so this is
     * the rate actually reaching the screen -- not the frame counter divided
     * by an assumed 60. */
    double now = emscripten_get_now();
    double fps = g_prof.window_start > 0.0 && now > g_prof.window_start ? n * 1000.0 / (now - g_prof.window_start) : 0.0;
    port_log("frame %u | %.1f fps (%.1f ms/frame) | game %.1f present %.1f events %.1f pace %.1f "
             "dvd %.1f alarm %.1f vi %.1f audio %.1f begin %.1f | worst %.1f",
             frame, fps, fps > 0.0 ? 1000.0 / fps : 0.0, g_prof.game / n, g_prof.present / n, g_prof.events / n,
             g_prof.pace / n, g_prof.dvd / n, g_prof.alarm / n, g_prof.vi / n, g_prof.audio / n, g_prof.begin / n,
             g_prof.worst);
    memset(&g_prof, 0, sizeof(g_prof));
    g_prof.window_start = now;
}

void port_vblank(void)
{
    static bool s_devices_bound;
    if (!s_devices_bound) { /* after the game's PADInit, which resets Aurora's bindings */
        s_devices_bound = true;
        port_pad_install_keyboard();
        port_ax_init();
    }
    double t_enter = emscripten_get_now();
    if (g_prof.window_start == 0.0) {
        g_prof.window_start = t_enter;
    }
    if (g_prof_left != 0.0) {
        g_prof.game += t_enter - g_prof_left;
    }

    if (g_frame_active) {
        aurora_end_frame();
        g_frame_active = false;
    }
    double t_present = emscripten_get_now();
    g_prof.present += t_present - t_enter;

    handle_events();
    if (g_exit_requested) {
        shutdown_and_exit();
    }
    double t_events = emscripten_get_now();
    g_prof.events += t_events - t_present;

    /* Pace to 60 Hz. After a long stall, resynchronise instead of running
     * several frames back to back. */
    double now = t_events;
    if (g_next_vblank_ms == 0.0 || now > g_next_vblank_ms + 4 * kFrameMs) {
        g_next_vblank_ms = now;
    }
    while (now < g_next_vblank_ms) {
        double remaining = g_next_vblank_ms - now;
        emscripten_sleep(remaining >= 2.0 ? (unsigned) (remaining - 1.0) : 0);
        now = emscripten_get_now();
    }
    g_next_vblank_ms += kFrameMs;
    double t_pace = emscripten_get_now();
    g_prof.pace += t_pace - t_events;

    if (g_yields_since_frame != NULL) {
        *g_yields_since_frame = 0;
    }
    port_dvd_pump();
    double t_dvd = emscripten_get_now();
    g_prof.dvd += t_dvd - t_pace;
    /* Alarms run on a virtual clock that advances exactly one frame per
     * vblank, not on the wall clock. The game's pad queue is filled only by a
     * periodic alarm of about 1/60 s, and gmscene's wait loop presents another
     * frame for every vblank that produces no pad sample. Tying the alarm grid
     * to the wall clock let sleep jitter decide whether a given vblank saw a
     * fire, so the simulation ran below 60 Hz with duplicate frames. One tick
     * per frame makes that alarm fire exactly once per vblank. */
    {
        static OSTime s_virtual_time;
        if (s_virtual_time == 0) {
            s_virtual_time = OSGetTime();
        } else {
            s_virtual_time += (OSTime) (OS_TIMER_CLOCK / 60);
        }
        port_alarm_tick(s_virtual_time);
    }
    double t_alarm = emscripten_get_now();
    g_prof.alarm += t_alarm - t_dvd;

    port_vi_retrace(); /* pad queue, XFB flip bookkeeping */
    double t_vi = emscripten_get_now();
    g_prof.vi += t_vi - t_alarm;

    port_ax_pump(1); /* AX frames due this video frame, mixed and queued */
    double t_audio = emscripten_get_now();
    g_prof.audio += t_audio - t_vi;

    begin_frame_blocking();
    double t_begin = emscripten_get_now();
    g_prof.begin += t_begin - t_audio;
    g_prof.frames++;
    {
        /* The frame's real cost: everything but the deliberate wait. A value
         * above 16.7 means this frame could not have hit 60 Hz. */
        double busy = (t_begin - t_enter) - (t_pace - t_events);
        if (g_prof_left != 0.0) {
            busy += t_enter - g_prof_left;
        }
        if (busy > g_prof.worst) {
            g_prof.worst = busy;
        }
    }

    /* Once a second: frame count and wasm heap size, to spot runaway growth. */
    static unsigned s_frames;
    if (++s_frames % 60 == 0) {
        struct mallinfo mi = mallinfo();
        port_log("frame %u heap=%u MB malloc=%u MB", s_frames, (unsigned) (emscripten_get_heap_size() >> 20),
                 (unsigned) ((unsigned) mi.uordblks >> 20));
        prof_report(s_frames);
    }
    {
        /* The wasm heap only ever grows, so report every step: a jump that is
         * not matched by malloc came from somewhere other than the C heap. */
        static size_t s_heap;
        size_t now = emscripten_get_heap_size();
        if (now != s_heap) {
            struct mallinfo mi = mallinfo();
            port_log("heap grew %u -> %u MB at frame %u (malloc in use %u, free %u, mmapped %u MB)",
                     (unsigned) (s_heap >> 20), (unsigned) (now >> 20), s_frames,
                     (unsigned) ((unsigned) mi.uordblks >> 20), (unsigned) ((unsigned) mi.fordblks >> 20),
                     (unsigned) ((unsigned) mi.hblkhd >> 20));
            s_heap = now;
        }
    }

    g_prof_left = emscripten_get_now();
}

int main(int argc, char** argv)
{
    /* Render size. The game draws 640x480; the default renders at twice that
     * and lets the page scale it down, which is free on a real GPU and is not
     * free on a software rasteriser. ?res=WxH overrides it -- the frame-time
     * line printed once a second says whether it helped. */
    int render_w = emscripten_run_script_int("(typeof Module !== 'undefined' && Module.renderWidth) | 0");
    int render_h = emscripten_run_script_int("(typeof Module !== 'undefined' && Module.renderHeight) | 0");
    if (render_w < 256 || render_h < 192) {
        render_w = 1280;
        render_h = 960;
    }
    port_log("render size %dx%d", render_w, render_h);

    AuroraConfig cfg = {
        .appName = "Melee",
        .desiredBackend = BACKEND_WEBGPU,
        .msaa = 1,
        .vsync = true,
        .allowCpuAdapter = true,
        .windowWidth = render_w,
        .windowHeight = render_h,
        .logLevel = LOG_INFO,
        .mem1Size = MEM1_DEFAULT_SIZE,
        .mem2Size = ARAM_DEFAULT_SIZE,
        /* boot.js sets Module.forceCompatProfile from ?gpu=compat|noimm|nostorage (see the
         * WebGL2 fallback spec); 0 lets Aurora pick from the adapter's limits. */
        .forceCompatProfile = (uint32_t) emscripten_run_script_int(
            "(typeof Module !== 'undefined' && Module.forceCompatProfile) | 0"),
    };
    reserve_low_heap();
    aurora_initialize(argc, argv, &cfg);
    /* SDL creates the window resizable, and its resize handler sets the canvas
     * drawing buffer to whatever CSS size the page gives it. Aurora then makes
     * the framebuffer match, and scales x and y independently -- so the page's
     * object-fit letterboxing stops doing anything the moment the window is
     * resized, zoomed or made fullscreen. Ask Aurora to keep the logical 4:3
     * aspect and letterbox in the present pass instead, which holds at any
     * canvas size. */
    AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);
    port_font_load_from_dol();
    port_log("Aurora initialized; starting the game");
    begin_frame_blocking();
    int rc = melee_main(); /* never returns on hardware; scene loops run inside */
    port_log("melee_main returned %d", rc);
    shutdown_and_exit();
    return rc;
}
