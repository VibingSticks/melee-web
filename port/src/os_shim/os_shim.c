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

/* --- debug output helper used by sysdolphin's class.c --- */
void OSReport_PrintSpaces(int n)
{
    while (n-- > 0) {
        OSReport(" ");
    }
}
