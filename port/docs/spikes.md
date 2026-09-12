# Spike results

Each spike from spec §11 gets an entry: date, PASS/FAIL, measurements, and
what the result decided.

| # | Question | Result |
|---|---|---|
| S1 | Does Aurora's `simple` example build and run under Emscripten + emdawnwebgpu? | **PASS** (2026-09-11), see below |
| S2 | Is the Asyncify size/CPU cost acceptable for the full game? | **Size measured** (2026-09-11): release `melee.wasm` 11.6 MB + 260 KB JS with the whole game, Aurora and Asyncify, no `ASYNCIFY_ONLY` tuning yet; debug 36.7 MB. CPU cost still to measure with real game data |
| S3 | Which storage/audio APIs work on `file://` in Chrome, Firefox, Safari? | **Done** (2026-09-11) for Chrome 151 and Firefox 155; Safari not available on this machine. See below |
| S4 | Does any bitfield cross its storage unit under either ABI? | **PASS** (2026-09-11): 1405 bitfield members in 246 structs across 984 units, 0 crossings (`port/tools/check_bitfields.py`) |
| S5 | Are all pointer fields in game data listed in archive relocation tables? | not run (needs a disc image) |
| S6 | Peak wasm heap with MEM1 + ARAM + read cache? | not run |

## S1 — Aurora under Emscripten (2026-09-11)

**Result: PASS.** `port/spikes/aurora-web` builds Aurora (r-burns fork
`e6a6f02` + `port/extern/aurora-patches/0001-emscripten-build.patch`) and
its `simple` example to wasm. In Chrome 151 on the host GPU it renders the
example's blue clear color at display rate.

| Measurement | Value |
|---|---|
| Toolchain | Emscripten 6.0.9, emdawnwebgpu v20260910.214722 (remote port), SDL3 3.4.2 (Emscripten port) |
| Link flags | `-O3 -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=128MB -sASSERTIONS=1 -sSTACK_SIZE=1MB` |
| `simple.wasm` / `simple.js` | 5.83 MB / 324 KB (Release, Asyncify, no size tuning) |
| Headed Chrome 151, GTX 1660 SUPER | 239 frames in ~2 s after init, 0 skipped; screenshot center pixel `(0, 0, 100)` |
| Headless Chrome (SwiftShader CPU adapter) | device initializes, then Chrome reports `device.lost` reason `destroyed` after 3 frames with no error and no `destroy()` call from our side. Treat headless SwiftShader as unsupported for smoke tests; use a real GPU |

Run it: `emcmake cmake -S port/spikes/aurora-web -B port/spikes/aurora-web/build -G Ninja && cmake --build port/spikes/aurora-web/build`, serve `build/`, open `simple.html`. `run_pw.mjs` drives Chrome through Playwright (`HEADED=1` for the real GPU) and prints the console; headless `--screenshot` with `--virtual-time-budget` does not work because virtual time expires the WebGPU adapter wait.

### What the patch changes (all guarded by `EMSCRIPTEN` / `__EMSCRIPTEN__`)

Build:
- `cmake/AuroraDawnProvider.cmake`: no Dawn native; `dawn::webgpu_dawn` becomes an interface target carrying `--use-port=${AURORA_EMDAWNWEBGPU_PORT}`.
- `cmake/AuroraSDL3Provider.cmake`: `SDL3::SDL3` from Emscripten's `--use-port=sdl3`.
- `CMakeLists.txt`: no `CMAKE_POSITION_INDEPENDENT_CODE` (PIC objects use GOT globals that only side modules can link; Binaryen fails with "popping from empty stack").
- `cmake/aurora_core.cmake`, `cmake/aurora_gx.cmake`: no `WEBGPU_DAWN` define, no `lib/dawn/TracyPlatform.cpp`; define `ENABLE_BACKEND_WEBGPU` so `BACKEND_WEBGPU` is in the preferred backend list.
- `extern/CMakeLists.txt` + `cmake/patches/apply-imgui-emdawnwebgpu.cmake`: imgui 1.91.9b's wgpu backend rejects `IMGUI_IMPL_WEBGPU_BACKEND_DAWN` on Emscripten; emdawnwebgpu is the Dawn-flavoured API, so the check is removed.
- Third-party libs must be static: pass `-DBUILD_SHARED_LIBS=OFF` (zlib-ng otherwise builds a `.so`).

