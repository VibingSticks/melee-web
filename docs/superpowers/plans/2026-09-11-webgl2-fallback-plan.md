# WebGL2 Fallback Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the same wasm build render in browsers without WebGPU (via WebGL2) and in Firefox/Safari WebGPU (which lack immediates), without forking Aurora's renderer.

**Architecture:** Aurora keeps using `webgpu.h` through `emdawnwebgpu`. A capability-driven "compat profile" inside the Aurora patch removes the two constructs WebGL2 cannot express (per-draw immediates become part of the dynamic-offset uniform block; storage-buffer vertex pulling becomes `textureLoad` from `rgba32uint` textures). Underneath, `port/web/js/gpu-gl2.js` implements the subset of the browser WebGPU API that `emdawnwebgpu` calls, on WebGL2, translating WGSL to GLSL ES 3.00 at pipeline creation with naga compiled to wasm.

**Tech Stack:** Aurora C++20 patch, WGSL, JavaScript (ES modules), WebGL2, Rust (naga 30, `wasm32-unknown-unknown` via rustup in `~/.cargo`), Playwright for browser tests.

**Spec:** `docs/superpowers/specs/2026-09-11-webgl2-fallback-design.md`

## Global Constraints

- One wasm build serves all browsers; the profile is chosen at runtime from adapter limits (`maxImmediateSize < 64`, `maxStorageBuffersPerShaderStage == 0`, no compute), and can be forced on Chrome with `?renderer=webgl2` (polyfill) or `?gpu=compat` (compat profile on real WebGPU).
- Aurora changes live in `port/extern/aurora-patches/0001-emscripten-build.patch` (regenerate with `git -C port/extern/aurora diff`); no Aurora file is edited outside the checkout + patch flow.
- The polyfill implements exactly the JS API surface `emdawnwebgpu` calls (spec §2, §3B); anything else throws a `GPUValidationError`-shaped error naming the call.
- Orientation and clip space follow wgpu-hal's GL backend: `ADJUST_COORDINATE_SPACE` in naga, y-flipped viewport/scissor/copies in the polyfill.
- `naga.wasm` is a build artifact (`port/tools/build_naga.sh` → `port/web/js/naga/naga.wasm`, git-ignored), produced in CI like `melee.wasm`.
- All new JS is plain ES modules with no bundler; tests run with Node 26 and Playwright's Chrome.

---

## File structure

| Path | Responsibility |
|---|---|
| `port/tools/naga-wasm/{Cargo.toml,src/lib.rs}` | naga wrapper: `nw_translate(wgsl, entry, stage) → JSON {glsl, uniforms, textures}` with a C ABI |
| `port/tools/build_naga.sh` | Builds `port/web/js/naga/naga.wasm` |
| `port/web/js/naga/naga.js` | Loader: `loadNaga(source) → { translate(wgsl, entry, stage) }` |
| `port/tests/naga/fixtures/*.wgsl` | Aurora's static shaders and a GX-style compat-profile shader |
| `port/tests/naga/translate_test.mjs` | Translates each fixture and compiles the GLSL in headless Chrome |
| `port/web/js/gpu-gl2.js` | The WebGPU-subset-over-WebGL2 implementation (`installWebGL2Fallback(canvas, naga)`) |
| `port/tests/browser/gl2_polyfill_test.mjs` + `gl2_polyfill_page.html` | Polyfill unit tests (map/unmap, copies, render-to-texture orientation, dynamic offsets, depth, sampling, blending, canvas) |
| `port/web/js/boot.js` | Renderer selection (`navigator.gpu` or the fallback), status line |
| Aurora patch: `lib/gx/shader.cpp`, `lib/gx/shader_info.cpp`, `lib/gx/gx.hpp`, `lib/gx/pipeline.cpp`, `lib/gx/command_processor.cpp`, `lib/gfx/frame.cpp`, `lib/gfx/encoding.cpp`, `lib/gfx/resources.hpp`, `lib/webgpu/gpu.cpp`, `include/aurora/aurora.h` | Compat profile |

---

## Milestone G-B — Translator (done first: it de-risks everything else)

### Task 1: naga.wasm builds and translates Aurora's static shaders

> **Done 2026-09-11 (spike G1 PASS).** See `port/docs/spikes.md`. Finding: compat-profile WGSL must not use `extractBits`.

