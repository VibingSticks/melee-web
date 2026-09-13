/* Definitions for game symbols whose units are excluded from the port build
 * (debug error handler, THP movies) and for linker-provided DOL symbols. */
#include <dolphin/os.h>
#include <emscripten.h>
#include <stdlib.h>

#include "port.h"

/* dberror.c (excluded): the crash handler installs OS error callbacks. */
void db_SetupCrashHandler(void) {}

/* debug.c (excluded): HSD's fatal error. */
void HSD_Panic(const char* file, int line, const char* msg)
{
    port_log("HSD_Panic: %s (%s:%d)", msg, file, line);
    emscripten_log(EM_LOG_ERROR | EM_LOG_C_STACK, "HSD_Panic stack");
    abort();
}

/* GX misc token; nothing to configure on Aurora. */
void GXSetMisc(u32 token, u32 val)
{
    (void) token;
    (void) val;
}

/* DOL stack bounds, only printed by the debug stack monitor. */
unsigned char _stack_end[4];
unsigned char _stack_addr[4];

/* The SDK's own THP decoder is compiled now (see cmake/game_sources.cmake), so
 * the stubs that used to fail every decode here have gone. What it still needs
 * are three cache primitives Aurora does not carry. The GameCube's locked
 * cache is a scratchpad the decoder fills and then DMAs out; this target has
 * one flat address space, so the "DMA" is a copy and the wait is nothing.
 * THPInit backs the scratchpad itself with a static buffer. */
#include <string.h>
void DCZeroRange(void* addr, u32 nBytes) { memset(addr, 0, nBytes); }
u32 LCStoreData(void* dest, void* src, u32 nBytes)
{
    memcpy(dest, src, nBytes);
    /* The hardware answers with the number of 32-byte blocks it queued. No
     * caller here looks, but keep the shape honest. */
    return (nBytes + 31) / 32;
}
void LCQueueWait(u32 len) { (void) len; }

/* --- host PC link (MCC over EXI) and its file IO: never connected --- */
#include <dolphin/mcc.h>
int MCCStreamOpen(enum MCC_CHANNEL chID, u8 blockSize) { (void) chID; (void) blockSize; return 0; }
int MCCInit(enum MCC_EXI exiChannel, u8 timeout, MCC_CBSysEvent cb) { (void) exiChannel; (void) timeout; (void) cb; return 0; }
void MCCExit(void) {}
int MCCEnumDevices(MCC_CBEnumDevices cb) { (void) cb; return 0; }
u8 MCCGetFreeBlocks(enum MCC_MODE mode) { (void) mode; return 0; }
u8 MCCGetLastError(void) { return 0; }
int MCCGetConnectionStatus(enum MCC_CHANNEL chID, enum MCC_CONNECT* connect) { (void) chID; (void) connect; return 0; }
int MCCNotify(enum MCC_CHANNEL chID, u32 notify) { (void) chID; (void) notify; return 0; }
int MCCOpen(enum MCC_CHANNEL chID, u8 blockSize, MCC_CBEvent cb) { (void) chID; (void) blockSize; (void) cb; return 0; }
int MCCClose(enum MCC_CHANNEL chID) { (void) chID; return 0; }
int MCCRead(enum MCC_CHANNEL chID, u32 offset, void* data, long size, enum MCC_SYNC_STATE async) { (void) chID; (void) offset; (void) data; (void) size; (void) async; return 0; }
int MCCWrite(enum MCC_CHANNEL chID, u32 offset, void* data, long size, enum MCC_SYNC_STATE async) { (void) chID; (void) offset; (void) data; (void) size; (void) async; return 0; }
int FIOInit(enum MCC_EXI exiChannel, enum MCC_CHANNEL chID, u8 blockSize) { (void) exiChannel; (void) chID; (void) blockSize; return 0; }
void FIOExit(void) {}
int FIOQuery(void) { return 0; }
int FIOFopen(const char* filename, u32 mode) { (void) filename; (void) mode; return -1; }
int FIOFclose(int handle) { (void) handle; return 0; }
u32 FIOFwrite(int handle, void* data, u32 size) { (void) handle; (void) data; (void) size; return 0; }

/* hsd_3915.c draws these for real now that the font atlas is loaded from the
 * disc at startup (port/src/font_dol.c); the stubs that stood in for it while
 * it was excluded from the build have gone. */
void HSD_LogInit(void) {}
void db_ClearFPUExceptions(void) {}

/* --- GX bits Aurora lacks --- */
void GXSetCopyClamp(GXFBClamp clamp) { (void) clamp; }
GXRenderModeObj GXNtsc480Prog = {
    VI_TVMODE_NTSC_PROG, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_SF, 0, 0,
    { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
    { 0, 0, 21, 22, 21, 0, 0 },
};

/* --- CodeWarrior runtime: double -> unsigned 64-bit --- */
unsigned long long __cvt_dbl_usll(double x)
{
    if (!(x > 0.0)) {
        return 0;
    }
    if (x >= 18446744073709551616.0) {
        return ~0ull;
    }
    return (unsigned long long) x;
}
