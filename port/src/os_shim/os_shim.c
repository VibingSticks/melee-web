/* OS functions the game calls that Aurora does not provide. Everything here is
 * either a no-op on a flat single-threaded address space or a constant. */
#include <dolphin/os.h>

#include "../port.h"

/* --- interrupts: single-threaded, so critical sections are just a counter --- */
static int g_irq_depth;

BOOL OSDisableInterrupts(void)
{
    return g_irq_depth++ == 0;
}

BOOL OSEnableInterrupts(void)
{
    BOOL was_enabled = g_irq_depth == 0;
    g_irq_depth = 0;
    return was_enabled;
}

BOOL OSRestoreInterrupts(BOOL level)
{
    BOOL was_enabled = g_irq_depth == 0;
    if (level) {
        g_irq_depth = 0;
    } else if (g_irq_depth > 0) {
        g_irq_depth--;
    }
    return was_enabled;
}

/* --- caches: no data/instruction cache to maintain --- */
void DCFlushRange(void* p, u32 n) { (void) p; (void) n; }
void DCInvalidateRange(void* p, u32 n) { (void) p; (void) n; }
void DCStoreRange(void* p, u32 n) { (void) p; (void) n; }
void DCFlushRangeNoSync(void* p, u32 n) { (void) p; (void) n; }
void ICInvalidateRange(void* p, u32 n) { (void) p; (void) n; }

/* --- system --- */
void OSResetSystem(int reset, u32 reset_code, BOOL force_menu)
{
    port_log("OSResetSystem(reset=%d, code=0x%x, forceMenu=%d): stopping", reset, (unsigned) reset_code,
             (int) force_menu);
    port_request_exit();
}

u32 OSGetResetCode(void) { return 0; }
BOOL OSGetResetSwitchState(void) { return FALSE; }
u32 OSGetConsoleSimulatedMemSize(void) { return 24 * 1024 * 1024; /* retail MEM1 */ }
s32 OSCheckActiveThreads(void) { return 1; /* only the main thread */ }

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler)
{
    (void) error;
    (void) handler;
    return NULL;
}

/* --- SRAM-backed settings --- */
static u32 g_sound_mode = 1;       /* stereo */
static u32 g_progressive_mode = 0; /* 480i, the game's default */

u32 OSGetSoundMode(void) { return g_sound_mode; }
void OSSetSoundMode(u32 mode) { g_sound_mode = mode; }
u32 OSGetProgressiveMode(void) { return g_progressive_mode; }
void OSSetProgressiveMode(u32 mode) { g_progressive_mode = mode; }


/* --- MSL runtime assert, called directly by a few game units --- */
#include <emscripten.h>
#include <stdlib.h>
/* The decompilation compiles the SDK's assertions in; the retail disc runs with
 * data these sometimes reject (and on hardware a failed assertion only halted a
 * development build). A port cannot be bit-exact, so an assertion is reported
 * and execution continues. Set `Module.assertsFatal = true` to stop instead,
 * which is what the port's own tests do. */
static int g_asserts_fatal = -1;
static unsigned g_assert_count;

void __assert(const char* file, int line, const char* expr)
{
    if (g_asserts_fatal < 0) {
        g_asserts_fatal = emscripten_run_script_int(
            "(typeof Module !== 'undefined' && Module.assertsFatal) ? 1 : 0");
    }
    if (++g_assert_count <= 200) {
        port_log("assertion failed: %s (%s:%d)", expr, file, line);
    }
    if (g_asserts_fatal) {
        emscripten_log(EM_LOG_ERROR | EM_LOG_C_STACK, "assertion stack");
        abort();
    }
}

/* --- OSReport family: Aurora declares these weak and leaves them to the game --- */
#include <dolphin/gx/GXStruct.h>
#include <stdio.h>
#include <string.h>

void OSVReport(const char* msg, va_list list)
{
    char buf[1024];
    vsnprintf(buf, sizeof buf, msg, list);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = 0; /* console.log adds its own line break */
    }
    if (n > 0) {
        emscripten_log(EM_LOG_CONSOLE, "%s", buf);
    }
}

void OSReport(const char* msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    OSVReport(msg, ap);
    va_end(ap);
}

void OSPanic(const char* file, int line, const char* msg, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, msg);
    vsnprintf(buf, sizeof buf, msg, ap);
    va_end(ap);
    port_log("OSPanic at %s:%d: %s", file, line, buf);
    emscripten_log(EM_LOG_ERROR | EM_LOG_C_STACK, "OSPanic stack");
    abort();
}

void OSFatal(GXColor fg, GXColor bg, const char* msg)
{
    (void) fg;
    (void) bg;
    port_log("OSFatal: %s", msg);
    abort();
}