**Files:**
- Create: `port/tools/naga-wasm/Cargo.toml`, `port/tools/naga-wasm/src/lib.rs`, `port/tools/build_naga.sh`, `port/web/js/naga/naga.js`
- Create: `port/tests/naga/fixtures/efb_clear.wgsl` (from `lib/gfx/clear.cpp`), `port/tests/naga/fixtures/gx_compat.wgsl` (hand-written, exercises `textureLoad` on `texture_2d<u32>`, `extractBits`, `bitcast<f32>`, `select`, `textureSampleBias`, a dynamic-offset uniform block, `@builtin(instance_index)`, `discard`)
- Create: `port/tests/naga/translate_test.mjs`

**Interfaces:**
- Produces: `naga.translate(wgsl, entryPoint, 'vertex'|'fragment') → { glsl: string, uniforms: [{group, binding, name}], textures: [{name, texture: [g,b], sampler: [g,b]}] }`; throws `Error('naga: …')` on failure.

- [ ] **Step 1: Build** — `bash port/tools/build_naga.sh` → `port/web/js/naga/naga.wasm` exists, under 2 MB.
- [ ] **Step 2: Write the test**

```js
// port/tests/naga/translate_test.mjs — node translate_test.mjs
import { readFileSync, readdirSync } from 'node:fs';
import { loadNaga } from '../../web/js/naga/naga.js';
const naga = await loadNaga(readFileSync(new URL('../../web/js/naga/naga.wasm', import.meta.url)));
let failed = 0;
for (const f of readdirSync(new URL('./fixtures', import.meta.url))) {
  const wgsl = readFileSync(new URL(`./fixtures/${f}`, import.meta.url), 'utf8');
  for (const [entry, stage] of [['vs_main', 'vertex'], ['fs_main', 'fragment']]) {
    if (!wgsl.includes(`fn ${entry}`)) continue;
    try {
      const r = naga.translate(wgsl, entry, stage);
      if (!r.glsl.startsWith('#version 300 es')) throw new Error('not GLSL ES 3.00');
      console.log(`ok   ${f} ${entry}: ${r.glsl.length} chars, ${r.uniforms.length} uniform blocks, ${r.textures.length} textures`);
    } catch (e) { failed++; console.log(`FAIL ${f} ${entry}: ${e.message}`); }
  }
}
process.exit(failed ? 1 : 0);
```

- [ ] **Step 3: Run** — Expected: every fixture/entry prints `ok`. Fixture failures name the WGSL construct naga's GLSL backend rejects; adjust the compat-profile WGSL (Task 4) rather than naga.
- [ ] **Step 4: Compile check in Chrome** — `port/tests/naga/compile_test.mjs` (Playwright, headless Chrome, `--use-angle=swiftshader` is fine for compiling): for each translated pair, `gl.createShader`/`compileShader`/`linkProgram`; print the info log on failure. Expected: all link.
- [ ] **Step 5: Commit** — `git add port/tools/naga-wasm port/tools/build_naga.sh port/web/js/naga port/tests/naga` — `port: naga.wasm WGSL→GLSL translator and fixture tests (WebGL2 fallback G-B)`.

---

## Milestone G-A — Aurora compat profile

### Task 2: Profile flags and capability probing

> **Done 2026-09-11.** `AuroraConfig.forceCompatProfile` + `AURORA_COMPAT_*` bits in `aurora.h`; `gpu.cpp` probes `maxImmediateSize`, `maxStorageBuffersPerShaderStage` and `maxComputeWorkgroupsPerDimension`, adjusts the required limits, logs `Compatibility profile: …` and stores the bits in `g_graphicsConfig.profile`; `ShaderConfig.noImmediates/noStorage`; depth peek stays off under `NO_COMPUTE`. The page passes `?gpu=noimm|nostorage|nocompute|compat` through `Module.forceCompatProfile`.

