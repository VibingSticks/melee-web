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
#include <stdbool.h>
#include <aurora/gfx.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_stdinc.h>
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

#include "save_web.h"
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

/* boot.js's loading screen: shows done/total, hides when finished is set, and
 * returns 1 once the player has pressed Skip (web/js/imports.js). */
extern int port_preload_progress(int done, int total, int finished);

/* Compile the pipeline seed before the game starts, as Dolphin's "Compile
 * Shaders Before Starting" does. In the background, 2 at a time and paused
 * whenever a frame runs late, a slow GPU (about 400 ms a pipeline on a
 * Chromebook's) never finished it, and every new effect stalled the game the
 * first time it drew. With nothing else running, let more compile at once.
 * Skip (or ?preload=off) leaves the rest to the background warm-up. */
static void preload_pipelines(void)
{
    size_t total = aurora_pipeline_seed_pending();
    if (total == 0 ||
        emscripten_run_script_int("(typeof Module !== 'undefined' && Module.preload === 'off') | 0")) {
        return;
    }
    double t0 = emscripten_get_now();
    int max_in_flight = emscripten_run_script_int("(typeof Module !== 'undefined' && Module.preloadInFlight) | 0");
    aurora_pipeline_seed_set_max_in_flight(max_in_flight > 0 ? (size_t) max_in_flight : 16);
    double last_ui = 0.0;
    int skipped = 0;
    size_t pending;
    while ((pending = aurora_pipeline_seed_pending()) > 0) {
        aurora_pipeline_seed_pump();
        double now = emscripten_get_now();
        if (now - last_ui >= 50.0) { /* the page's progress bar: a few times a second is plenty */
            last_ui = now;
            if (port_preload_progress((int) (total - pending), (int) total, 0)) {
                skipped = 1;
                break;
            }
        }
        /* The compile callbacks run from the event loop. A short sleep with
         * many compiles in flight: 16 ms between checks with 4 in flight
         * capped the rate, and yielding in a tight loop spent a fifth of the
         * main thread spinning, which a 2-core machine's GPU process needs. */
        emscripten_sleep(4);
    }
    aurora_pipeline_seed_set_max_in_flight(2);
    port_preload_progress((int) (total - aurora_pipeline_seed_pending()), (int) total, 1);
    port_log("pipeline preload: %u of %u compiled in %.1f s%s", (unsigned) (total - aurora_pipeline_seed_pending()),
             (unsigned) total, (emscripten_get_now() - t0) / 1000.0,
             skipped ? " (skipped; the rest compile in the background)" : "");
}

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
/* Sanitizer builds (port/build/web-asan): keep running after a report so one
 * run lists every bad access, not only the first. Needs -fsanitize-recover. */
const char* __asan_default_options(void)
{
    return "halt_on_error=0:print_stats=0:malloc_context_size=12:quarantine_size_mb=8";
}
#endif
#endif

/* One turn of the browser's event loop (port/web/js/imports.js). Unlike
 * emscripten_sleep(0) this is not a timer, so it is not subject to the 4 ms
 * minimum Chrome applies to nested setTimeout -- which every Asyncify wake-up
 * is. Use it for "let the browser run" waits; use emscripten_sleep only when
 * an actual delay is wanted. */
extern void port_yield_browser(void);

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
/* Frame skip: how many missed vblanks a late frame may make up before the
 * next draw (0 turns it off; ?frameskip=off). The clock and its alarms catch
 * up all of them, as the GameCube's keep firing through a slow frame, so
 * alarm-driven things such as the movie player stay in time with the music.
 * Game logic is bounded by the pad queue instead: it holds 5 samples
 * (gmmain.c HSD_PadInit) and merges the rest, as on the console. */
static int g_max_catchup = 14; /* just under the 250 ms resync below */
static unsigned g_ticks;          /* vblanks the clock advanced, per report */
static unsigned g_merged_samples; /* of those, pad samples merged into a full queue */

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

/* Time the game spends parked in port_yield (waiting on the browser) rather
 * than running its own code. Reset every vblank; the spike line below reads
 * it, and lbfile's per-load timing reads it through port_yield_count/ms. */
