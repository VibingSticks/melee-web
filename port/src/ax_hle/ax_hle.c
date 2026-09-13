/* AX on the host: the voice mixer the GameCube ran on its DSP.
 *
 * The game drives audio through AX voices. Each voice is a parameter block
 * describing where its samples live in ARAM, how to decode them, how fast to
 * walk them and how loudly to mix them. On hardware the DSP reads those blocks
 * every 5 ms, mixes 160 samples per voice at 32 kHz and hands the result to AI;
 * HSD_SynthCallback rides that same interrupt and is where the game advances
 * its envelopes and starts and stops voices.
 *
 * Here the AX frame is driven from the video frame instead (port_ax_pump),
 * mixed in software and pushed to an SDL audio stream. Bit-exactness with the
 * DSP is not attempted: volumes ramp once per frame rather than per sample, and
 * resampling is linear rather than the DSP's 4-tap filter.
 *
 * Sample data lives in Aurora's emulated ARAM (ARGetStorageAddress), which is
 * where the game's .ssm banks were DMA'd.
 */
#include <dolphin/ai.h>
#include <dolphin/ar.h>
#include <dolphin/ax.h>
#include <dolphin/axfx.h>

#include <emscripten.h>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include <stddef.h>
#include <string.h>

#include "../port.h"
#include "ax_hle.h"

#define AX_SAMPLE_RATE 32000
#define AX_FRAME_SAMPLES 160 /* 5 ms at 32 kHz, one DSP frame */

/* AXPBADDR.format */
#define AX_FORMAT_ADPCM 0x00
#define AX_FORMAT_PCM8 0x19
#define AX_FORMAT_PCM16 0x0A

#define AX_STATE_RUN 1

/* AX volumes are 1.15: 0x8000 is unity. */
#define AX_UNITY 32768.0f

static AXVPB g_voices[AX_MAX_VOICES];
static u8 g_voice_used[AX_MAX_VOICES];
static void (*g_frame_cb)(void);

static SDL_AudioStream* g_stream;
static int g_audio_failed;

/* Per-voice resampler state; the parameter block has no room for ours. */
typedef struct {
    s32 frac;     /* 16.16 position between src[0] and src[1] */
    s16 s0, s1;   /* the two source samples being interpolated */
    u8 primed;
} resampler;
static resampler g_resample[AX_MAX_VOICES];

static u32 addr32(u16 hi, u16 lo) { return ((u32) hi << 16) | lo; }

static void set_addr32(u16* hi, u16* lo, u32 v)
{
    *hi = (u16) (v >> 16);
    *lo = (u16) v;
}

/* --- AI ------------------------------------------------------------------ */
void AIInit(u8* stack) { (void) stack; }
void AISetDSPSampleRate(u32 rate) { (void) rate; }
void AISetStreamVolLeft(u8 vol) { (void) vol; }
void AISetStreamVolRight(u8 vol) { (void) vol; }

/* --- voices -------------------------------------------------------------- */
void AXInit(void)
{
    memset(g_voices, 0, sizeof g_voices);
    memset(g_voice_used, 0, sizeof g_voice_used);
    memset(g_resample, 0, sizeof g_resample);
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
            memset(&g_resample[i], 0, sizeof g_resample[i]);
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
        p->pb.state = 0;
    }
}

void AXRegisterAuxACallback(void (*callback)(void*, void*), void* context) { (void) callback; (void) context; }
void AXRegisterAuxBCallback(void (*callback)(void*, void*), void* context) { (void) callback; (void) context; }

void AXRegisterCallback(void (*callback)()) { g_frame_cb = callback; }

/* These all land in the voice's own parameter block, which is what the mixer
 * reads; the stub that preceded this file dropped them on the floor. */
void AXSetVoiceAddr(AXVPB* p, AXPBADDR* addr)
{
    if (p != NULL && addr != NULL) {
        p->pb.addr = *addr;
        g_resample[p->index].primed = 0;
    }
}

void AXSetVoiceAdpcm(AXVPB* p, AXPBADPCM* adpcm)
{
    if (p != NULL && adpcm != NULL) {
        p->pb.adpcm = *adpcm;
    }
}

void AXSetVoiceAdpcmLoop(AXVPB* p, AXPBADPCMLOOP* adpcmloop)
{
    if (p != NULL && adpcmloop != NULL) {
        p->pb.adpcmLoop = *adpcmloop;
    }
}

