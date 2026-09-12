# WebGL2 Fallback Renderer — Design Spec

Date: 2026-09-11
Status: draft for review
Extends: `2026-09-10-web-port-design.md` (assumption 7 and decision D1 said
"WebGPU only, no WebGL fallback in v1"; this spec adds the fallback)

## 1. Goal

Run the port in browsers that have no WebGPU, using WebGL2, with the same
game build. The fallback must also make the existing WebGPU path work on
Firefox and Safari WebGPU, which lack the Chrome-only "immediates" feature
Aurora currently requires.

Non-goals: matching WebGPU performance, WebGL1, browsers without WebGL2
(none of consequence in 2026), rendering features Aurora does not already
implement.

## 2. What the survey established

Aurora (`port/extern/aurora`) has no rendering-backend abstraction: the
WebGPU C++ types are used directly across `lib/gfx`, `lib/gx`, `lib/webgpu`
and even the public `aurora/gfx.hpp`. Roughly 4,300 lines are WebGPU-specific
and 4,500 more are backend-neutral logic wrapped in WebGPU types. A native
WebGL2 backend inside Aurora would be a multi-week rewrite and a permanent
fork of upstream.

Aurora's use of WebGPU is otherwise conservative. It needs render pipelines,
bind groups with one dynamic uniform offset, index buffers, buffer/texture
copies, mappable staging buffers, MSAA optional, `Depth32Float` with
reversed Z, and no indirect draws, bundles or queries. Only three things
have no WebGL2 equivalent:

1. **Immediates** (`SetImmediates`, `var<immediate>`): 64 bytes of per-draw
   data (vertex stream offset, matrix index, 12 array offsets).
2. **Storage buffers in the vertex stage**: all GX vertex data is fetched by
   index from two `array<u32>` storage buffers, with big-endian decoding in
   WGSL. WebGL2 has no storage buffers.
3. **A compute pass** for `GXPeekZ` depth readback. Melee never calls
   `GXPeekZ`.

Everything Aurora does at the JavaScript level goes through `emdawnwebgpu`,
whose JS side calls a well-defined subset of the browser WebGPU API
(`requestAdapter`, `requestDevice`, buffers, textures, samplers, bind groups,
pipelines, command encoders, render passes, copies, `mapAsync`, canvas
context). No existing WebGPU-over-WebGL2 polyfill is production grade, and
wgpu's WebGL2 backend has no storage buffers and does not target Emscripten.

## 3. Design

Three parts, each independently testable:

```
 game + Aurora (wasm, unchanged webgpu.h usage)
        │  webgpu.h
 emdawnwebgpu (unchanged)
        │  browser WebGPU JS API
 ┌──────┴───────────────────────────────┐
 │ real navigator.gpu   |  gpu-gl2.js   │   ← part B: WebGPU-subset over WebGL2
 │ (Chrome/Firefox/…)   |  + naga.wasm  │   ← part C: WGSL→GLSL ES 3.00 translator
 └──────────────────────────────────────┘
        ▲
 part A: Aurora "compat profile" chosen from adapter limits
```

### A. Aurora compatibility profile (in the Aurora patch)

Selected per adapter at device creation, from the limits it reports:

| Capability probe | Profile switch |
|---|---|
| `maxImmediateSize < 64` (Firefox, Safari, WebGL2) | Per-draw immediates move into the per-draw uniform block. `DrawImmediateData` is appended to the `Uniform` struct Aurora already binds with a dynamic offset (group 1); `SetImmediates` is not called; the shader reads `ubuf.imm_*`. |
| `maxStorageBuffersPerShaderStage == 0` (WebGL2) | The vertex and storage buffers become two `rgba32uint` textures of width 2048 (5 MB → 2048×160 texels, 8 MB → 2048×256). `push_verts`/`push_storage` keep their bump allocators; the per-frame high-water copy uses `CopyBufferToTexture` instead of `CopyBufferToBuffer`. The WGSL fetch helpers take `texture_2d<u32>` and use `textureLoad(vbuf, vec2u(w & 2047u, w >> 11u), 0)` instead of `p[w]`; `arrayLength` guards become size constants. Bind group 0 holds two texture views instead of two storage buffers. |
| `maxComputeWorkgroupsPerDimension == 0` (WebGL2) | Depth peek is disabled; `GXPeekZ` returns the far plane. |
| No `TextureComponentSwizzle` | Already handled by Aurora (CPU expansion). |

