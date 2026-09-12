# Melee Web Port — Design Spec

Date: 2026-09-10
Status: draft for review
Repo baseline: `doldecomp/melee` master @ `c7861544f` (100% decompiled)

## 1. Goal

Run Super Smash Bros. Melee (GALE01, NTSC 1.02) in a web browser, built
from this decompilation, in two delivery forms:

| Form | What the user gets | How it runs |
|---|---|---|
| **Hosted** ("normal") | A directory of static files (`index.html`, `melee.js`, `melee.wasm`, assets) served by any static web server | Open the URL |
| **Single-file** ("offline") | One `melee-offline.html` with the engine embedded inline, in the style of Eaglercraft's offline download | Double-click the file; runs from `file://` with no server |

Both forms require the user to supply their own Melee disc image. No game
assets are distributed with either build.

A native desktop build is **not** a deliverable of this spec. The wasm build
plus browser DevTools is the development loop (see §10).

## 2. Assumptions

These were decided without the user present. Each is easy to reverse if
wrong; they are listed so they can be challenged.

1. "Normal version" means the normal *hosted web* build, not a native PC port.
2. Target region/version is GALE01 (NTSC-U 1.02), the only version this repo builds.
3. The port lives in this repository under a new top-level `port/` directory,
   on a branch or fork. The upstream README states ports are out of scope for
   `doldecomp/melee`, so this work is not expected to merge upstream as-is.
4. The user supplies a disc image in ISO/GCM format (uncompressed). Compressed
   formats (RVZ, NKit) are a later addition.
5. Local multiplayer with up to four gamepads is in scope. Online play is not.
6. The single-file build may be large (tens of MB); that is acceptable, as it
   is for Eaglercraft's offline HTML.
7. Browser floor: any browser with WebGPU enabled. Chrome/Edge stable qualify;
   Firefox and Safari qualify where WebGPU is shipped. No WebGL fallback in v1.

## 3. What the survey established

Facts that shape the design (all verified in-repo on 2026-09-10; details in
the survey notes summarised here):

- **100% C.** 1130 translation units; the only assembly is the MetroTRK
  exception vector, which the port does not build. Every inline `asm` block in
  `src/` is behind a `MWERKS_GEKKO`/`__MWERKS__` guard with a C fallback; the
  only unguarded blocks are 9 paired-single matrix routines in
  `extern/dolphin/src/dolphin/mtx/{mtx,vec}.c`, which Aurora replaces anyway.
- **A non-CodeWarrior build already compiles.** `.nix/CMakeLists.txt` builds
  `src/melee` + `src/sysdolphin` with GCC (i686) against Aurora's headers,
  defining `TARGET_PC` and `bool=int`, excluding 13 units. CI runs it as the
  `native-build` job. It is compile-only; nothing links or runs.
- **Single-threaded.** `OSCreateThread` is used once in game code, in the
  dead debug console. Concurrency is interrupts plus DVD/ARQ callbacks.
- **The frame loop is interrupt-paced.** `main()` in `src/melee/gm/gmmain.c`
  does init and never returns; the real loop is `gm_801A4D34()` in
  `src/melee/gm/gmscene.c:271`, which spins on a pad queue filled by the VI
  retrace path (`HSD_PadRenewMasterStatus`).
- **Blocking file loads exist.** `src/melee/lb/lbfile.c` issues
  `HSD_DevComRequest` and busy-waits in `waitForDisc()`.
- **All disc access funnels through 18 call sites** (`DVDConvertPathToEntrynum`
  ×12, `DVDFastOpen` ×3, `DVDReadAsyncPrio` ×3), plus ARAM staging through
  ARQ (29 sites).
- **Asset archives are big-endian and self-relocating.**
  `src/sysdolphin/baselib/archive.c` casts the file buffer to structs and adds
  the base address to every `u32` listed in the relocation table. 508 `.dat`
  and 23 `.usd` files use this format. Other formats: `.ssm` (55), `.hps`
  (99), `.thp` (75), `.mth` (29), `.sem` (1).
- **904 bitfield members** exist across the `types.h` headers. CodeWarrior on
  PowerPC packs bitfields MSB-first; clang for wasm32 packs LSB-first.
- **Two real fixed-address assumptions**: `src/melee/lb/lbfile.c:126`
  distinguishes MRAM from ARAM destinations by `dst >= 0x80000000`, and
  `src/melee/lb/lbmemory.c` sanity-checks arena pointers against
  `0x80000000`.