static struct {
    unsigned count;
    double sleep_ms; /* inside port_yield_browser: the browser's turn */
    double pump_ms;  /* the disc and audio pumps that follow it */
} g_yield;

void port_yield(void)
{
    /* A wait loop that never reaches a frame means something it is waiting for
     * never completes; say so rather than hanging silently. */
    static unsigned s_since_frame;
    if (++s_since_frame % 20000 == 0) {
        port_log("stuck: %u yields without a frame (%u disc reads in flight)", s_since_frame, port_dvd_pending());
    }
    g_yields_since_frame = &s_since_frame;
    double t0 = emscripten_get_now();
    port_yield_browser();
    double t1 = emscripten_get_now();
    port_dvd_pump();
    port_ax_pump(0); /* keep audio flowing while the game blocks on a load */
    double t2 = emscripten_get_now();
    g_yield.count++;
    g_yield.sleep_ms += t1 - t0;
    g_yield.pump_ms += t2 - t1;
}

unsigned port_yield_count(void) { return g_yield.count; }
double port_yield_ms(void) { return g_yield.sleep_ms + g_yield.pump_ms; }

/* Where a frame's milliseconds actually go.
 *
 * The game runs its own scene loops and calls back in here once per frame, so
 * "game" is the time spent outside this function -- simulation, collision,
 * display-list building -- and the rest is the port's own per-frame work.
 * Printed once a second next to the heap line. `pace` is the deliberate wait
 * to hit 60 Hz: while it is large the frame has headroom, and when it falls to
 * zero the frame is late and the other columns say why. */
static struct {
    double game, present, events, pace, browser, dvd, alarm, vi, audio, begin;
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
    port_log("frame %u | %.1f fps (%.1f ms/frame) | game %.1f present %.1f events %.1f pace %.1f browser %.1f "
             "dvd %.1f alarm %.1f vi %.1f audio %.1f begin %.1f | worst %.1f | game speed %.0f%% (%u skipped) | "
             "audio dropped %u ms",
             frame, fps, fps > 0.0 ? 1000.0 / fps : 0.0, g_prof.game / n, g_prof.present / n, g_prof.events / n,
             g_prof.pace / n, g_prof.browser / n, g_prof.dvd / n, g_prof.alarm / n, g_prof.vi / n, g_prof.audio / n,
             g_prof.begin / n, g_prof.worst,
             now > g_prof.window_start
                 ? (g_ticks - g_merged_samples) * 1000.0 / (now - g_prof.window_start) / 60.0 * 100.0
                 : 0.0,
             g_ticks - g_merged_samples > n ? (unsigned) (g_ticks - g_merged_samples - n) : 0u, port_ax_take_dry_ms());
    g_ticks = 0;
    g_merged_samples = 0;
    {
        /* What the last frame asked of the GPU: on a slow machine "present"
         * scales with the draw count (each draw is several WebGPU calls, and
         * each call crosses from wasm into JavaScript). */
        const AuroraStats* st = aurora_get_stats();
        if (st != NULL) {
            port_log("gpu: %u draws (%u merged), %u KB verts, %u KB indices, %u KB uniforms, %u KB storage, "
                     "%u KB texture uploads, %u MB textures live, %u pipelines created, warm-up left %u",
                     st->drawCallCount, st->mergedDrawCallCount, st->lastVertSize / 1024, st->lastIndexSize / 1024,
                     st->lastUniformSize / 1024, st->lastStorageSize / 1024, st->lastTextureUploadSize / 1024,
                     (unsigned) (aurora_live_texture_bytes() >> 20), st->createdPipelines,
                     (unsigned) aurora_pipeline_seed_pending());
        }
    }
    memset(&g_prof, 0, sizeof(g_prof));
    g_prof.window_start = now;
}

/* The game's scene state, for the browser tests and for the log.
 *
 * Melee runs a two-level state machine: a "game mode" (title, menu, VS, and so
 * on) and an index within it. Declared here rather than including the game's
 * header, which drags in enough of the decomp to disturb this file's include
 * order. */
extern unsigned char gm_GetCurrentGameMode(void);
extern unsigned char gm_GetCurrentSceneIndex(void);