Each switch is a runtime flag in `ShaderConfig`/`GraphicsConfig`, so one
wasm build serves every browser, and each can be forced on Chrome for
testing (`AuroraConfig.forceCompatProfile`, set from a page query string).
Both new WGSL variants are valid standard WGSL, which matters for part C.

### B. `gpu-gl2.js`: WebGPU subset over WebGL2

A JavaScript module that installs a `navigator.gpu`-shaped object when the
real one is absent, or when the page is loaded with `?renderer=webgl2`. It
implements exactly the surface `emdawnwebgpu`'s JS calls (listed in §2),
with behaviour defined by mapping to WebGL2:

| WebGPU | WebGL2 |
|---|---|
| `GPUBuffer` (`COPY_DST`, `INDEX`, `UNIFORM`) | `gl.createBuffer` with the matching target; `mapAsync(WRITE)` returns a JS `ArrayBuffer` and `unmap` uploads with `bufferSubData` |
| `GPUBuffer` (`STORAGE`) | not offered: `limits.maxStorageBuffersPerShaderStage = 0` |
| `copyBufferToBuffer` | `gl.copyBufferSubData` |
| `GPUTexture` (2D, `rgba8unorm`, `r8`, `rg8`, `r16sint`, `rgba32uint`, `r32float`, `depth32float`) | `texStorage2D` + `texSubImage2D`; MSAA targets are multisample renderbuffers |
| `copyBufferToTexture` | `PIXEL_UNPACK_BUFFER` + `texSubImage2D` (GPU-side, no readback) |
| `copyTextureToTexture` | `blitFramebuffer` between two cached FBOs |
| `GPUSampler` | `gl.createSampler` (wrap, filters, LOD clamps, anisotropy via `EXT_texture_filter_anisotropic` when present) |
| `GPUBindGroupLayout` / `GPUBindGroup` | Tables; applied at draw time as `bindBufferRange`, `activeTexture` + `bindTexture` + `bindSampler` |
| `GPUShaderModule` | WGSL text translated by part C when the pipeline is created (per entry point) |
| `GPURenderPipeline` | A linked program plus a state vector (blend, depth compare/write, cull, front face, color mask, topology, depth bias) applied at `setPipeline`; uniform blocks and samplers bound by the names naga's reflection reports |
| `beginRenderPass` | A cached FBO for the (color view, depth view) pair; `LoadOp::Clear` → `clearBufferfv/fi`; the canvas' current texture maps to framebuffer 0 |
| `setViewport` / `setScissorRect` | y-flipped as wgpu's GL backend does; `setBlendConstant` → `blendColor` |
| `draw` / `drawIndexed` | `drawArraysInstanced` / `drawElementsInstanced` (no vertex attributes: GX draws pull by `gl_VertexID`) |
| `queue.submit` | Commands are recorded as closures at encode time and executed in order on submit |
| `queue.onSubmittedWorkDone`, `mapAsync` | Resolved on the next macrotask; `gl.fenceSync` polling is unnecessary for correctness |
| Canvas context | `configure` records the format; `getCurrentTexture` returns a texture object that stands for framebuffer 0; presentation is implicit when the wasm yields |
| Errors | `pushErrorScope`/`popErrorScope`/`onuncapturederror` with `GPUValidationError`-shaped objects; `device.lost` resolves on `webglcontextlost` |
| Not implemented (throws `GPUValidationError`) | compute passes, render bundles, indirect and multi-draw, occlusion/timestamp queries, `setImmediates`, storage buffers |

Clip-space and framebuffer-orientation differences follow wgpu-hal's GL
backend exactly: the translator flips Y and remaps Z in the vertex stage
(`ADJUST_COORDINATE_SPACE`), and the polyfill flips viewport, scissor and
copy rectangles to match. This is the highest-risk detail and gets its own
tests (render to texture, then sample it, in both orientations).

### C. `naga.wasm`: WGSL → GLSL ES 3.00