- **Aurora** (`encounter/aurora`, MIT, C++20; the `r-burns` fork is what the
  repo's native build pins) implements GX on WebGPU via Dawn's `webgpu.h`,
  plus PAD, VI, DVD (nod), CARD (kabufuda), AR/ARQ, mtx, and part of OS. It
  has no Emscripten support, one file with Dawn-native-only calls
  (`lib/webgpu/gpu.cpp`), threads for the render worker, pipeline cache and
  DVD reader, a sqlite pipeline cache, and **no AX/DSP/AI audio
  implementation**. Its GX layer already byte-swaps FIFO commands, display
  lists, textures, and vertex arrays (`GXSetArray` has an explicit
  little-endian flag), so GX-consumed data can stay big-endian.
- **Emscripten's WebGPU path** is the `emdawnwebgpu` port
  (`--use-port=emdawnwebgpu`, Emscripten 4.0.3+), which implements the same
  `webgpu.h` Aurora targets. Aurora's GX pipeline uses WebGPU *immediates*
  (`SetImmediates`), which Chrome shipped in 149–150 and which only
  emdawnwebgpu packages from September 2026 implement, so the port pins a
  newer remote port than the one bundled with Emscripten (spike S1).

## 4. Architecture

```
┌───────────────────────────────────────────────────────────────────┐
│ Browser page (hosted index.html  |  single-file melee-offline.html)│
│  • ISO picker / remembered handle    • save export/import          │
│  • canvas + gamepad hints            • boot status & error panel   │
├───────────────────────────────────────────────────────────────────┤
│ JS runtime glue (port/web/js)                                      │
│  DiscSource (File → range reads)   SaveStore (IDBFS + download)    │
│  AudioSink (AudioWorklet)          Emscripten pre/post JS          │
├───────────────────────────────────────────────────────────────────┤
│ melee.wasm  (Emscripten, wasm32, single thread, Asyncify)          │
│  ┌──────────────────────────────┐  ┌─────────────────────────────┐ │
│  │ Game: src/melee, src/sysdolphin│  │ port/src (port-only C/C++) │ │
│  │ unmodified except #ifdef      │  │  main loop adapter          │ │
│  │ TARGET_PC patches             │  │  hsd_endian: archive swapper│ │
│  └──────────────┬───────────────┘  │  dvd_web: DVD over DiscSource│ │
│                 │ Dolphin SDK API   │  ax_hle: AX/DSP/AI mixer    │ │
│  ┌──────────────┴───────────────┐  │  os_shim: OS/DC/IC/alarm    │ │
│  │ Aurora (patched for wasm)    │  │  thp: SDK THPDec C fallback │ │
│  │ GX→WebGPU, PAD, VI, CARD, AR │  └─────────────────────────────┘ │
│  └──────────────────────────────┘                                  │
│  SDL3 (Emscripten backend)   emdawnwebgpu   libc++/musl (Emscripten)│
└───────────────────────────────────────────────────────────────────┘
```

Layer rules:

- Game code calls only Dolphin SDK and HSD APIs. It is not edited except
  under `#ifdef TARGET_PC` (already the convention in `.nix/CMakeLists.txt`).
- Aurora provides the SDK where it already does. `port/src` fills the gaps
  and adapts Aurora to wasm. Aurora is vendored as a git submodule with a
  patch queue (`port/extern/aurora-patches/`) so upstream can be tracked.
- JS glue is the only code that touches browser APIs. wasm talks to it via a
  small explicit import table (`port/web/js/imports.js`), not `EM_ASM`.

## 5. Key decisions

### D1. Graphics: Aurora on WebGPU (not a hand-written GX→WebGL2 layer)

Aurora is ~55k lines and already matches GX semantics closely enough to run
completed ports. Its shaders fetch vertex data from storage buffers and
byte-swap in WGSL, a technique WebGL2 has no equivalent for. Writing a new GX
layer on WebGL2 is months of work with lower fidelity.

Cost: WebGPU-only browsers (assumption 7) and a port of Aurora itself to
Emscripten:

- `lib/webgpu/gpu.cpp`: guard `dawn::native` instance creation, toggles and
  cache descriptor with `#ifndef __EMSCRIPTEN__`; obtain the adapter/device
  through `webgpu.h` futures with `wgpuInstanceWaitAny` (works under
  Asyncify) or from a device the page pre-creates.
- Threads: every Aurora thread is compiled out under `__EMSCRIPTEN__` (render
  worker, FIFO worker, pipeline compile and cache-writer threads, texture
  replacement pool); queued work runs inline. The DVD reader thread is
  replaced entirely by `dvd_web`. Done in spike S1's patch.
- Pipeline cache: sqlite off; in-memory only. Persisting compiled pipelines
  is not possible in browsers anyway (the browser caches shader modules).
- Window/input: SDL3's Emscripten backend, unchanged.

### D2. Endianness: swap archives at load, driven by a type schema

wasm32 is little-endian; the assets are big-endian. Options considered:

- (a) Load-time in-place conversion in C, schema-driven. **Chosen.**
- (b) Offline conversion into a little-endian asset pack on first run. Same
  schema work, plus a cache layer and a second format. Rejected for v1; can be
  added later as a pure optimisation because (a) and (b) share the walker.
- (c) Big-endian accessor macros throughout game code. ~490k lines touched.
  Rejected.

Design of (a), implemented in `port/src/hsd_endian/`:

1. `HSD_ArchiveParse` is replaced under `TARGET_PC`. It swaps the 0x20 header,
   the relocation table, the public/extern symbol tables, then for each
   relocation offset swaps the `u32` at that offset **before** adding the base.
   After this step every pointer in the archive is correct. No schema needed.
2. For each public symbol, a table maps symbol name (or prefix, e.g. `ftData`,
   `map_head`, `coll_data`, `scene_data`, `SIS_`, `eff`) to a **type
   descriptor**. The walker traverses the object graph from each root and
   swaps non-pointer scalar fields. Pointers are skipped (already done in
   step 1) and every relocation-table offset is also held in a set so the
   walker asserts it never swaps a pointer slot. A visited set keyed by
   address guarantees each object is swapped once, which matters because HSD
   graphs share nodes (materials, textures, animations).
3. Descriptors express: primitive width; nested struct; pointer to struct;
   pointer to array with length given by a constant, a sibling field, or
   null-termination; discriminated unions (discriminator field + cases);
   bitfield groups (storage width plus ordered widths, re-packed from
   MSB-first to LSB-first); and **opaque blobs** (image data, display lists,
   vertex/index buffers, palettes) which stay big-endian for Aurora.
4. Descriptors are **generated** from the existing headers
   (`src/sysdolphin/baselib/*.h`, `src/melee/*/types.h`) by a Python +
   libclang script (`port/tools/gen_schema.py`), with array-length and union
   information supplied by a small hand-maintained annotation file
   (`port/schema/annotations.yml`). HSDRaw's public type definitions are used
   as a cross-check only, not copied. The generator emits C tables compiled
   into the wasm.
5. Coverage grows incrementally: boot files first, then title/menu, stage,
   fighters, items, effects, trophies. An unknown root symbol is a **hard
   error** at load in debug builds and a logged warning in release, so gaps
   are visible rather than silent corruption.
6. Non-archive formats get explicit hand-written swappers in the same module:
   `.ssm` and `.sem` (sound tables), `.hps` (stream header), `.mth`. `.thp` is
   parsed by the SDK THP library, which reads big-endian by design.

`ASSERT_SIZE`/`ASSERT_OFFSET` (190 sites) are kept enabled under `LINT` so a
struct whose wasm32 layout differs from the GameCube layout fails to compile.
The known divergence class is bitfields, handled in step 3; `long double`,
`double` alignment and enum size are checked by the same asserts.

### D3. Blocking waits: Asyncify now, JSPI later

The game blocks in `waitForDisc()` and in the pad-queue spin. Browsers cannot
block the main thread waiting for an async file read. Options:

- Asyncify (`-sASYNCIFY`): the blocking points call `emscripten_sleep(0)`,
  which unwinds to the event loop and resumes later. Works in every wasm
  browser. Costs code size and CPU time in every instrumented function (the
  Emscripten docs describe the overhead as significant; spike S2 measures it
  for this game); Melee's CPU budget is tiny relative to modern hardware, so
  this is expected to be acceptable. **Chosen for v1.**
- JSPI (`-sJSPI`): same source, no instrumentation cost, but needs browser
  support for WebAssembly JS Promise Integration. Added later as a second
  build flavour once support is confirmed in the target browsers; the hosted
  loader can pick it at runtime.
- Moving the game into a Web Worker with synchronous `FileReaderSync` reads.
  Rejected: SDL3's Emscripten backend, the Gamepad API and WebAudio node
  creation are main-thread facilities, so the worker design would replace
  Aurora's app layer.

`ASYNCIFY_ONLY`/`ASYNCIFY_REMOVE` lists are tuned once the call graphs are
known; Aurora's render path is excluded from instrumentation.

### D4. Main loop: frame function, not `while(1)`

`gm_801A4D34()` is patched under `TARGET_PC` so one iteration of its outer
loop is a function `port_frame()`. `port/src/main_loop.c` registers it with
`emscripten_set_main_loop_arg` at 60 Hz (or `requestAnimationFrame` with a
fixed-step accumulator so the game logic still runs at 60 Hz on 120 Hz
displays). Each frame: `aurora_update()` → push one synthetic pad-queue entry
(the retrace path's job on hardware) → run the loop body → `aurora_end_frame()`.
`VIWaitForRetrace` and `HSD_VIWaitXFBFlush` become yields.

### D5. Disc access: `DiscSource` in JS, `dvd_web` in C

- JS `DiscSource` wraps a `File`/`Blob` and exposes `read(offset, length) →
  Promise<ArrayBuffer>`. Hosted builds additionally keep a File System Access
  handle in IndexedDB so the ISO is remembered across visits (with the
  permission re-prompt the API requires). The single-file build uses
  `<input type="file">` only, which works on `file://`.
- `dvd_web` (C, replaces Aurora's nod-backed DVD) parses the disc header and
  FST once at boot (both big-endian, small, swapped by hand) and implements
  `DVDConvertPathToEntrynum`, `DVDFastOpen`, `DVDReadAsyncPrio`, `DVDClose`
  and the few status calls the game uses. Reads are issued to JS; completion
  fires the DVD callback from the main loop, which is exactly the callback
  discipline the game already expects from `devcom.c`.
- ARAM: Aurora's `AR.cpp` (memcpy-backed buffer). The `dst >= 0x80000000` test
  in `lbfile.c` becomes an `ARGetStorageAddress()` range test under
  `TARGET_PC`.
- A small LRU read cache in wasm memory (configurable, default 64 MB) avoids
  re-reading hot files such as `IfAll.dat` and the SFX banks.

### D6. Audio: AX high-level emulation feeding an AudioWorklet

The SDK's AX/AXFX C sources compile as-is; they build voice parameter blocks
and expect the DSP microcode to mix them. `port/src/ax_hle/` implements the
DSP side: on each AI DMA period (5 ms, 32 kHz) walk the active voice list,
decode ADPCM/PCM8/PCM16 from ARAM, resample, apply volume/envelope/mixer
settings into main and aux buses, invoke the aux (AXFX reverb) callbacks the
SDK already runs on the CPU, and emit stereo PCM. This is an original
implementation written from the AX headers vendored in this repo; Dolphin's
AX HLE is GPL and is used only as a behavioural reference, never copied.

`dsp.h`/`ai.h` entry points become HLE stubs that schedule the mixer. Output
is transferred to an `AudioWorkletNode` via `port.postMessage` (no
SharedArrayBuffer, so no cross-origin isolation headers are needed, which
keeps the single-file build working on `file://`). Target latency ≈ 60 ms.

### D7. Saves: CARD on a memory-card image persisted by the page

Aurora's CARD (kabufuda) works on a `.raw` memory card image in Emscripten's
MEMFS. `SaveStore` mirrors it to IndexedDB through IDBFS after each write
and offers explicit **Export .raw / .gci** and **Import** buttons, because
IndexedDB availability on `file://` is browser-dependent. The export button
is the guaranteed path; IndexedDB is the convenience path.

### D8. Single-file packaging

`port/tools/pack_single_html.py` produces `melee-offline.html` from the hosted
build:

- wasm embedded with `-sSINGLE_FILE` (base64 in the JS),
- the JS glue, AudioWorklet processor source (loaded via a Blob URL), CSS and
  page markup inlined,
- no external `fetch()` at runtime; the only I/O is the user's file input,
  IndexedDB, and the AudioWorklet.

The packer rejects any `<script src>`, `<link href>` or `fetch(` left in the
output so regressions are caught at build time. Expected size is in the same
range as Eaglercraft's offline file (tens of MB).

## 6. Components

Each item: purpose · interface · depends on · lives in.

| Component | Purpose | Interface | Depends on |
|---|---|---|---|
| `port/CMakeLists.txt` | Builds game + Aurora + port sources for `wasm32-emscripten`; presets for `web-debug`, `web-release`, `web-jspi` | CMake presets | emsdk, Aurora submodule |
| `port/src/main_loop.c` | Frame-function adapter for `gm_801A4D34`; pad-queue tick; Asyncify yield points | `port_frame()`, `port_yield()` | Aurora, game |
| `port/src/os_shim/` | OS pieces Aurora lacks: `OSAlarm` (driven from the frame tick), `OSDisableInterrupts` (no-op counter), `DC*/IC*` cache ops (no-op), `OSResetSystem`, `OSGetConsoleType`, error handler | Dolphin `os.h` | — |
| `port/src/dvd_web/` | DVD API over `DiscSource`; FST parser; read cache | Dolphin `dvd.h`; JS imports `disc_read(off,len,buf,cb)` | JS glue |
| `port/src/hsd_endian/` | Archive header/reloc swap; schema walker; format swappers for `.ssm/.sem/.hps/.mth` | `HSD_ArchiveParse` (replaces), `port_swap_file(kind, buf, len)` | generated schema tables |
| `port/schema/` + `port/tools/gen_schema.py` | Descriptor generation from headers + annotations | emits `schema_tables.c` | libclang |
| `port/src/ax_hle/` | DSP-side AX mixer; `dsp.h`/`ai.h` stubs; AudioSink bridge | Dolphin `ax.h`, `dsp.h`, `ai.h`; JS import `audio_push(f32*, n)` | AR (ARAM buffer) |
| `port/src/thp/` | Builds SDK `THPDec.c` C fallback; `lbmthp.c` re-enabled | Dolphin `thp.h` | dvd_web |
| Aurora patches | Emscripten guards, single-thread option, DVD removed, `GXInitFogAdjTable`, `VIPadFrameBufferWidth`, `PADSetSamplingRate`, `GXSetArray` little-endian shim | Aurora build options | — |
| `port/web/js/` | `DiscSource`, `SaveStore`, `AudioSink`, `imports.js`, boot sequencing, error panel | ES modules; Emscripten `--pre-js/--post-js` | browser APIs |
| `port/web/shell/` | `index.html` (hosted) and `offline.template.html` | HTML/CSS | JS glue |
| `port/tools/pack_single_html.py` | Inline everything into one file; verify no external refs | CLI | hosted build output |
| `port/tools/setup.sh` | Fetch emsdk, init Aurora submodule, check tools | CLI | git, python |

## 7. Data flow: boot

1. Page loads; JS checks `navigator.gpu`, shows the disc picker.
2. User selects the ISO. `DiscSource` validates the game ID at offset 0 is
   `GALE01` and the disc header/FST parse. Wrong disc → clear error, stop.
3. JS requests a WebGPU adapter/device (or lets Aurora do it under Asyncify),
   creates the AudioContext on the user's click (autoplay policy), mounts
   IDBFS and restores `memcard.raw` if present.
4. wasm `main()` (game's `main` renamed `melee_main`; `port_main` wraps it)
   runs the game's init chain; `DVDInit` initialises `dvd_web` with the FST.
5. `emscripten_set_main_loop` starts `port_frame`. First loads (`IfAll.usd`
   etc.) go through `dvd_web` → `DiscSource`; the game's `waitForDisc` yields
   until the callback fires; `HSD_ArchiveParse` swaps the archive.
6. Title screen renders through Aurora → WebGPU; `ax_hle` starts pushing audio.

## 8. Error handling

- No WebGPU: friendly page-level message with browser guidance. Never a
  blank canvas.
- Wrong or corrupt disc: validated before wasm starts; message names the
  expected game ID.
- Unknown archive root symbol: debug builds abort with the symbol and file
  name; release builds log and continue (data will look wrong, but the log
  identifies the gap).
- Read failure mid-game (file handle revoked, disc removed from memory): the
  DVD callback reports an error status; the game's own DVD error path runs;
  the page shows a re-pick prompt.
- Save persistence failure: surface a banner offering export; never lose the
  in-memory card silently.
- Every port-side `OSReport`/`OSPanic` goes to the browser console with a
  `[melee]` prefix; `HSD_ASSERT` failures in debug builds trap into DevTools.

## 9. Security and legal posture

- No game data in the repository or build artefacts. The single-file HTML
  contains only code built from this repo, Aurora, SDL3 and Emscripten
  runtime, all under permissive licences. A `THIRD_PARTY_NOTICES` file is
  generated into both builds.
- The page never uploads the disc or saves anywhere; `DiscSource` reads are
  local. The hosted build can be served with no backend.

## 10. Testing

- **Compile gate**: the existing CI `native-build` job is extended to the
  wasm build so every upstream commit is checked against the port
  (`cmake --preset web-release`).
- **Layout gate**: `ASSERT_SIZE`/`ASSERT_OFFSET` compiled in for wasm32.
- **Schema unit tests** (`port/tests/schema/`): run the swapper natively
  (x86-64 host, big-endian test fixtures generated by the test) on
  hand-built archives and on fixtures the user supplies locally from their
  disc (never committed). Round-trip property: swapping twice is identity.
- **ax_hle unit tests**: known ADPCM vectors, resampler ratios, mixer gains.
- **Browser smoke tests** (Playwright, hosted build, headless Chrome with
  WebGPU): boot to title screen with a user-provided ISO path from an env
  var; screenshot diff against a stored reference; verifies audio worklet
  received frames. Skipped when no ISO is configured.
- **Manual milestone checklist** in `port/docs/milestones.md`: title, main
  menu, CSS, a VS match on Final Destination with two humans, training mode,
  save round-trip, single-file build opened from `file://` on Chrome and
  Firefox.

## 11. Risks and Phase-0 spikes

Each spike is a throwaway with a yes/no outcome, done before Milestone 1.

| # | Risk | Spike |
|---|---|---|
| S1 | Aurora does not build or run under Emscripten/emdawnwebgpu | **Done, PASS** (2026-09-11): blue screen at display rate in Chrome 151 on a real GPU; see `port/docs/spikes.md` |
| S2 | Asyncify size/CPU cost is unacceptable on the full game | Link the full game with Asyncify; measure wasm size and a synthetic frame loop |
| S3 | The single-file HTML cannot use IndexedDB or Blob-URL worklets on `file://` in Firefox/Safari | 20-line test page; decide which persistence paths are "guaranteed" vs "best effort" |
| S4 | Bitfield repacking has an unhandled case (fields spanning storage units) | Enumerate all 904 bitfields via libclang; assert none crosses its storage unit under either ABI |
| S5 | Game data has pointer-typed fields not listed in the relocation table (would break D2 step 1) | Cross-check HSDRaw's pointer fields against relocation tables of a sample of files |
| S6 | Memory: 24 MB MEM1 + 16 MB ARAM + Aurora + read cache exceeds a comfortable wasm heap on mobile | Measure peak `HEAP` with `ALLOW_MEMORY_GROWTH`; set `MAXIMUM_MEMORY` accordingly |

Other known unknowns: exact browser support for WebGPU on Linux Firefox at
release time; JSPI availability (affects only the optional flavour).

## 12. Out of scope (v1)

Netplay, Slippi compatibility, texture packs, widescreen/HD UI fixes beyond
what Aurora gives for free, compressed disc formats, PAL/JP versions, mobile
touch controls, a native desktop build, save-state support.

## 13. Milestones

1. **M0 Spikes** (S1–S6) and toolchain setup.
2. **M1 Link**: full game + Aurora + port stubs link to a wasm; boots to the
   first `OSReport` in the console.
3. **M2 Title**: `IfAll`, boot archives swapped; title screen renders.
4. **M3 Menus + CSS**: menu archives, SIS text, gamepad input, `.sem/.ssm`
   loading, first sound.
5. **M4 Match**: stage + fighter + item + effect archives; a VS match plays
   with audio and music.
6. **M5 Saves + polish**: CARD persistence, export/import, error panel,
   AudioWorklet latency tuning, Asyncify list tuning.
7. **M6 Single-file**: packer, `file://` validation on three browsers, size
   budget.
8. **M7 Coverage**: every character, stage, mode; THP movies; trophies; the
   milestone checklist passes.

The implementation plan (`docs/superpowers/plans/2026-09-10-web-port-plan.md`)
breaks M0–M3 into tasks and lists M4–M7 at milestone granularity.