void AXSetVoiceCurrentAddr(AXVPB* p, u32 addr)
{
    if (p != NULL) {
        set_addr32(&p->pb.addr.currentAddressHi, &p->pb.addr.currentAddressLo, addr);
        g_resample[p->index].primed = 0;
    }
}

void AXSetVoiceEndAddr(AXVPB* p, u32 addr)
{
    if (p != NULL) {
        set_addr32(&p->pb.addr.endAddressHi, &p->pb.addr.endAddressLo, addr);
    }
}

void AXSetVoiceItdOn(AXVPB* p) { (void) p; }
void AXSetVoiceItdTarget(AXVPB* p, u16 lShift, u16 rShift) { (void) p; (void) lShift; (void) rShift; }

void AXSetVoiceLoop(AXVPB* p, u16 loop)
{
    if (p != NULL) {
        p->pb.addr.loopFlag = loop;
    }
}

void AXSetVoiceLoopAddr(AXVPB* p, u32 addr)
{
    if (p != NULL) {
        set_addr32(&p->pb.addr.loopAddressHi, &p->pb.addr.loopAddressLo, addr);
    }
}

void AXSetVoiceMix(AXVPB* p, AXPBMIX* mix)
{
    if (p != NULL && mix != NULL) {
        p->pb.mix = *mix;
    }
}

void AXSetVoicePriority(AXVPB* p, u32 priority)
{
    if (p != NULL) {
        p->priority = priority;
    }
}

void AXSetVoiceSrc(AXVPB* p, AXPBSRC* src_)
{
    if (p != NULL && src_ != NULL) {
        p->pb.src = *src_;
    }
}

void AXSetVoiceSrcRatio(AXVPB* p, float ratio)
{
    if (p != NULL) {
        u32 fixed = (u32) (ratio * 65536.0f);
        p->pb.src.ratioHi = (u16) (fixed >> 16);
        p->pb.src.ratioLo = (u16) fixed;
    }
}

void AXSetVoiceState(AXVPB* p, u16 state)
{
    if (p != NULL) {
        p->pb.state = state;
    }
}

void AXSetVoiceVe(AXVPB* p, AXPBVE* ve)
{
    if (p != NULL && ve != NULL) {
        p->pb.ve = *ve;
    }
}

void AXSetVoiceVeDelta(AXVPB* p, s16 delta)
{
    if (p != NULL) {
        p->pb.ve.currentDelta = delta;
    }
}

/* --- sample decoding ----------------------------------------------------- */

/* GameCube ADPCM: 8-byte blocks of 16 nibbles. The first byte holds the
 * predictor (high nibble) and scale (low nibble); the remaining 14 nibbles are
 * samples. Addresses in the parameter block count nibbles, so the block is
 * addr/16 and the nibble within it addr%16, with 0 and 1 being the header. */
static s16 decode_adpcm_nibble(AXPB* pb, const u8* aram, u32 aram_size, u32 nibble)
{
    u32 byte = nibble >> 1;
    if (byte >= aram_size) {
        return 0;
    }
    u32 in_block = nibble & 15;
    if (in_block < 2) {
        return 0; /* the header, not a sample */
    }
    u32 header_byte = (nibble & ~15u) >> 1;
    if (header_byte >= aram_size) {
        return 0;
    }
    pb->adpcm.pred_scale = aram[header_byte];

    u32 pred = (pb->adpcm.pred_scale >> 4) & 7;
    u32 scale = pb->adpcm.pred_scale & 15;
    s32 nib = (nibble & 1) ? (aram[byte] & 15) : (aram[byte] >> 4);
    if (nib > 7) {
        nib -= 16;
    }

    s32 c1 = (s16) pb->adpcm.a[pred][0];
    s32 c2 = (s16) pb->adpcm.a[pred][1];
    s32 yn1 = (s16) pb->adpcm.yn1;
    s32 yn2 = (s16) pb->adpcm.yn2;

    s32 out = ((nib << scale) << 11) + ((c1 * yn1 + c2 * yn2) + 1024);
    out >>= 11;
    if (out > 32767) {
        out = 32767;
    } else if (out < -32768) {
        out = -32768;
    }
    pb->adpcm.yn2 = (u16) yn1;
    pb->adpcm.yn1 = (u16) out;
    return (s16) out;
}

/* Advances the voice by one source sample, honouring the loop point. Returns 0
 * and stops the voice when it runs off the end without a loop. */