Code:
- `lib/dawn/BackendBinding.cpp`: surface from `EmscriptenSurfaceSourceCanvasHTMLSelector{"#canvas"}`.
- `lib/webgpu/gpu.cpp`: Dawn-only logging callback (`wgpu::LoggingType`) guarded out.
- `lib/aurora.cpp`: `wgpu::ConvertibleStatus` is Dawn-only; `wgpuSurfacePresent` is unsupported in browsers, presentation happens when the wasm yields.
- `lib/gfx/frame.cpp`: `wait_for_gpu_progress` yields with `emscripten_sleep` instead of `sleep_for`, otherwise the first frame spins forever waiting for a `MapAsync` callback that can only run on the event loop.
- `lib/gfx/render_worker.cpp`: no worker thread; `is_worker_thread()` is true when no worker runs (work executes inline).
- `lib/gx/fifo.cpp`: FIFO `ProcessingMode::Drain` instead of the worker thread.
- `lib/gfx/pipeline_cache.cpp`: no pipeline thread, no sqlite cache load/writer thread.
- `lib/gx/texture.hpp`: `AsyncTextureReplacements = false`.
- `lib/rmlui.hpp`: include `<webgpu/webgpu_cpp.h>` instead of `<dawn/webgpu_cpp.h>`.
- `lib/gfx/texture_replacement.cpp`: two `uint64_t` → `size_t` narrowing initialisers.

### Facts that change the plan

- **WebGPU immediates.** Aurora's GX pipeline uses `var<immediate>` and `SetImmediates`. Chrome shipped immediates in 149–150 (June 2026) and the device request succeeds with `maxImmediateSize = 64`. The emdawnwebgpu bundled with Emscripten 6.0.9 (v20260423) lacks `wgpuRenderPassEncoderSetImmediates`; the v20260910 remote port has it, so that file is tracked in `port/extern/` and pinned through `AURORA_EMDAWNWEBGPU_PORT`. Firefox/Safari support for immediates is unverified; that is the main browser-support risk for D1.
- **Timed `WaitAny` needs Asyncify (or JSPI).** Aurora's adapter/device requests use `WaitAny` with a timeout; emdawnwebgpu implements that via `Asyncify.handleAsync`, so Asyncify is required at least during init even before the game's own blocking waits are considered.
- **The main loop must yield every frame.** `emscripten_sleep(0)` after `aurora_end_frame()` is enough for the spike; the port's `main_loop.c` will use `emscripten_set_main_loop` instead.
- **Plan Task 9** needs no separate `AURORA_SINGLE_THREADED` option: the `__EMSCRIPTEN__` guards in this patch already cover every thread.

## S3 — `file://` capabilities (2026-09-11)

`port/spikes/file-url/index.html` opened from disk via `node run_pw.mjs chrome|firefox`
(Playwright, headless). Safari is not available here and stays unverified.

| Probe | Chrome 151 (headless) | Firefox 155 (Playwright build, headless) |
|---|---|---|
| secure context | yes | yes |
| SharedArrayBuffer / crossOriginIsolated | no / no | no / no |
| WebGPU (`navigator.gpu`) | yes | no (headless Linux build) |
| WebAssembly from bytes | ok | ok |
| localStorage | ok | ok |
| IndexedDB (512 KB write) | ok | ok |
| OPFS (`navigator.storage.getDirectory`) | SecurityError | ok |
| `File.slice().arrayBuffer()` | ok | ok |
| Worker from blob URL | ok | ok |
| AudioWorklet module from blob URL | **AbortError** ("Unable to load a worklet's module") | ok |
| AudioWorklet module from `data:` URL | ok | ok |
| ScriptProcessorNode | ok | ok |
| AudioBufferSourceNode queueing | ok | ok |
| Gamepad API | present | present |
| `showOpenFilePicker` | function exists; AbortError under headless (dialog auto-dismissed, so inconclusive) | absent |

Decisions:
- **Saves:** IndexedDB works on `file://` in both browsers, so IDBFS is the primary persistence path with export/import as the guaranteed backup (spec D7 stands). OPFS is not usable on `file://` in Chrome and is not needed.
- **Audio:** the single-file build must load its AudioWorklet processor from a `data:` URL, not a blob URL. ScriptProcessorNode remains a last-resort fallback.
- **Threads:** no SharedArrayBuffer on `file://`, confirming the single-threaded design.
- **Disc picking:** `<input type="file">` is the only path that works everywhere; File System Access handles are a hosted-build convenience only.

## S4 — bitfield audit (2026-09-11)

`python3 port/tools/check_bitfields.py` parses every `.c` under `src/melee` and
`src/sysdolphin` with libclang for `wasm32-unknown-emscripten` + `TARGET_PC`:
984 units, 246 structs with bitfields, 1405 bitfield members, **0** that straddle
their storage unit. The converter's per-unit MSB→LSB repacking (spec D2 step 3)
is therefore sufficient; no struct needs a hand-written swapper for bitfield
reasons. Runtime: 52 s on this machine.
