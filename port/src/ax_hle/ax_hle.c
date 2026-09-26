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
 * mixed in software and pushed to an AudioWorklet (or an SDL audio stream
 * with ?audio=sdl). Bit-exactness with the
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

/* The aux buses: each voice sends to A and B at its own levels (vAuxA*,
 * vAuxB*); once a frame the bus's callback (an AXFX effect) processes the
 * summed send in place, and the result joins the main mix. */
static void (*g_aux_cb[2])(void*, void*);
static void* g_aux_ctx[2];

static int g_aux_heard[2]; /* logged the bus's first send yet */
static int g_fx_off;       /* ?fx=off: leave the aux buses dry, to compare */

void AXRegisterAuxACallback(void (*callback)(void*, void*), void* context)
{
    if (callback != NULL) {
        port_log("audio: aux A effect on");
    }
    g_aux_heard[0] = 0;
    g_aux_cb[0] = callback;
    g_aux_ctx[0] = context;
}

void AXRegisterAuxBCallback(void (*callback)(void*, void*), void* context)
{
    if (callback != NULL) {
        port_log("audio: aux B effect on");
    }
    g_aux_heard[1] = 0;
    g_aux_cb[1] = callback;
    g_aux_ctx[1] = context;
}

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

/* Aux send buffers, per bus: left, right and surround, 160 samples each and
 * contiguous -- the reverb walks all three from the left pointer. */
static s32 g_aux[2][3][AX_FRAME_SAMPLES];

