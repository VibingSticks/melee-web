#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <dolphin/gx.h>
#include <dolphin/os.h>

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
  GXSetCopyClear(
      (GXColor){
          .r = 0,
          .g = 0,
          .b = 100,
          .a = 255,
      },
      GX_MAX_Z24);
}

int main(int argc, char* argv[]) {
  const AuroraConfig config = {
      .appName = "Demo",
      .logCallback = &log_callback,
      .allowCpuAdapter = true, /* spike: headless Chrome may only offer SwiftShader */
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
