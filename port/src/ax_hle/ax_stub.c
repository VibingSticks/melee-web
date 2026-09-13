/* Silent audio: stubs for the AX, AXFX and AI entry points the game calls.
 * ax_hle.c is the real mixer; build with -DPORT_AUDIO=ON to use it instead.
 * Voices are handed out from a small pool so the game's voice management runs
 * normally, but nothing they are set to is kept and nothing is ever mixed. */
#include <dolphin/ai.h>
#include <dolphin/ax.h>
#include <dolphin/axfx.h>
#include <stddef.h>
#include <string.h>

#include "ax_hle.h"

/* --- AI --- */
void AIInit(u8* stack) { (void) stack; }
void AISetDSPSampleRate(u32 rate) { (void) rate; }
void AISetStreamVolLeft(u8 vol) { (void) vol; }
void AISetStreamVolRight(u8 vol) { (void) vol; }

/* --- AX voices --- */
static AXVPB g_voices[AX_MAX_VOICES];
static int g_voice_used[AX_MAX_VOICES];

void AXInit(void)
{
    memset(g_voices, 0, sizeof g_voices);
    memset(g_voice_used, 0, sizeof g_voice_used);
    for (u32 i = 0; i < AX_MAX_VOICES; i++) {
        g_voices[i].index = i;
    }
}

AXVPB* AXAcquireVoice(u32 priority, void (*callback)(void*), u32 userContext)
{
    for (u32 i = 0; i < AX_MAX_VOICES; i++) {
        if (!g_voice_used[i]) {
            g_voice_used[i] = 1;
            memset(&g_voices[i], 0, sizeof g_voices[i]);
            g_voices[i].index = i;
            g_voices[i].priority = priority;
            g_voices[i].callback = callback;
            g_voices[i].userContext = userContext;
            return &g_voices[i];
        }
    }
    return NULL;
}

void AXFreeVoice(AXVPB* p)
{
    if (p != NULL && p->index < AX_MAX_VOICES) {
        g_voice_used[p->index] = 0;
    }
}

void AXRegisterAuxACallback(void (*callback)(void*, void*), void* context) { (void) callback; (void) context; }
void AXRegisterAuxBCallback(void (*callback)(void*, void*), void* context) { (void) callback; (void) context; }
void AXRegisterCallback(void (*callback)()) { (void) callback; }

void AXSetVoiceAddr(AXVPB* p, AXPBADDR* addr) { (void) p; (void) addr; }
void AXSetVoiceAdpcm(AXVPB* p, AXPBADPCM* adpcm) { (void) p; (void) adpcm; }
void AXSetVoiceAdpcmLoop(AXVPB* p, AXPBADPCMLOOP* adpcmloop) { (void) p; (void) adpcmloop; }
void AXSetVoiceCurrentAddr(AXVPB* p, u32 addr) { (void) p; (void) addr; }
void AXSetVoiceEndAddr(AXVPB* p, u32 addr) { (void) p; (void) addr; }
void AXSetVoiceItdOn(AXVPB* p) { (void) p; }
void AXSetVoiceItdTarget(AXVPB* p, u16 lShift, u16 rShift) { (void) p; (void) lShift; (void) rShift; }
void AXSetVoiceLoop(AXVPB* p, u16 loop) { (void) p; (void) loop; }
void AXSetVoiceLoopAddr(AXVPB* p, u32 addr) { (void) p; (void) addr; }
void AXSetVoiceMix(AXVPB* p, AXPBMIX* mix) { (void) p; (void) mix; }
void AXSetVoicePriority(AXVPB* p, u32 priority) { if (p != NULL) p->priority = priority; }
void AXSetVoiceSrc(AXVPB* p, AXPBSRC* src_) { (void) p; (void) src_; }
void AXSetVoiceSrcRatio(AXVPB* p, float ratio) { (void) p; (void) ratio; }
void AXSetVoiceState(AXVPB* p, u16 state) { (void) p; (void) state; }
void AXSetVoiceVe(AXVPB* p, AXPBVE* ve) { (void) p; (void) ve; }
void AXSetVoiceVeDelta(AXVPB* p, s16 delta) { (void) p; (void) delta; }

/* --- AXFX --- */
int AXFXChorusInit(struct AXFX_CHORUS* c) { (void) c; return 1; }
int AXFXChorusShutdown(struct AXFX_CHORUS* c) { (void) c; return 1; }
int AXFXDelayInit(struct AXFX_DELAY* delay) { (void) delay; return 1; }
int AXFXDelayShutdown(struct AXFX_DELAY* delay) { (void) delay; return 1; }
int AXFXReverbHiInit(struct AXFX_REVERBHI* rev) { (void) rev; return 1; }
int AXFXReverbHiShutdown(struct AXFX_REVERBHI* rev) { (void) rev; return 1; }
int AXFXReverbStdInit(struct AXFX_REVERBSTD* rev) { (void) rev; return 1; }
int AXFXReverbStdShutdown(struct AXFX_REVERBSTD* rev) { (void) rev; return 1; }
void AXFXSetHooks(void* (*alloc_hook)(size_t), void (*free_hook)(void*)) { (void) alloc_hook; (void) free_hook; }

/* AXFX effect callbacks (run by the aux bus on hardware) */
void AXFXChorusCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_CHORUS* c) { (void) b; (void) c; }
void AXFXDelayCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_DELAY* d) { (void) b; (void) d; }
void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_REVERBHI* r) { (void) b; (void) r; }
void AXFXReverbStdCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_REVERBSTD* r) { (void) b; (void) r; }

/* --- the port's side (no-ops here; see ax_hle.c) --- */
void port_ax_init(void) {}
void port_ax_pump(int from_frame) { (void) from_frame; }