/* mode in the high byte, scene index in the low one. */
EMSCRIPTEN_KEEPALIVE int port_scene_state(void)
{
    return ((int) gm_GetCurrentGameMode() << 8) | gm_GetCurrentSceneIndex();
}

/* The main menu's cursor (MenuFlow mn_804A04F0, src/melee/mn/mnmain.h).
 *
 * Read as bytes at documented offsets rather than through the game's header,
 * for the same include-order reason as the scene getters above:
 *   +0 cur_menu, +1 prev_menu, +2 hovered_selection (u16), +4 confirmed.
 * This is live runtime state, so it is in the host's byte order, not the
 * disc's. */
extern unsigned char mn_804A04F0[];

/* cur_menu in bits 24-31, prev_menu in 16-23, hovered selection in 0-15. */
EMSCRIPTEN_KEEPALIVE int port_menu_state(void)
{
    const unsigned char* m = mn_804A04F0;
    unsigned short hovered;
    memcpy(&hovered, m + 2, sizeof hovered);
    return ((int) m[0] << 24) | ((int) m[1] << 16) | hovered;
}

/* Frames completed since boot. A screen that is not advancing may be waiting
 * for input or may be wedged; the difference is whether this keeps moving. */
static unsigned g_frame_count;

EMSCRIPTEN_KEEPALIVE int port_frame_count(void)
{
    return (int) g_frame_count;
}

/* The character-select screen's player slots (CssSubStruct gm_80473814,
 * src/melee/gm/types.h). Bytes at documented offsets, as above:
 *   +0x006 stage_id (s16)
 *   +0x014 saved_players[4], each 0x24 bytes, ckind at +0 and slot_type at +1
 * slot_type is Gm_PKind: 0 human, 1 cpu, 2 demo, 3 none, 4 boss. */
extern unsigned char gm_80473814[];

/* slot_type in bits 8-15, ckind in bits 0-7 (0xFF for an empty slot). */
EMSCRIPTEN_KEEPALIVE int port_css_slot(int i)
{
    if (i < 0 || i > 3) {
        return -1;
    }
    const unsigned char* p = gm_80473814 + 0x14 + (unsigned) i * 0x24;
    return ((int) p[1] << 8) | p[0];
}

EMSCRIPTEN_KEEPALIVE int port_css_stage(void)
{
    short id;
    memcpy(&id, gm_80473814 + 6, sizeof id);
    return id;
}

/* Test hook: mark All-Star as unlocked in the loaded save (gmMainLib_8015EDE4
 * sets the flag mn_80229938's menu check reads). The Regular Match menu will
 * not put its cursor on a locked All-Star, so a fresh save cannot reach the
 * mode at all; the browser tests call this once the main menu is up. */
extern void gmMainLib_8015EDE4(void);

EMSCRIPTEN_KEEPALIVE void port_unlock_allstar(void)
{
    gmMainLib_8015EDE4();
}

/* Say so whenever the game moves. A screen that never arrives, or one that
 * arrives and then stops, is the difference between "it went black" and a
 * scene number to go and look at. */
