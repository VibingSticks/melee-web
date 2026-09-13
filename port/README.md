# Melee web port

Builds the decompiled game into WebAssembly. Two outputs:

- **Hosted**: a static directory served by any web server.
- **Single-file**: one `melee-offline.html` that runs from `file://`.

Both need the user's own GALE01 (NTSC-U 1.02) disc image; nothing from the
game ships with the build.

Design: [`docs/superpowers/specs/2026-09-10-web-port-design.md`](../docs/superpowers/specs/2026-09-10-web-port-design.md)
Plan: [`docs/superpowers/plans/2026-09-10-web-port-plan.md`](../docs/superpowers/plans/2026-09-10-web-port-plan.md)

## What runs today

Chrome 151 on WebGPU and Firefox on the WebGL2 fallback, against a real disc:
boot → the memory-card notice → title → main menu → VS Mode → character select
with every portrait → a VS match on Final Destination with both fighters, the
HUD and particle effects. `Module._port_debug_start_vs()` jumps straight to a
match without the menus. The packed offline file does all of this from
`file://`. Host tests: `ctest --preset host-tests`.

Not working yet, roughly in the order worth fixing:

| | |
|---|---|
| A translucent overlay washes over the 3D scene | cause not yet found |
| Audio is plain | `src/ax_hle/ax_hle.c` mixes the game's voices (music stream and SFX) but skips the aux busses (reverb/chorus) and ITD, and resamples linearly; `-DPORT_AUDIO=OFF` swaps in the silent stubs |
| Only controller port 1 is usable | the player-type toggle does not respond |
| The intro movie is skipped | THP decoder not ported (plan Task 22) |

`docs/milestones.md` tracks this in more detail.

## Setup

```sh
port/tools/setup.sh          # installs emsdk under port/extern, clones Aurora at the pinned rev
source port/tools/env.sh     # puts emcc on PATH
```

## Build

```sh
cd port
cmake --preset web-debug && cmake --build --preset web-debug
python3 -m http.server -d build/web-debug 8080
```

## Offline file

One HTML file that runs from `file://` with nothing beside it. The wasm, the
JavaScript and naga's translator are all inlined; the player still picks their
own disc image at runtime, and it is never part of the file.

```sh
cd port
cmake --preset web-single && cmake --build --preset web-single
python3 tools/pack_single_html.py build/web-single -o melee-offline.html
```

`web-single` is the release preset plus `-sSINGLE_FILE` (the wasm becomes a
base64 data URL inside `melee.js`). The packer refuses to write a page that
still reaches for a second file -- a `<script src>`, a `<link href>`, a
non-`data:` `fetch` -- so the check travels with the build rather than living
in someone's head. `tests/pack_test.py` covers it against a fixture.

To drive the packed file the same way as the hosted page, hand the probe the
HTML instead of a build directory:

```sh
node tests/browser/boot_probe.mjs melee-offline.html /path/to/GALE01.iso 20 --headed
```

## Boot smoke test without a real disc

```sh
python3 port/tools/make_test_disc.py port/build/test-disc.iso audio/main.ssm=port/build/fake-main.ssm
node port/tests/browser/boot_probe.mjs port/build/web-debug port/build/test-disc.iso 8 --headed
```

`boot_probe.mjs` drives the browser through Playwright, feeds the disc to the page, and prints the console. Use `--headed`: headless Chrome only offers a software WebGPU adapter that Chrome drops after a few frames. `--query=renderer=webgl2` boots under the WebGL2 polyfill, `--query=gpu=compat` forces Aurora's compatibility profile on real WebGPU, and `--browser=firefox` runs Playwright's Firefox, which has no WebGPU on Linux and so exercises the automatic fallback.

## WebGL2 fallback tests

```sh
node port/tests/browser/gl2_polyfill_test.mjs                    # headless Chromium on SwiftShader
node port/tests/browser/gl2_polyfill_test.mjs --browser=firefox  # real GPU, no WebGPU present
```

## Host unit tests

```sh
cd port
cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests
```

## What this project uses

| Component | Role | Version / source |
|---|---|---|
| [doldecomp/melee](https://github.com/doldecomp/melee) | The game itself, 100% decompiled C (`src/melee`, `src/sysdolphin`) | this repo |
| [Aurora](https://github.com/encounter/aurora) | GameCube compatibility layer: GX on WebGPU, PAD, VI, CARD, ARAM, mtx, part of OS | r-burns fork, rev `e6a6f02` (same rev as `.nix/overlay.nix`), MIT |
| [Emscripten](https://emscripten.org) | C/C++ → WebAssembly toolchain, `emscripten_set_main_loop`, Asyncify, IDBFS, `-sSINGLE_FILE` | emsdk `latest` (6.0.9 at setup time), installed by `tools/setup.sh` |
| [emdawnwebgpu](https://dawn.googlesource.com/dawn/+/refs/heads/main/src/emdawnwebgpu/pkg/README.md) | Dawn's `webgpu.h` implemented on the browser's WebGPU; needs `SetImmediates`, so a newer port than Emscripten's bundled one | remote port v20260910.214722, tracked as `extern/emdawnwebgpu-*.remoteport.py` |
| [SDL3](https://github.com/libsdl-org/SDL) | Window, input and gamepads via Aurora's app layer; Emscripten backend | Emscripten's `sdl3` port (3.4.2) |
| Dolphin SDK sources (`extern/dolphin`) | AX/AXFX/THP C code compiled as-is; headers for everything Aurora does not vendor | this repo |
| CMake ≥ 3.25, Ninja, Python 3 (+ `libclang`, `pyyaml`) | Build, schema generator, packer | system |
| [Playwright](https://playwright.dev) | Browser smoke tests | `tests/browser` |
| [naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga) (Rust, via rustup) | WGSL → GLSL ES 3.00 in the browser for the WebGL2 fallback | `tools/naga-wasm`, built by `tools/build_naga.sh` |
| `web/js/gpu-gl2.js` (port-only) | The WebGPU subset emdawnwebgpu needs, on WebGL2: used automatically when the browser has no WebGPU adapter, or with `?renderer=webgl2` | this repo; tests in `tests/browser/gl2_polyfill_test.mjs` |

Port-only code (`port/src`) supplies what none of the above provide: the main
loop adapter, OS shims, a DVD layer over the user's disc image, load-time
big-endian → little-endian conversion of HSD archives, and an AX voice mixer
(`src/ax_hle`, off by default -- see below).

Two font atlases are read out of the boot DOL on the player's disc at startup
(`src/font_dol.c`). sysdolphin keeps them as bitmaps in the executable rather
than in a DAT file, so the decompilation has nothing to compile them from and
the units that draw text were excluded from the build until this loaded them.
As with everything else, the data stays on the player's disc.

## Layout

| Path | What |
|---|---|
| `src/` | Port-only C: main loop adapter, OS shims, DVD layer, archive endian conversion, font atlases from the disc's DOL, AX mixer. `src/compat/` holds headers that shadow libc for the game library only (`bool` as a 4-byte int, MSL `printf.h`) |
| `web/` | JS glue and HTML shells |
| `schema/` | Annotations and root-symbol table for the endian converter |
| `tools/` | Setup, schema generator, single-file packer |
| `tests/` | Host unit tests (`check.h` harness) and Playwright browser tests |
| `extern/` | emsdk and Aurora (ignored); `aurora-patches/` is tracked |
| `docs/` | Spike results, milestone checklist |