static void mix_voice(AXVPB* p, const u8* aram, u32 aram_size, s32* accum)
{
    AXPB* pb = &p->pb;
    resampler* r = &g_resample[p->index];

    u32 ratio = addr32(pb->src.ratioHi, pb->src.ratioLo);
    if (ratio == 0) {
        ratio = 0x10000; /* no resampling means one source sample per output */
    }

    /* The volume envelope steps by its delta every sample, as on the DSP; the
     * mix levels multiply it. */
    s32 vol = (s16) pb->ve.currentVolume;
    const s32 vdelta = (s16) pb->ve.currentDelta;
    const float gl = pb->mix.vL / AX_UNITY / AX_UNITY;
    const float gr = pb->mix.vR / AX_UNITY / AX_UNITY;
    /* Sends, by bus then left/right/surround; a bus with no effect is not
     * mixed, so its sends are skipped. */
    const float ga[2][3] = {
        { pb->mix.vAuxAL / AX_UNITY / AX_UNITY, pb->mix.vAuxAR / AX_UNITY / AX_UNITY, pb->mix.vAuxAS / AX_UNITY / AX_UNITY },
        { pb->mix.vAuxBL / AX_UNITY / AX_UNITY, pb->mix.vAuxBR / AX_UNITY / AX_UNITY, pb->mix.vAuxBS / AX_UNITY / AX_UNITY },
    };
    int sends = 0;
    for (int b = 0; b < 2; b++) {
        if (g_aux_cb[b] != NULL && !g_fx_off && (ga[b][0] != 0.0f || ga[b][1] != 0.0f || ga[b][2] != 0.0f)) {
            sends |= 1 << b;
            if (!g_aux_heard[b]) {
                g_aux_heard[b] = 1;
                port_log("audio: first send to aux %c (voice %u)", 'A' + b, (unsigned) p->index);
            }
        }
    }

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
        float s = (float) (r->s0 + (((r->s1 - r->s0) * t) >> 16)) * (float) vol;
        accum[i * 2 + 0] += (s32) (s * gl);
        accum[i * 2 + 1] += (s32) (s * gr);
        for (int b = 0; b < 2; b++) {
            if (sends & (1 << b)) {
                g_aux[b][0][i] += (s32) (s * ga[b][0]);
                g_aux[b][1][i] += (s32) (s * ga[b][1]);
                g_aux[b][2][i] += (s32) (s * ga[b][2]);
            }
        }
        vol += vdelta;
        if (vol > 32767) {
            vol = 32767;
        } else if (vol < 0) {
            vol = 0;
        }

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

    /* The effects, then their output into the main mix. Surround has no
     * speaker here (the main surround send is not mixed either). */
    for (int b = 0; b < 2; b++) {
        if (g_aux_cb[b] != NULL && !g_fx_off) {
            struct AXFX_BUFFERUPDATE update = { (long*) g_aux[b][0], (long*) g_aux[b][1], (long*) g_aux[b][2] };
            g_aux_cb[b](&update, g_aux_ctx[b]);
            for (int i = 0; i < AX_FRAME_SAMPLES; i++) {
                accum[i * 2 + 0] += g_aux[b][0][i];
                accum[i * 2 + 1] += g_aux[b][1][i];
            }
        }
    }
    memset(g_aux, 0, sizeof g_aux);

    for (int i = 0; i < AX_FRAME_SAMPLES * 2; i++) {
        s32 v = accum[i];
        out[i] = (s16) (v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

/* --- the port's side ----------------------------------------------------- */

/* AudioWorklet output (web/js/imports.js). */
extern int port_audio_open(int rate);
extern int port_audio_running(void);
extern int port_audio_queued_frames(void);
extern int port_audio_dry_frames(void);
extern void port_audio_push(const s16* samples, int frames);

static int g_worklet; /* output through the AudioWorklet rather than SDL */
static void open_sdl(void);

void port_ax_init(void)
{
    g_fx_off = emscripten_run_script_int("(typeof Module !== 'undefined' && Module.audioFxOff) | 0");
    if (g_fx_off) {
        port_log("audio: effects off (?fx=off)");
    }
    /* ?audio=sdl (boot.js sets Module.audioBackend) keeps SDL's output. */
    if (!emscripten_run_script_int("(typeof Module !== 'undefined' && Module.audioBackend === 'sdl') | 0") &&
        port_audio_open(AX_SAMPLE_RATE)) {
        g_worklet = 1;
        port_log("audio: %d Hz stereo out (AudioWorklet)", AX_SAMPLE_RATE);
        return;
    }
    open_sdl();
}

static void open_sdl(void)
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
    port_log("audio: %d Hz stereo out (SDL)", AX_SAMPLE_RATE);
}

static unsigned g_dry_frames; /* AX frames mixed and dropped after an underrun */

unsigned port_ax_take_dry_ms(void)
{
    unsigned ms = g_dry_frames * 1000u * AX_FRAME_SAMPLES / AX_SAMPLE_RATE;
    g_dry_frames = 0;
    return ms;
}

/* Mix and drop AX frames the output went without, so the music keeps its
 * place on the wall clock (see port_ax_pump). */
static void skip_ax_frames(int missed, int from_frame)
{
    if (missed > 50) {
        missed = 50; /* 250 ms: a longer stall is a load, and the frame pump resyncs too */
    }
    g_dry_frames += (unsigned) missed;
    while (missed-- > 0) {
        s16 frame[AX_FRAME_SAMPLES * 2];
        run_ax_frame(frame, from_frame);
    }
}

/* No output (none opened, or the worklet still loading or held by the
 * autoplay policy): the game still needs its AX callback to make progress,
 * so run it on the wall clock and discard the mix. */
static void pump_silent(int budget, int from_frame)
{
    static double s_next_ms;
    double now = emscripten_get_now();
    if (s_next_ms == 0 || s_next_ms < now - 250.0) {
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

void port_ax_pump(int from_frame)
{
    /* This runs from the frame pump and from port_yield, because the game
     * blocks inside scene code waiting for sound banks to load and the synth
     * callback is what completes them (HSD_SynthSFXWaitForLoadCompletion).
     * Producing to a queue depth rather than on a schedule keeps the rate right
     * whichever one calls, and bounds how far ahead a burst can run. */
    int budget = 16;
    const double frame_ms = 1000.0 * AX_FRAME_SAMPLES / AX_SAMPLE_RATE;

    if (g_worklet) {
        int running = port_audio_running();
        if (running < 0) {
            g_worklet = 0; /* the worklet failed to load: SDL's output instead */
            port_log("audio: the AudioWorklet failed; falling back to SDL");
            open_sdl();
            return;
        }
        if (running == 0) {
            pump_silent(budget, from_frame);
            return;
        }
        /* The worklet plays on the audio thread, so a slow frame only costs
         * sound when it outlasts the queue. Keep 60 ms queued, more for a
         * while after the queue has run dry (a machine that stalls often
         * gets a deeper buffer), and when it has run dry, drop what it
         * missed so the music stays in time with the game's clock. */
        static int s_dry_seen = -1;
        static double s_target_ms = 60.0, s_last_ms;
        double now = emscripten_get_now();
        int dry = port_audio_dry_frames();
        if (s_dry_seen < 0) {
            s_dry_seen = dry;
        }
        if (dry != s_dry_seen) {
            skip_ax_frames((dry - s_dry_seen) / AX_FRAME_SAMPLES, from_frame);
            s_dry_seen = dry;
            s_target_ms = s_target_ms + 20.0 > 150.0 ? 150.0 : s_target_ms + 20.0;
        } else if (s_last_ms != 0.0 && s_target_ms > 60.0) {
            s_target_ms -= (now - s_last_ms) * 0.002; /* back down 2 ms a second */
        }
        s_last_ms = now;
        int target_frames = (int) (s_target_ms * AX_SAMPLE_RATE / 1000.0);
        int queued = port_audio_queued_frames();
        while (budget-- > 0 && queued < target_frames) {
            s16 frame[AX_FRAME_SAMPLES * 2];
            run_ax_frame(frame, from_frame);
            port_audio_push(frame, AX_FRAME_SAMPLES);
            queued += AX_FRAME_SAMPLES;
        }
        return;
    }

    if (g_stream != NULL) {
        /* Keep roughly this much audio queued. Enough that a slow frame does
         * not starve the device, short enough that input still feels attached
         * to sound. */
        const int target_bytes = AX_SAMPLE_RATE * 2 * 2 * 50 / 1000; /* 50 ms */
        /* SDL's browser callback runs on the main thread, so a stall starves
         * Web Audio without the stream ever looking empty, and the music
         * resumes where it stopped: late for good, behind the movie, whose
         * alarm clock catches up. Compare what the device has taken with the
         * wall clock, and when it has fallen behind, drop what it missed. */
        static double s_start_ms, s_base_ms;
        static double s_put_ms, s_dropped_ms; /* audio produced: queued, and mixed but dropped */
        const double bytes_per_ms = AX_SAMPLE_RATE * 2 * 2 / 1000.0;
        double now = emscripten_get_now();
        if (s_start_ms == 0.0) {
            s_start_ms = now;
        }
        double played_ms = s_put_ms - SDL_GetAudioStreamQueued(g_stream) / bytes_per_ms + s_dropped_ms;
        double behind_ms = (now - s_start_ms) - played_ms - s_base_ms;
        if (behind_ms < 0.0) {
            s_base_ms += behind_ms; /* the device runs ahead of the wall clock: take that as the new zero */
        } else if (behind_ms > 60.0) { /* the device pulls about 43 ms at a time */
            int missed = (int) (behind_ms / frame_ms);
            if (missed > 50) {
                s_base_ms += (missed - 50) * frame_ms;
                missed = 50;
            }
            s_dropped_ms += missed * frame_ms;
            skip_ax_frames(missed, from_frame);
        }
        while (budget-- > 0 && (int) SDL_GetAudioStreamQueued(g_stream) < target_bytes) {
            s16 frame[AX_FRAME_SAMPLES * 2];
            run_ax_frame(frame, from_frame);
            SDL_PutAudioStreamData(g_stream, frame, (int) sizeof frame);
            s_put_ms += frame_ms;
        }
        return;
    }

    if (!g_audio_failed) {
        return; /* not opened yet */
    }
    pump_silent(budget, from_frame);
}

/* --- AXFX ---------------------------------------------------------------- */
/* The standard reverb and the delay -- the two effects Melee sets up
 * (lbaudio_ax.c: reverb on aux A, delay on aux B) -- are the SDK's own code
 * (libs/dolphin/src/dolphin/axfx, compiled into the game library). The high
 * reverb and the chorus are PowerPC assembly the game never calls, so they
 * stay out: Init fails, which makes AXDriverSetupAux leave the bus dry. */
int AXFXChorusInit(struct AXFX_CHORUS* c) { (void) c; return 0; }
int AXFXChorusShutdown(struct AXFX_CHORUS* c) { (void) c; return 1; }
int AXFXReverbHiInit(struct AXFX_REVERBHI* rev) { (void) rev; return 0; }
int AXFXReverbHiShutdown(struct AXFX_REVERBHI* rev) { (void) rev; return 1; }
void AXFXChorusCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_CHORUS* c) { (void) b; (void) c; }
void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_REVERBHI* r) { (void) b; (void) r; }