static int next_source_sample(AXVPB* p, const u8* aram, u32 aram_size, s16* out)
{
    AXPB* pb = &p->pb;
    u32 cur = addr32(pb->addr.currentAddressHi, pb->addr.currentAddressLo);
    u32 end = addr32(pb->addr.endAddressHi, pb->addr.endAddressLo);

    if (cur > end) {
        if (pb->addr.loopFlag) {
            cur = addr32(pb->addr.loopAddressHi, pb->addr.loopAddressLo);
            if (pb->addr.format == AX_FORMAT_ADPCM) {
                /* The loop point needs the decoder restarted from the state the
                 * encoder had there, which the game supplies separately. */
                pb->adpcm.pred_scale = pb->adpcmLoop.loop_pred_scale;
                pb->adpcm.yn1 = pb->adpcmLoop.loop_yn1;
                pb->adpcm.yn2 = pb->adpcmLoop.loop_yn2;
            }
        } else {
            pb->state = 0;
            *out = 0;
            return 0;
        }
    }

    s16 sample = 0;
    switch (pb->addr.format) {
    case AX_FORMAT_ADPCM:
        if ((cur & 15) < 2) {
            cur += 2; /* step over the block header */
        }
        sample = decode_adpcm_nibble(pb, aram, aram_size, cur);
        cur += 1;
        break;
    case AX_FORMAT_PCM16: {
        u32 byte = cur * 2;
        if (byte + 1 < aram_size) {
            sample = (s16) (((u16) aram[byte] << 8) | aram[byte + 1]);
        }
        cur += 1;
        break;
    }
    case AX_FORMAT_PCM8:
        if (cur < aram_size) {
            sample = (s16) ((s8) aram[cur] << 8);
        }
        cur += 1;
        break;
    default:
        pb->state = 0;
        *out = 0;
        return 0;
    }

    set_addr32(&pb->addr.currentAddressHi, &pb->addr.currentAddressLo, cur);
    *out = sample;
    return 1;
}

/* --- mixing -------------------------------------------------------------- */

static void mix_voice(AXVPB* p, const u8* aram, u32 aram_size, s32* accum)
{
    AXPB* pb = &p->pb;
    resampler* r = &g_resample[p->index];

    u32 ratio = addr32(pb->src.ratioHi, pb->src.ratioLo);
    if (ratio == 0) {
        ratio = 0x10000; /* no resampling means one source sample per output */
    }

    float ve = (s16) pb->ve.currentVolume / AX_UNITY;
    float gl = ve * (pb->mix.vL / AX_UNITY);
    float gr = ve * (pb->mix.vR / AX_UNITY);

    if (!r->primed) {
        if (!next_source_sample(p, aram, aram_size, &r->s0)) {
            return;
        }
        if (!next_source_sample(p, aram, aram_size, &r->s1)) {
            r->s1 = r->s0;
        }
        r->frac = 0;
        r->primed = 1;
    }

    for (int i = 0; i < AX_FRAME_SAMPLES; i++) {
        if (pb->state != AX_STATE_RUN) {
            break;
        }
        s32 t = r->frac & 0xFFFF;
        s32 s = r->s0 + (((r->s1 - r->s0) * t) >> 16);
        accum[i * 2 + 0] += (s32) (s * gl);
        accum[i * 2 + 1] += (s32) (s * gr);

        r->frac += (s32) ratio;
        while (r->frac >= 0x10000) {
            r->frac -= 0x10000;
            r->s0 = r->s1;
            if (!next_source_sample(p, aram, aram_size, &r->s1)) {
                r->s1 = r->s0;
                break;
            }
        }
    }

    /* One volume step per frame rather than per sample: the envelope still
     * tracks, an order of magnitude coarser than the DSP. */
    s32 vol = (s16) pb->ve.currentVolume + (s32) pb->ve.currentDelta * AX_FRAME_SAMPLES;
    if (vol > 32767) {
        vol = 32767;
    } else if (vol < 0) {
        vol = 0;
    }
    pb->ve.currentVolume = (u16) vol;
}