static void report_scene_change(void)
{
    static int s_last = -1;
    static unsigned s_frames_here;
    int now = port_scene_state();

    if (now != s_last) {
        port_log("scene: mode %d index %d (previous mode %d index %d after %u frames)", now >> 8, now & 0xFF,
                 s_last >> 8, s_last & 0xFF, s_frames_here);
        s_last = now;
        s_frames_here = 0;
        /* Between scenes, when this session has met pipelines it had not
         * compiled before, have the page add them to the saved list the next
         * boot's preload compiles (boot.js savePipelines). */
        extern void port_pipelines_save(void);
        static uint32_t s_created_at_save;
        const AuroraStats* st = aurora_get_stats();
        if (st != NULL && st->createdPipelines > s_created_at_save) {
            s_created_at_save = st->createdPipelines;
            port_pipelines_save();
        }
    } else {
        s_frames_here++;
    }
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
    int catchup = 0;
    /* A stall this long is a load, not a slow draw: resynchronise instead of
     * running the missed frames. */
    if (g_next_vblank_ms == 0.0 || now > g_next_vblank_ms + 250.0) {
        g_next_vblank_ms = now;
    } else if (now >= g_next_vblank_ms + kFrameMs) {
        /* Late by whole frames. The GameCube never slows the game down for a
         * slow draw: its pad alarm keeps queueing a sample every 1/60 s, and
         * the scene loop runs one logic frame per queued sample before it
         * draws again (gmscene.c). Give the loop those samples too, so a
         * machine that can only draw 20 frames a second still plays at full
         * speed, skipping draws, instead of in slow motion. */
        catchup = (int) ((now - g_next_vblank_ms) / kFrameMs);
        if (catchup > g_max_catchup) {
            catchup = g_max_catchup;
        }
        g_next_vblank_ms += catchup * kFrameMs;
        if (now > g_next_vblank_ms + kFrameMs) {
            g_next_vblank_ms = now; /* beyond the cap: drop the rest */
        }
    }
    bool yielded = false;
    while (now < g_next_vblank_ms) {
        double remaining = g_next_vblank_ms - now;
        if (remaining >= 2.0) {
            emscripten_sleep((unsigned) (remaining - 1.0));
        } else {
            port_yield_browser(); /* a timer here would overshoot by 4 ms */
        }
        yielded = true;
        now = emscripten_get_now();
    }
    g_next_vblank_ms += kFrameMs;
    double t_pace = emscripten_get_now();
    g_prof.pace += t_pace - t_events;

    /* A late frame has no wait to take, but the browser still needs the turn:
     * the canvas is only composited, and the renderer's map-completion
     * callbacks only run, when this task ends. Running late frames back to
     * back put one frame in five on screen and stalled every fifth frame about
     * 60 ms in aurora_begin_frame, once all five staging buffers were waiting
     * on callbacks that had never had a chance to run. */
    if (!yielded) {
        port_yield_browser();
    }
    double t_browser = emscripten_get_now();
    g_prof.browser += t_browser - t_pace;

    if (g_yields_since_frame != NULL) {
        *g_yields_since_frame = 0;
    }
    port_dvd_pump();
    double t_dvd = emscripten_get_now();
    g_prof.dvd += t_dvd - t_browser;
    /* Alarms run on a virtual clock that advances exactly one frame per
     * vblank, not on the wall clock. The game's pad queue is filled only by a
     * periodic alarm of about 1/60 s, and gmscene's wait loop presents another
     * frame for every vblank that produces no pad sample. Tying the alarm grid
     * to the wall clock let sleep jitter decide whether a given vblank saw a
     * fire, so the simulation ran below 60 Hz with duplicate frames. One tick
     * per frame makes that alarm fire exactly once per vblank. */
    {
        static OSTime s_virtual_time;
        for (int tick = 0; tick <= catchup; tick++) {
            if (port_pad_queue_full()) {
                g_merged_samples++; /* this vblank's pad sample adds no logic frame */
            }
            g_ticks++;
            if (s_virtual_time == 0) {
                s_virtual_time = OSGetTime();
            } else {
                s_virtual_time += (OSTime) (OS_TIMER_CLOCK / 60);
            }
            port_alarm_tick(s_virtual_time);
        }
    }
    double t_alarm = emscripten_get_now();
    g_prof.alarm += t_alarm - t_dvd;

    port_vi_retrace(); /* pad queue, XFB flip bookkeeping */
    double t_vi = emscripten_get_now();
    g_prof.vi += t_vi - t_alarm;

    port_ax_pump(1); /* AX frames due this video frame, mixed and queued */
    port_save_tick(); /* store the memory card when the game has written to it */
    report_scene_change();
    g_frame_count++;
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
        /* The background pipeline warm-up competes with the frame for the
         * browser's GPU process; on a two-core machine it made every match
         * stall. Hold it for half a second after any frame that ran late, so
         * it only works while there is time to spare (menus, loading). */
        {
            static int s_seed_hold;
            s_seed_hold = busy > 20.0 ? 30 : (s_seed_hold > 0 ? s_seed_hold - 1 : 0);
            aurora_pipeline_seed_pause(s_seed_hold > 0);
        }
        /* A frame this long is a visible pause. Say where it went: how much of
         * the game's time was spent parked in port_yield (and in which half),
         * and what the disc did meanwhile. */
        if (busy > 100.0) {
            unsigned reads, bytes;
            double read_wait, read_max;
            port_dvd_stats(&reads, &bytes, &read_wait, &read_max);
            port_log("spike: frame %u busy %.0f ms | game %.0f (yield %u x: sleep %.0f pump %.0f) present %.0f "
                     "browser %.0f begin %.0f | disc %u reads %u KB, wait %.0f ms (max %.0f)",
                     g_frame_count, busy, g_prof_left != 0.0 ? t_enter - g_prof_left : 0.0, g_yield.count,
                     g_yield.sleep_ms, g_yield.pump_ms, t_present - t_enter, t_browser - t_pace, t_begin - t_audio,
                     reads, bytes >> 10, read_wait, read_max);
        }
        memset(&g_yield, 0, sizeof g_yield);
        port_dvd_stats_reset();
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
    /* ?frameskip=off (boot.js sets Module.noFrameSkip): draw every frame and
     * let a slow machine run the game in slow motion instead. */
    if (emscripten_run_script_int("(typeof Module !== 'undefined' && Module.noFrameSkip) | 0")) {
        g_max_catchup = 0;
    }
    port_log("frame skip: %s", g_max_catchup > 0 ? "on (up to 5 logic frames per draw)" : "off");

    /* Before Aurora: it opens the memory card during aurora_initialize and
     * formats a blank one if the image is absent, so the stored card has to be
     * in the filesystem by then. */
    port_save_mount();

    AuroraConfig cfg = {
        .appName = "Melee",
        .userPath = PORT_SAVE_DIR,
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
    /* The pipeline warm-up list boot.js loaded (pipelines.bin.gz): every
     * shader pipeline a recorded session used, compiled in the background
     * from here on so a match's first draws do not stall on them. */
    {
        /* Two lists: the one shipped with the page, and the pipelines this
         * browser's own sessions met that it lacked (saved by boot.js).
         * Import skips what is already queued. */
        extern uint8_t* port_pipeline_seed_take(uint32_t* size, int saved);
        for (int saved = 0; saved <= 1; saved++) {
            uint32_t seed_size = 0;
            uint8_t* seed = port_pipeline_seed_take(&seed_size, saved);
            if (seed != NULL) {
                size_t queued = aurora_pipeline_seed_import(seed, seed_size);
                port_log("pipeline %s: %u bytes, %u pipelines queued", saved ? "list saved by earlier sessions" : "seed",
                         seed_size, (unsigned) queued);
                free(seed);
            }
        }
        preload_pipelines();
    }
    /* SDL creates the window resizable, and its resize handler sets the canvas
     * drawing buffer to whatever CSS size the page gives it. Aurora then makes
     * the framebuffer match, and scales x and y independently -- so the page's
     * object-fit letterboxing stops doing anything the moment the window is
     * resized, zoomed or made fullscreen. Ask Aurora to keep the logical 4:3
     * aspect and letterbox in the present pass instead, which holds at any
     * canvas size. */
    AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);
    /* Keep the render size. SDL's window is resizable, and on every resize
     * its handler set the canvas drawing buffer -- and so the frame the GPU
     * renders -- to the page's CSS size: 1280x960, or the 640x480 a small
     * machine asks for, lasted only until the window or fullscreen state
     * changed. A fixed buffer is scaled by the page instead (#canvas uses
     * object-fit: contain), so the cost stays what the render size says. */
    {
        int count = 0;
        SDL_Window** windows = SDL_GetWindows(&count);
        for (int i = 0; windows != NULL && i < count; i++) {
            SDL_SetWindowResizable(windows[i], false);
        }
        SDL_free(windows);
    }
    port_font_load_from_dol();
    port_log("Aurora initialized; starting the game");
    begin_frame_blocking();
    int rc = melee_main(); /* never returns on hardware; scene loops run inside */
    port_log("melee_main returned %d", rc);
    shutdown_and_exit();
    return rc;
}
