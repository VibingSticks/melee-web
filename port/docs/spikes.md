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

## G1 — naga.wasm translates Aurora's WGSL to GLSL ES 3.00 (2026-09-11)

**Result: PASS with one rule.** `port/tools/build_naga.sh` builds naga 30.0.1
(`wgsl-in`, `glsl-out`, C ABI, no wasm-bindgen) to a 1.1 MB `naga.wasm` in
18 s. `port/tests/naga/translate_test.mjs` translates Aurora's EFB clear and
present shaders and a hand-written compat-profile GX shader, and links the
GLSL in headless Chrome (SwiftShader).

- Everything translates; the reflection info gives per-stage uniform block
  names (`Uniform_block_0Vertex` / `Uniform_block_0Fragment`) and combined
  texture-sampler names (`_group_2_binding_0_fs`), which is what the polyfill
  binds by.
- **Rule for the compat-profile WGSL: no `extractBits`.** naga emits
  `bitfieldExtract`, which is GLSL ES 3.10; ES 3.00 rejects it. Shifts and
  masks translate fine. `bitcast<f32>`, `select`, `textureLoad` on
  `texture_2d<u32>`, `textureSampleBias`, `discard`, `@builtin(instance_index)`
  and a dynamic-offset uniform block all link.
- Rust is not part of `tools/setup.sh`; rustup lives in `~/.cargo`
  (`rustup-init -y --no-modify-path --profile minimal --target wasm32-unknown-unknown`).

## G-A — Aurora compat profile verified on real WebGPU (2026-09-11)

`port/spikes/aurora-web` now draws two GX triangles (direct attributes, and positions from a `GXSetArray` array via `GX_INDEX8`). With `?gpu=noimm`, `?gpu=nostorage` and `?gpu=compat` the frames are pixel-identical to the default profile and the console shows no WebGPU errors, so immediates-in-uniform and texture-based vertex pulling are behaviourally equivalent. The triangles come out black rather than the material colour in every profile; that is a spike TEV/channel setup detail, not a profile difference, and is left as-is.

## G-C — WebGL2 polyfill runs Aurora and boots the game (2026-09-11)

`port/web/js/gpu-gl2.js` implements the WebGPU surface emdawnwebgpu calls on WebGL2, with naga.wasm translating WGSL at pipeline creation.

- **Unit tests:** `node port/tests/browser/gl2_polyfill_test.mjs` (headless Chrome, SwiftShader) — 7/7 pass: adapter limits, buffer map/copy/readback in both directions, texture row order through `writeTexture`/`copyBufferToTexture`/`copyTextureToBuffer`, render-pass orientation and scissor, uniform block with dynamic offsets (array and typed-array forms), reversed-Z depth test, texture sampling orientation, vertex/index buffers with blending onto the canvas texture.
- **Spike:** `simple.html?renderer=webgl2` draws the same frame as WebGPU (blue clear, two black GX triangles, right way up) at 119 frames per 120 iterations, no WebGL errors.
- **Game:** `boot_probe.mjs … --query=renderer=webgl2` boots to the same first `OSReport` and the same synthetic-disc stop as WebGPU; `?gpu=compat` on real WebGPU does too.
- **Coordinate rule learned the hard way:** naga's `ADJUST_COORDINATE_SPACE` flip makes WebGPU row *r* land on GL row *r* of a texture attachment, so viewports and scissors must not be flipped (only front faces and the final canvas blit). The unit test caught a flipped scissor.
- **Not measured yet (G2–G5, need real game data):** frame time of the polyfill versus WebGPU, title-screen and match screenshots under both renderers.