naga (the wgpu project's shader translator, Rust, MIT/Apache) compiled to
`wasm32-unknown-unknown` with `wgsl-in` and `glsl-out`, wrapped by a
~100-line Rust crate exposing `translate(wgsl, entry_point, stage) →
{ glsl, reflection }`. The reflection data (uniform block and combined
texture-sampler names per group/binding) is what part B uses to bind
resources by name. Built by `port/tools/build_naga.sh` into
`port/web/js/naga/`; about 1–2 MB of wasm, loaded only when the fallback is
active and inlined into the single-file build like everything else.

Aurora's WGSL is plain WGSL once part A removes `var<immediate>`; the
translator's first test is Aurora's own static shaders (EFB clear, present
copy, resample, the 16 EFB-copy conversions, the palette shaders).

### Selection at boot

`boot.js`: if `navigator.gpu` exists and `?renderer=webgl2` is not set, use it.
Otherwise load `gpu-gl2.js` and `naga.wasm`, install the polyfill, then start
the wasm unchanged. Aurora sees an adapter reporting no immediates, no
storage buffers, no compute, and picks the compat profile. The page shows
which renderer is active.

## 4. Why not the alternatives

- **Native WebGL2 backend in Aurora**: the survey's numbers (§2). Also a
  permanent divergence from upstream Aurora, which the patch queue could not
  carry.
- **Reimplement `webgpu.h` in C over WebGL2**: would duplicate emdawnwebgpu's
  future/event machinery and the C++ object layer; the JS API is the
  narrower and better-specified seam.
- **Hand-written GLSL emitter in Aurora**: a second 2,000-line shader
  generator to keep in sync with the WGSL one; naga makes that unnecessary.
- **Do nothing**: WebGPU is Baseline since January 2026, but the immediates
  requirement alone excludes current Firefox and Safari, and the ~5–10 % tail
  without WebGPU is exactly the audience a `file://` offline build reaches.

## 5. Risks and spikes

| # | Risk | Spike |
|---|---|---|
| G1 | naga's GLSL ES 3.00 backend rejects something Aurora's WGSL uses (`extractBits`, `bitcast`, `textureLoad` on `texture_2d<u32>`, depth textures) | Build naga.wasm, translate Aurora's static shaders and a captured GX shader; fix by adjusting the WGSL in the compat profile if needed |
| G2 | Texture-based vertex pulling is too slow | Measure a match scene with the compat profile forced on Chrome's real WebGPU (isolates the fetch cost from WebGL2) |
| G3 | Orientation/clip-space mismatches produce flipped EFB copies | Polyfill unit tests: render to texture then sample; compare pixels with the WebGPU path |
| G4 | WebGL2 UBO limits (16 KB minimum `MAX_UNIFORM_BLOCK_SIZE`) versus Aurora's `MaxUniformSize` 3840 | Within limits; verify on a Mesa/ANGLE stack |
| G5 | Per-draw overhead of binding state in JS | Profile with Chrome's tracing; batch state application, cache FBOs and programs |

## 6. Testing

- naga translator: a Node test that translates each static Aurora shader and
  a captured GX shader and checks the GLSL compiles in headless Chrome.
- Polyfill unit tests (Playwright, real Chrome forced to WebGL2 via
  `?renderer=webgl2`): buffer map/unmap round trip, copies, render to
  texture and readback, dynamic uniform offsets, blend/depth state,
  orientation.
- Aurora `simple` example (spike S1's build) under the polyfill: blue screen.
- Game boot under the polyfill: same milestone checklist as the WebGPU path,
  run twice.
- Compat profile forced on real WebGPU (Chrome): the title screen and a match
  must render identically to the default profile.

## 7. Milestones

- **G-A** Aurora compat profile: immediates → uniform (unlocks Firefox and
  Safari WebGPU immediately), then storage → textures, depth peek gating,
  force flag. Verified on Chrome with the profile forced.
- **G-B** naga.wasm translator with tests on Aurora's shaders.
- **G-C** `gpu-gl2.js`: enough for the `simple` example (device, buffers,
  textures, one pipeline, one pass, present), then the full subset for the
  game.
- **G-D** Boot the game under the polyfill; fix until the milestone
  checklist matches the WebGPU path.
