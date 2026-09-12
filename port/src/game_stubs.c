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

/* --- THP video decoder (SDK, not built yet; Task 22): every decode fails --- */
void THPInit(void) {}
s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV, void* work)
{
    (void) file; (void) tileY; (void) tileU; (void) tileV; (void) work;
    return -1;
}
s32 THPDec_8032FD40(void* data, u16 arg1) { (void) data; (void) arg1; return -1; }
s32 THPDec_8032F8D4(u8* data, void* out) { (void) data; (void) out; return -1; }
void THPDec_80331340(s32 a, void* b, void* c, void* d) { (void) a; (void) b; (void) c; (void) d; }
void THPDec_803313D0(s32 a, void* b, void* c, void* d, u32 e) { (void) a; (void) b; (void) c; (void) d; (void) e; }

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

/* --- debug console drawing (debugconsole_main.c is excluded) --- */
#include <dolphin/gx.h>
void DrawRectangle(float x, float y, float w, float h, GXColor* color) { (void) x; (void) y; (void) w; (void) h; (void) color; }
f32 DrawASCII(int chr, float x, float y, GXColor* color) { (void) chr; (void) y; (void) color; return x; }
void hsd_80391A04(float sx, float sy, int lw) { (void) sx; (void) sy; (void) lw; }
s32 hsd_80391AC8(char* s, GXColor* c, f32 x, f32 y) { (void) s; (void) c; (void) x; (void) y; return 0; }
void hsd_80391E18(const u8* list, f32 x1, f32 y1, f32 x2, f32 y2) { (void) list; (void) x1; (void) y1; (void) x2; (void) y2; }
void hsd_80391F28(GXColor* c, f32 a, f32 b, f32 d, f32 e, f32 f) { (void) c; (void) a; (void) b; (void) d; (void) e; (void) f; }
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