**Files:**
- Modify (Aurora): `include/aurora/aurora.h` (`AuroraConfig.forceCompatProfile: uint32_t` bitmask), `lib/webgpu/gpu.cpp` (probe adapter limits; only add `maxImmediateSize` to `requiredLimits` when the adapter's limit is ≥ 64; set `g_graphicsConfig.profile` bits `PROFILE_NO_IMMEDIATES`, `PROFILE_NO_STORAGE`, `PROFILE_NO_COMPUTE`), `lib/gx/gx.hpp` (`ShaderConfig` gains `noImmediates:1`, `noStorage:1`, part of the pipeline hash), `lib/gfx/frame.cpp` (`depth_peek::initialize()` skipped under `PROFILE_NO_COMPUTE`; `GXPeekZ` returns `0xFFFFFF`).
- Modify: `port/src/main_loop.c` (read `Module.forceCompatProfile` via an exported setter called from `boot.js`).

- [ ] **Step 1:** Add the flags; log the chosen profile at init (`[aurora::gpu] profile: immediates=…, storage=…, compute=…`).
- [ ] **Step 2:** Build; run the boot probe on Chrome with and without `?gpu=compat`; the log line shows both profiles; behaviour unchanged (compat bits not yet consumed).
- [ ] **Step 3:** Regenerate the patch; commit.

### Task 3: Immediates → uniform block

> **Done 2026-09-11.** Under `noImmediates` the shader has no `var<immediate>`; `DrawImmediateData` is appended (16-aligned, `@align(16)`) to the `Uniform` block, `build_uniform` takes the immediates, the draw cache rebuilds the uniform when they change, the pipeline layout has `immediateSize = 0` and `render()` skips `SetImmediates`. `MaxUniformSize` is 4096. Verified: spike `simple` draws a GX triangle identically with and without `?gpu=noimm` on Chrome; the game boot probe is unchanged under `?gpu=noimm`. Pixel comparison with real game data is still pending (M2).

**Files:**
- Modify (Aurora): `lib/gx/shader.cpp` (when `noImmediates`: no `var<immediate> imm`; `struct Uniform` ends with `imm_vtx_start: u32, imm_current_pnmtx: u32, imm_array_start0..2: vec4u`; every `imm.x` reference emitted as `ubuf.imm_x`), `lib/gx/shader_info.cpp` (`build_uniform` appends `DrawImmediateData` when `noImmediates`; `MaxUniformSize` grows by 64), `lib/gx/gx.cpp` (`immediateSize = 0` in the pipeline layout when `noImmediates`), `lib/gx/pipeline.cpp` (skip `SetImmediates` when `noImmediates`), `lib/gx/command_processor.cpp` (`DirtyImmediates` implies `DirtyUniform` under the profile).

- [ ] **Step 1:** Implement; keep the default path byte-identical.
- [ ] **Step 2:** Verify on Chrome with `?gpu=compat`: spike `simple` (blue) and the game boot probe both run; with real game data the title screen must match the default profile pixel-for-pixel (Playwright screenshot diff, `maxDiffPixelRatio: 0`).
- [ ] **Step 3:** Regenerate the patch; commit. **This step alone enables Firefox and Safari WebGPU.**

### Task 4: Storage buffers → `rgba32uint` textures

> **Done 2026-09-11.** Under `noStorage` the generated WGSL is post-processed (`ptr<storage, array<u32>>` → `texture_2d<u32>`, `load_word` → `textureLoad` on a 2048-texel-wide `rgba32uint` texture with one `u32` per component, bind group 0 holds two texture views), the frame resources create the two data textures, and the per-frame high-water copies go row by row through `CopyBufferToTexture`. `extractBits` was replaced by an `ebits` shift/mask helper in every profile so the WGSL translates to GLSL ES 3.00. Verified: the spike draws a direct-attribute triangle and a `GXSetArray`/`GX_INDEX8` triangle identically under default, `noimm`, `nostorage` and `compat` profiles on Chrome. Frame-time measurement (G2) still pending real game data.

**Files:**
- Modify (Aurora): `lib/gfx/resources.hpp` + `frame.cpp` (under `PROFILE_NO_STORAGE`: create `vertexTexture` 2048×⌈5 MiB/32 KiB⌉ and `storageTexture` 2048×⌈8 MiB/32 KiB⌉, `rgba32uint`, usage `TextureBinding|CopyDst`; bind group 0 holds their views), `lib/gfx/encoding.cpp` (`copy_staging_to_high_water`: `CopyBufferToTexture` rows of 32 KiB with `bytesPerRow = 32768`), `lib/gx/shader.cpp` (when `noStorage`: `@group(0) @binding(0) var vbuf: texture_2d<u32>;` and `load_word(t, w) = textureLoad(t, vec2u(w & 2047u, w >> 11u), 0)[w-component]` — store one `u32` per texel component so `word_idx` maps to `(texel = w >> 2, comp = w & 3)`; all `ptr<storage, array<u32>>` parameters become `texture_2d<u32>`), `lib/gx/gx.cpp` (bind group layout 0 entries become sampled-texture entries of `sampleType: uint`).

- [ ] **Step 1:** Implement; the fixture `gx_compat.wgsl` from Task 1 must be exactly the WGSL this emits for a representative config (update the fixture from a captured shader: add a debug env `AURORA_DUMP_SHADERS` that writes each generated WGSL to the console once).
- [ ] **Step 2:** Verify on Chrome `?gpu=compat`: title screen and a match identical to the default profile; note the frame time difference (spike G2).
- [ ] **Step 3:** Regenerate the patch; commit.

---

## Milestone G-C — `gpu-gl2.js`

### Task 5: Device, buffers, textures, samplers, errors

> **Done 2026-09-11 (Tasks 5–7 together).** Deviation: the polyfill is one 740-line module, `port/web/js/gpu-gl2.js`, rather than the planned `gpu-gl2/*.js` split; the object families are small enough to read in one file and share the GL context, format tables and draw-state cache. Tests live in one page, `port/tests/browser/gl2_polyfill_page.html`, driven by `gl2_polyfill_test.mjs` in headless Chrome (SwiftShader): 7 tests, all pass. Finding: because naga's `ADJUST_COORDINATE_SPACE` flips clip-space Y, WebGPU row *r* is GL framebuffer row *r* of the attached texture, so viewports, scissors and copies are **not** flipped; only front faces are inverted and the final canvas blit flips. Buffers with `MAP_READ`/`MAP_WRITE` live in JS `ArrayBuffer`s; `mapAsync(READ)` on a device buffer pulls with `getBufferSubData`.

**Files:** `port/web/js/gpu-gl2.js`, `gpu-gl2/errors.js`, `gpu-gl2/buffers.js`, `gpu-gl2/textures.js`

**Interfaces:** `installWebGL2Fallback({ canvas, naga }) → { gpu }` sets `navigator.gpu` (configurable property) to an object with `requestAdapter()`, `getPreferredCanvasFormat() → 'rgba8unorm'`, `wgslLanguageFeatures: new Set()`. The adapter reports `features: Set()`, `limits` (`maxStorageBuffersPerShaderStage: 0`, `maxImmediateSize: 0`, `minUniformBufferOffsetAlignment: 256`, `maxUniformBufferBindingSize: gl.MAX_UNIFORM_BLOCK_SIZE`, texture dimension limits from `gl.MAX_TEXTURE_SIZE`, `maxComputeWorkgroupsPerDimension: 0`), `info: { vendor: 'webgl2-fallback', device: gl RENDERER string }`, `isFallbackAdapter: true`.

- [ ] Tests (`gl2_buffers.spec.mjs`): `createBuffer(MAP_WRITE|COPY_SRC)` → `mapAsync` → `getMappedRange` → write → `unmap` → `copyBufferToBuffer` into an `INDEX` buffer → readback via a render (indices drawn) or `gl.getBufferSubData`; `writeBuffer`; `destroy`. `gl2_textures.spec.mjs`: `createTexture` for each supported format, `writeTexture`, `copyBufferToTexture`, `createView`, `createSampler` parameters round-trip via `gl.getSamplerParameter`.
- [ ] Commit.

### Task 6: Shader modules, pipelines, bind groups

> **Done 2026-09-11** (see Task 5). Programs are cached per (vertex module, entry, fragment module, entry); uniform blocks and combined samplers bind by naga's reflection names; the sampler for a texture binding is read from the bind group named by the reflection's `sampler: [group, binding]` pair (integer textures get a NEAREST default sampler).

**Files:** `gpu-gl2/pipelines.js`

- `createShaderModule({code})` stores WGSL; `createRenderPipeline` translates `vertex.entryPoint` and `fragment.entryPoint` with naga, compiles, links, and resolves each bind group layout entry to a uniform block index (`getUniformBlockIndex` + `uniformBlockBinding`) or a texture unit (`getUniformLocation` on naga's combined sampler name) using the reflection data; caches by (module, entry points, state hash).
- The state vector: primitive topology → GL mode (triangle strip/list, lines, points), cull/front face, depth compare/write/bias, blend (color and alpha factors and operations, `CONSTANT` factors), color write mask, sample count.
- [ ] Tests: translate + link the Task 1 fixtures through the pipeline API; a pipeline with two bind groups binds a UBO at a dynamic offset and a texture.
- [ ] Commit.

### Task 7: Command encoder, render passes, copies, queue, canvas

> **Done 2026-09-11** (see Task 5). The canvas texture is a regular `rgba8unorm` texture (FBO-cached like any other) and `queue.submit` ends with a y-flipping `blitFramebuffer` to framebuffer 0 when a pass or copy touched it. MSAA colour attachments become renderbuffers with `resolveTarget` blits; nothing in Aurora's web build uses them yet.

**Files:** `gpu-gl2/passes.js`, `gpu-gl2/queue.js`

- Encoder records closures; `submit` runs them. `beginRenderPass`: FBO from a cache keyed by attachment views (framebuffer 0 for the canvas texture); `loadOp: 'clear'` → `clearBufferfv/fi` with the scissor disabled; MSAA color attachments → renderbuffers, `resolveTarget` → `blitFramebuffer`. Pass ops: `setPipeline` (program + state), `setBindGroup` (with dynamic offsets), `setIndexBuffer`, `setVertexBuffer` (rmlui only; may be unsupported), `setViewport`/`setScissorRect` (y-flipped), `setBlendConstant`, `draw`/`drawIndexed` (instanced variants), debug groups as no-ops.
- Copies: `copyBufferToTexture` via `PIXEL_UNPACK_BUFFER` with `UNPACK_ROW_LENGTH = bytesPerRow / bytesPerTexel`; `copyTextureToTexture` via `blitFramebuffer` (y-flipped rectangles).
- `queue.onSubmittedWorkDone` and `mapAsync` resolve on `setTimeout(0)`.
- Canvas: `getContext('webgpu')` returns `{ configure, unconfigure, getCurrentTexture }`; `getCurrentTexture()` is a texture object flagged `isCanvas`, whose FBO is 0 and whose size follows `canvas.width/height`.
- [ ] Tests (`gl2_render.spec.mjs`): render a triangle to a texture, copy it to the canvas, read pixels; render to texture, sample it in a second pass, assert orientation (a marker in the top-left of the WebGPU coordinate system stays top-left); depth test with reversed Z.
- [ ] Commit.

### Task 8: Aurora `simple` under the polyfill

> **Done 2026-09-11.** `shell.html` loads `naga.js`, `gpu-gl2.js` and `naga.wasm` from `port/web/js` (serve `port/` and open `spikes/aurora-web/build/simple.html?renderer=webgl2`); Emscripten's `{{{ SCRIPT }}}` placeholder is neutralised in an HTML comment and `simple.js` is appended by the module loader. First run rendered the blue clear and both GX triangles identically to the WebGPU path, 119 frames in 120 iterations, no WebGL errors, `Compatibility profile: immediates=false storageBuffers=false compute=false`. See `port/docs/spikes.md` G-C.

- [ ] `port/spikes/aurora-web` gains `?renderer=webgl2` handling in `shell.html` (loads `gpu-gl2.js` + `naga.wasm`, installs the fallback before `simple.js`). Expected: blue screen, `[aurora::gpu] Device: webgl2-fallback`, profile line shows all three compat bits. Fix polyfill gaps the log reveals.
- [ ] Commit.

---

## Milestone G-D — Game under the polyfill

### Task 9: Boot selection and status

- [x] `boot.js`: choose renderer (`navigator.gpu` unless `?renderer=webgl2`, fallback when absent), show "Renderer: WebGPU" / "Renderer: WebGL2 (fallback)" in the status bar, load `naga.wasm` lazily. Single-file packer (main plan Task 28) inlines `gpu-gl2.js` and `naga.wasm` (base64) too. *(Done 2026-09-11: `selectRenderer()` also falls back when `requestAdapter()` returns null, e.g. Firefox without a WebGPU adapter; `?renderer=webgpu` refuses to fall back. CMake copies `gpu-gl2.js` and `naga/` next to `melee.js`. The packer part waits for Task 28.)*
- [x] Boot probe with `?renderer=webgl2` reaches the same first `OSReport` as WebGPU *(done 2026-09-11 with the synthetic disc: identical log up to the SFX-bank stop; `boot_probe.mjs --query=renderer=webgl2`)*; then, with real data, the title screen (M2) and a match (M4) under both renderers; screenshots compared with `maxDiffPixelRatio: 0.02` (texture filtering may differ).
- [ ] Commit; record measurements in `port/docs/spikes.md` (G1–G5).