static void run_ax_frame(s16* out, int run_callback)
{
    static s32 accum[AX_FRAME_SAMPLES * 2];
    memset(accum, 0, sizeof accum);

    /* The game's callback is only safe from the frame pump: it is scene-level
     * code, and port_yield is called from inside scene code that is already
     * mid-way through something. Mixing from a yield is fine; re-entering the
     * game from one is not. */
    if (run_callback && g_frame_cb != NULL) {
        g_frame_cb();
    }

    const u8* aram = ARGetStorageAddress();
    u32 aram_size = ARGetSize();
    if (aram != NULL) {
        for (u32 i = 0; i < AX_MAX_VOICES; i++) {
            if (g_voice_used[i] && g_voices[i].pb.state == AX_STATE_RUN) {
                mix_voice(&g_voices[i], aram, aram_size, accum);
            }
        }
    }

    for (int i = 0; i < AX_FRAME_SAMPLES * 2; i++) {
        s32 v = accum[i];
        out[i] = (s16) (v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

/* --- the port's side ----------------------------------------------------- */

void port_ax_init(void)
{
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, AX_SAMPLE_RATE };
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        port_log("audio: no SDL audio subsystem (%s); running silent", SDL_GetError());
        g_audio_failed = 1;
        return;
    }
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (g_stream == NULL) {
        port_log("audio: could not open an output stream (%s); running silent", SDL_GetError());
        g_audio_failed = 1;
        return;
    }
    SDL_ResumeAudioStreamDevice(g_stream);
    port_log("audio: %d Hz stereo out", AX_SAMPLE_RATE);
}

void port_ax_pump(int from_frame)
{
    /* Keep roughly this much audio queued. Enough that a slow frame does not
     * starve the device, short enough that input still feels attached to sound. */
    const int target_bytes = AX_SAMPLE_RATE * 2 * 2 * 50 / 1000; /* 50 ms */
    const int frame_bytes = AX_FRAME_SAMPLES * 2 * 2;

    /* This runs from the frame pump and from port_yield, because the game
     * blocks inside scene code waiting for sound banks to load and the synth
     * callback is what completes them (HSD_SynthSFXWaitForLoadCompletion).
     * Producing to a queue depth rather than on a schedule keeps the rate right
     * whichever one calls, and bounds how far ahead a burst can run. */
    int budget = 16;

    if (g_stream != NULL) {
        while (budget-- > 0 && (int) SDL_GetAudioStreamQueued(g_stream) < target_bytes) {
            s16 frame[AX_FRAME_SAMPLES * 2];
            run_ax_frame(frame, from_frame);
            SDL_PutAudioStreamData(g_stream, frame, (int) sizeof frame);
        }
        return;
    }

    if (!g_audio_failed) {
        return; /* not opened yet */
    }

    /* Silent, but the game still needs its AX callback to make progress. */
    static double s_next_ms;
    double now = emscripten_get_now();
    if (s_next_ms == 0) {
        s_next_ms = now;
    }
    while (budget-- > 0 && now >= s_next_ms) {
        s16 frame[AX_FRAME_SAMPLES * 2];
        run_ax_frame(frame, from_frame);
        s_next_ms += 1000.0 * AX_FRAME_SAMPLES / AX_SAMPLE_RATE;
    }
    if (s_next_ms < now) {
        s_next_ms = now;
    }
}

/* --- AXFX ---------------------------------------------------------------- */
int AXFXChorusInit(struct AXFX_CHORUS* c) { (void) c; return 1; }
int AXFXChorusShutdown(struct AXFX_CHORUS* c) { (void) c; return 1; }
int AXFXDelayInit(struct AXFX_DELAY* delay) { (void) delay; return 1; }
int AXFXDelayShutdown(struct AXFX_DELAY* delay) { (void) delay; return 1; }
int AXFXReverbHiInit(struct AXFX_REVERBHI* rev) { (void) rev; return 1; }
int AXFXReverbHiShutdown(struct AXFX_REVERBHI* rev) { (void) rev; return 1; }
int AXFXReverbStdInit(struct AXFX_REVERBSTD* rev) { (void) rev; return 1; }
int AXFXReverbStdShutdown(struct AXFX_REVERBSTD* rev) { (void) rev; return 1; }
void AXFXSetHooks(void* (*alloc_hook)(size_t), void (*free_hook)(void*)) { (void) alloc_hook; (void) free_hook; }

void AXFXChorusCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_CHORUS* c) { (void) b; (void) c; }
void AXFXDelayCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_DELAY* d) { (void) b; (void) d; }
void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_REVERBHI* r) { (void) b; (void) r; }
void AXFXReverbStdCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_REVERBSTD* r) { (void) b; (void) r; }
