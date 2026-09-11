# Melee web port

Builds the decompiled game into WebAssembly. Two outputs:

- **Hosted**: a static directory served by any web server.
- **Single-file**: one `melee-offline.html` that runs from `file://`.

Both need the user's own GALE01 (NTSC-U 1.02) disc image; nothing from the
game ships with the build.

Design: [`docs/superpowers/specs/2026-09-10-web-port-design.md`](../docs/superpowers/specs/2026-09-10-web-port-design.md)
Plan: [`docs/superpowers/plans/2026-09-10-web-port-plan.md`](../docs/superpowers/plans/2026-09-10-web-port-plan.md)

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
| [emdawnwebgpu](https://dawn.googlesource.com/dawn/+/refs/heads/main/src/emdawnwebgpu/pkg/README.md) | Dawn's `webgpu.h` implemented on the browser's WebGPU; Emscripten port `--use-port=emdawnwebgpu` | bundled with Emscripten |
| [SDL3](https://github.com/libsdl-org/SDL) | Window, input and gamepads via Aurora's app layer; Emscripten backend | fetched by Aurora's CMake |
| Dolphin SDK sources (`extern/dolphin`) | AX/AXFX/THP C code compiled as-is; headers for everything Aurora does not vendor | this repo |
| CMake ≥ 3.25, Ninja, Python 3 (+ `libclang`, `pyyaml`) | Build, schema generator, packer | system |
| [Playwright](https://playwright.dev) | Browser smoke tests | `tests/browser` |

Port-only code (`port/src`) supplies what none of the above provide: the main loop adapter, OS shims, a DVD layer over the user's disc image, load-time big-endian → little-endian conversion of HSD archives, and an AX audio mixer feeding an AudioWorklet.

## Layout

| Path | What |
|---|---|
| `src/` | Port-only C: main loop adapter, OS shims, DVD layer, archive endian conversion, AX mixer |
| `web/` | JS glue and HTML shells |
| `schema/` | Annotations and root-symbol table for the endian converter |
| `tools/` | Setup, schema generator, single-file packer |
| `tests/` | Host unit tests (`check.h` harness) and Playwright browser tests |
| `extern/` | emsdk and Aurora (ignored); `aurora-patches/` is tracked |
| `docs/` | Spike results, milestone checklist |
