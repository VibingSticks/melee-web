#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <dolphin/mtx.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
  const char* levelStr;
  FILE* out = stdout;
  switch (level) {
  case LOG_DEBUG:
    levelStr = "DEBUG";
    break;
  case LOG_INFO:
    levelStr = "INFO";
    break;
  case LOG_WARNING:
    levelStr = "WARNING";
    break;
  case LOG_ERROR:
    levelStr = "ERROR";
    out = stderr;
    break;
  case LOG_FATAL:
    levelStr = "FATAL";
    out = stderr;
    break;
  }
  fprintf(out, "[%s: %s;%s]\n", levelStr, module, message);
  if (level == LOG_FATAL) {
    fflush(out);
    abort();
  }
}

static void draw() {
  GXSetCopyClear((GXColor){.r = 0, .g = 0, .b = 100, .a = 255}, GX_MAX_Z24);

  /* One immediate-mode GX triangle through the real GX pipeline (exercises the
   * generated shader, uniform block and per-draw immediates). */
  Mtx44 proj = {{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, -1.f, 0.f}, {0.f, 0.f, 0.f, 1.f}};
  Mtx view;
  MTXIdentity(view);
  GXSetProjection(proj, GX_ORTHOGRAPHIC);
  GXLoadPosMtxImm(view, GX_PNMTX0);
  GXSetCurrentMtx(GX_PNMTX0);
  GXSetViewport(0.f, 0.f, 640.f, 480.f, 0.f, 1.f);
  GXSetScissor(0, 0, 640, 480);
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  GXSetNumChans(1);
  GXSetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
  GXSetChanMatColor(GX_COLOR0A0, (GXColor){.r = 255, .g = 32, .b = 32, .a = 255});
  GXSetNumTexGens(0);
  GXSetNumTevStages(1);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
  GXSetCullMode(GX_CULL_NONE);
  GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
  GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
  GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
  GXPosition3f32(-0.8f, -0.8f, 0.5f);
  GXColor4u8(255, 0, 0, 255);
  GXPosition3f32(0.8f, -0.8f, 0.5f);
  GXColor4u8(255, 0, 0, 255);
  GXPosition3f32(0.0f, 0.9f, 0.5f);
  GXColor4u8(255, 0, 0, 255);
  GXEnd();
}

int main(int argc, char* argv[]) {
  const AuroraConfig config = {
      .appName = "Demo",
      .logCallback = &log_callback,
      .allowCpuAdapter = true, /* spike: headless Chrome may only offer SwiftShader */
      /* ?gpu=noimm forces the no-immediates compat profile on a real WebGPU adapter */
      .forceCompatProfile = emscripten_run_script_int("(new URLSearchParams(location.search).get('gpu') === 'noimm') ? 1 : 0"),
  };
  AuroraInfo initInfo = aurora_initialize(argc, argv, &config);

  bool exiting = false;
  bool paused = false;
  unsigned frames = 0, skipped = 0, iters = 0;
  while (!exiting) {
    if (++iters % 120 == 0) {
      printf("[spike] iter %u: frames drawn %u, skipped %u\n", iters, frames, skipped);
    }
    const AuroraEvent* event = aurora_update();
    while (event != NULL && event->type != AURORA_NONE) {
      printf("[spike] event %d\n", (int)event->type);
      switch (event->type) {
      case AURORA_EXIT:
        printf("[spike] AURORA_EXIT received\n");
        exiting = true;
        break;
      case AURORA_PAUSED:
        paused = true;
        break;
      case AURORA_UNPAUSED:
        paused = false;
        break;
      case AURORA_WINDOW_RESIZED:
        initInfo.windowSize = event->windowSize;
        break;
      default:
        break;
      }
      ++event;
    }
    if (exiting || paused || !aurora_begin_frame()) {
        skipped++;
#ifdef __EMSCRIPTEN__
      emscripten_sleep(16);
#endif
      continue;
    }
    draw();
    aurora_end_frame();
    frames++;
#ifdef __EMSCRIPTEN__
    emscripten_sleep(0);
#endif
  }

  aurora_shutdown();
  return 0;
}
