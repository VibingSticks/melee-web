# Melee Web Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Super Smash Bros. Melee from this decompilation into a WebAssembly game that runs in the browser as both a hosted static site and a single self-contained HTML file.

**Architecture:** The unmodified game code (`src/melee`, `src/sysdolphin`) is compiled with Emscripten against Aurora, which provides GX-on-WebGPU, PAD, VI, CARD and ARAM. Port-only code under `port/src` supplies what Aurora lacks (OS shims, a DVD layer over a JS file reader, a load-time big-endian→little-endian archive converter, and an AX audio mixer) and adapts the interrupt-paced main loop to `emscripten_set_main_loop` with Asyncify at the game's blocking points.

**Tech Stack:** C99/C++20, CMake ≥ 3.25 + Ninja, Emscripten (emsdk, ≥ 4.0.10), `emdawnwebgpu` port, SDL3, Aurora (r-burns fork pinned at `e6a6f02ace4146e8a2f648d5c274dbb7dd89665c`), Python 3 + libclang for schema generation, Playwright for browser smoke tests.

**Spec:** `docs/superpowers/specs/2026-09-10-web-port-design.md`

## Global Constraints

- Target: `wasm32-unknown-emscripten`, single-threaded, no SharedArrayBuffer, no cross-origin-isolation headers required.
- Game sources are never edited except inside `#ifdef TARGET_PC` blocks.
- Build defines for game code: `TARGET_PC`, `LINT` (keeps `ASSERT_SIZE`/`ASSERT_OFFSET` on), `bool=int`; never `MUST_MATCH`.
- Aurora is vendored at the pinned rev under `port/extern/aurora`; changes go in `port/extern/aurora-patches/NNNN-*.patch`, applied by `port/tools/setup.sh`.
- No game data is ever committed. Test fixtures that need real disc data are read from `$MELEE_ISO` at test time and skipped when unset.
- The single-file build must contain no `<script src>`, `<link href>`, or runtime `fetch(`.
- All port C code is C99 with `<stdint.h>` types so it also compiles for host unit tests with clang.
- Host unit tests use the header-only harness in `port/tests/check.h` and run via `ctest`.
- Dolphin's AX HLE is a behavioural reference only; no code is copied from GPL sources.

---

## File structure

| Path | Responsibility |
|---|---|
| `port/CMakeLists.txt` | Top-level: game static lib, Aurora, port sources, wasm executable, options |
| `port/CMakePresets.json` | `web-debug`, `web-release`, `host-tests` presets |
| `port/cmake/game_sources.cmake` | Source globbing and the unit exclusion list for the game library |
| `port/cmake/emscripten_link.cmake` | emcc link flags (Asyncify lists, emdawnwebgpu, SINGLE_FILE toggle) |
| `port/tools/setup.sh` | Installs emsdk, clones/patches Aurora, verifies tools |
| `port/tools/gen_schema.py` | libclang → `schema_tables.c` generator |
| `port/tools/check_bitfields.py` | Spike S4: enumerate bitfields, flag storage-unit crossings |
| `port/tools/pack_single_html.py` | Inlines hosted build into `melee-offline.html`, verifies no external refs |
| `port/schema/annotations.yml` | Hand-maintained array-length/union/opaque annotations for the generator |
| `port/schema/roots.yml` | Archive public-symbol pattern → root type |
| `port/src/port.h` | Shared port declarations (yield, log, tick) |
| `port/src/main_loop.c` | `port_main`, `port_frame`, pad-queue tick, Asyncify yields |
| `port/src/os_shim/os_shim.c` | OSAlarm, interrupt counter, cache no-ops, reset, console type |
| `port/src/dvd_web/fst.{c,h}` | Disc header + FST parser, path→entry lookup |
| `port/src/dvd_web/dvd_web.{c,h}` | DVD API over a JS byte source; completion queue; read cache |
| `port/src/hsd_endian/archive_swap.{c,h}` | Header/reloc/symbol-table swap; replacement `HSD_ArchiveParse` |
| `port/src/hsd_endian/schema.h` | Descriptor data model |
| `port/src/hsd_endian/walker.{c,h}` | Graph walker that swaps scalars per descriptor |
| `port/src/hsd_endian/formats.c` | `.ssm/.sem/.hps/.mth` swappers |
| `port/src/hsd_endian/schema_tables.c` | **Generated** — do not edit |
| `port/src/ax_hle/*.c` | AX DSP-side mixer, `dsp.h`/`ai.h` stubs, audio bridge |
| `port/src/patches/*.c` | Nothing — game patches go inline under `TARGET_PC` in `src/` |
| `port/web/js/imports.js` | Functions the wasm imports (`port_disc_read`, `port_audio_push`, `port_log`) |
| `port/web/js/disc_source.js` | `DiscSource` over File/Blob, game-ID validation |
| `port/web/js/save_store.js` | IDBFS mirror + export/import |
| `port/web/js/audio_sink.js` + `audio_worklet.js` | AudioWorklet ring buffer |
| `port/web/js/boot.js` | Page flow: WebGPU check → pick disc → start module |
| `port/web/shell/index.html`, `offline.template.html` | Hosted page and single-file template |
| `port/tests/check.h` | Minimal assert harness |
| `port/tests/*.c` | Host unit tests |
| `port/tests/browser/*.spec.ts` | Playwright smoke tests |
| `port/docs/spikes.md`, `port/docs/milestones.md` | Spike outcomes; manual checklist |

Game-source edits (all under `#ifdef TARGET_PC`): `src/melee/gm/gmscene.c` (loop body extraction), `src/melee/gm/gmmain.c` (`main` → `melee_main`), `src/melee/lb/lbfile.c` (ARAM range test, yield in `waitForDisc`), `src/melee/lb/lbmemory.c` (arena checks), `src/sysdolphin/baselib/archive.c` (`HSD_ArchiveParse` delegated to port).

---

## Milestone M0 — Toolchain and spikes

### Task 1: Project skeleton and setup script

**Files:**
- Create: `port/README.md`, `port/.gitignore`, `port/tools/setup.sh`, `port/docs/spikes.md`
- Create: `port/extern/aurora-patches/.gitkeep`

**Interfaces:**
- Produces: `port/extern/emsdk/` (ignored), `port/extern/aurora/` (ignored, cloned at pinned rev), `port/tools/env.sh` (sourced for `emcc` on PATH).

- [ ] **Step 1: Write the setup script**

```bash
#!/usr/bin/env bash
# port/tools/setup.sh — one-time toolchain setup for the web port.
set -euo pipefail
PORT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
EXTERN="$PORT_DIR/extern"
EMSDK_VERSION="${EMSDK_VERSION:-latest}"
AURORA_REPO="https://github.com/r-burns/aurora.git"
AURORA_REV="e6a6f02ace4146e8a2f648d5c274dbb7dd89665c"
mkdir -p "$EXTERN"

for tool in git cmake ninja python3; do
  command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 1; }
done

if [ ! -d "$EXTERN/emsdk" ]; then
  git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EXTERN/emsdk"
fi
"$EXTERN/emsdk/emsdk" install "$EMSDK_VERSION"
"$EXTERN/emsdk/emsdk" activate "$EMSDK_VERSION"

if [ ! -d "$EXTERN/aurora/.git" ]; then
  git clone "$AURORA_REPO" "$EXTERN/aurora"
fi
git -C "$EXTERN/aurora" fetch -q origin
git -C "$EXTERN/aurora" checkout -q "$AURORA_REV"
git -C "$EXTERN/aurora" reset -q --hard
for p in "$PORT_DIR"/extern/aurora-patches/*.patch; do
  [ -e "$p" ] || continue
  git -C "$EXTERN/aurora" apply "$p"
done

cat > "$PORT_DIR/tools/env.sh" <<EOF
# source this file
export EMSDK_QUIET=1
source "$EXTERN/emsdk/emsdk_env.sh"
EOF
echo "OK. Run: source $PORT_DIR/tools/env.sh"
```

- [ ] **Step 2: Write `.gitignore` and README**

`port/.gitignore`:
```
extern/emsdk/
extern/aurora/
build/
tools/env.sh
*.iso
*.gcm
```

`port/README.md` contains the three commands: `tools/setup.sh`, `source tools/env.sh`, `cmake --preset web-debug && cmake --build --preset web-debug`, and a link to the spec.

- [ ] **Step 3: Run the script and verify**

Run: `bash port/tools/setup.sh && source port/tools/env.sh && emcc --version | head -1 && git -C port/extern/aurora rev-parse HEAD`
Expected: an `emcc` version line of 4.0.10 or newer (pin it in `setup.sh` once S1 passes) and `e6a6f02ace4146e8a2f648d5c274dbb7dd89665c`.

- [ ] **Step 4: Commit**

```bash
git add port/README.md port/.gitignore port/tools/setup.sh port/docs/spikes.md port/extern/aurora-patches/.gitkeep
git commit -m "port: add web port skeleton and toolchain setup script"
```

### Task 2: Host test harness

**Files:**
- Create: `port/tests/check.h`, `port/tests/CMakeLists.txt`, `port/tests/smoke_test.c`, `port/CMakePresets.json`, `port/CMakeLists.txt` (initial)

**Interfaces:**
- Produces: `CHECK(cond)`, `CHECK_EQ_U32(a,b)`, `CHECK_MEM_EQ(a,b,n)`, `TEST_MAIN(fn)` macros; CMake function `port_add_test(name src...)`.

- [ ] **Step 1: Write the harness**

```c
/* port/tests/check.h */
#ifndef PORT_CHECK_H
#define PORT_CHECK_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static int check_failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); check_failures++; } } while (0)
#define CHECK_EQ_U32(a, b) do { uint32_t _a = (uint32_t)(a), _b = (uint32_t)(b); if (_a != _b) { \
    fprintf(stderr, "%s:%d: %s == 0x%08x, expected 0x%08x\n", __FILE__, __LINE__, #a, _a, _b); check_failures++; } } while (0)
#define CHECK_MEM_EQ(a, b, n) do { if (memcmp((a), (b), (n)) != 0) { \
    fprintf(stderr, "%s:%d: memory differs: %s vs %s\n", __FILE__, __LINE__, #a, #b); check_failures++; } } while (0)
#define TEST_MAIN(fn) int main(void) { fn(); if (check_failures) { \
    fprintf(stderr, "%d failure(s)\n", check_failures); return 1; } puts("ok"); return 0; }
#endif
```

- [ ] **Step 2: Write the smoke test**

```c
/* port/tests/smoke_test.c */
#include "check.h"
static void run(void) { CHECK(1 + 1 == 2); CHECK_EQ_U32(0x12345678u, 0x12345678u); }
TEST_MAIN(run)
```

- [ ] **Step 3: Write the CMake files**

`port/tests/CMakeLists.txt`:
```cmake
function(port_add_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PORT_SRC_DIR})
  target_compile_options(${name} PRIVATE -Wall -Wextra -std=c99)
  add_test(NAME ${name} COMMAND ${name})
endfunction()
port_add_test(smoke_test smoke_test.c)
```

`port/CMakeLists.txt` (initial; Task 5 extends it):
```cmake
cmake_minimum_required(VERSION 3.25)
project(melee-web LANGUAGES C CXX)
set(PORT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
option(PORT_BUILD_TESTS "Build host unit tests" OFF)
if (PORT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif ()
if (EMSCRIPTEN)
  include(cmake/game_sources.cmake)
endif ()
```

`port/CMakePresets.json`:
```json
{
  "version": 6,
  "configurePresets": [
    { "name": "host-tests", "binaryDir": "${sourceDir}/build/host-tests", "generator": "Ninja",
      "cacheVariables": { "PORT_BUILD_TESTS": "ON", "CMAKE_BUILD_TYPE": "Debug" } },
    { "name": "web-debug", "binaryDir": "${sourceDir}/build/web-debug", "generator": "Ninja",
      "toolchainFile": "$env{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
    { "name": "web-release", "inherits": "web-debug", "binaryDir": "${sourceDir}/build/web-release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } }
  ],
  "buildPresets": [
    { "name": "host-tests", "configurePreset": "host-tests" },
    { "name": "web-debug", "configurePreset": "web-debug" },
    { "name": "web-release", "configurePreset": "web-release" }
  ],
  "testPresets": [ { "name": "host-tests", "configurePreset": "host-tests", "output": { "outputOnFailure": true } } ]
}
```

- [ ] **Step 4: Run**

Run: `cd port && cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests`
Expected: `1/1 Test #1: smoke_test ... Passed`

- [ ] **Step 5: Commit**

```bash
git add port/tests/check.h port/tests/CMakeLists.txt port/tests/smoke_test.c port/CMakeLists.txt port/CMakePresets.json
git commit -m "port: add host unit test harness and CMake presets"
```

### Task 3: Spike S1 — Aurora `simple` example under Emscripten

**Files:**
- Create: `port/spikes/aurora-web/CMakeLists.txt`, `port/spikes/aurora-web/shell.html`
- Create (if needed): `port/extern/aurora-patches/0001-emscripten-build.patch`
- Modify: `port/docs/spikes.md`

**Interfaces:**
- Produces: the first Aurora patch (Emscripten guards) that Task 9 builds on; a recorded yes/no.

- [ ] **Step 1: Write the spike CMake**

```cmake
cmake_minimum_required(VERSION 3.25)
project(aurora-web-spike LANGUAGES C CXX)
set(AURORA_ENABLE_DVD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_CARD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_THP OFF CACHE BOOL "" FORCE)
set(AURORA_DAWN_PROVIDER "none" CACHE STRING "" FORCE)   # added by patch: skip Dawn on Emscripten
add_subdirectory(../../extern/aurora aurora EXCLUDE_FROM_ALL)
add_executable(simple ../../extern/aurora/examples/simple.c)
target_link_libraries(simple PRIVATE aurora::core aurora::gx aurora::main aurora::vi)
target_link_options(simple PRIVATE --use-port=emdawnwebgpu -sASYNCIFY -sALLOW_MEMORY_GROWTH
  --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
set_target_properties(simple PROPERTIES SUFFIX ".html")
```

- [ ] **Step 2: Build, iterate on the patch until it links**

Run: `source port/tools/env.sh && cd port/spikes/aurora-web && emcmake cmake -B build -G Ninja && cmake --build build`

Expected failures to fix inside the patch, in order: `dawn/native/DawnNative.h` include (guard with `#ifndef __EMSCRIPTEN__`), `DawnTogglesDescriptor`/`DawnCacheDeviceDescriptor` (same guard), `std::jthread` use in `lib/thread.hpp` (guard, provide inline-run fallback), sqlite pipeline cache (`AURORA_CACHE_USE_SQLITE` off), `std::thread` in `pipeline_cache.cpp` (synchronous path when `AURORA_SINGLE_THREADED`). Keep each fix minimal; record each in the patch.

- [ ] **Step 3: Run in Chrome**

Run: `python3 -m http.server -d build 8080` then open `http://localhost:8080/simple.html`.
Expected: a solid blue canvas (the example clears to `{0,0,100,255}`) and no console errors.

- [ ] **Step 4: Record the outcome**

Append to `port/docs/spikes.md`: `S1: PASS/FAIL, date, wasm size, list of patch hunks, any Aurora feature that had to be disabled`.

- [ ] **Step 5: Commit**

```bash
git add port/spikes/aurora-web port/extern/aurora-patches/0001-emscripten-build.patch port/docs/spikes.md
git commit -m "port: spike S1 — Aurora simple example runs under Emscripten"
```

### Task 4: Spikes S3 and S4 — `file://` capabilities and bitfield audit

**Files:**
- Create: `port/spikes/file-url/index.html`, `port/tools/check_bitfields.py`
- Modify: `port/docs/spikes.md`

- [ ] **Step 1: Write the `file://` capability page**

```html
<!doctype html><meta charset="utf-8"><title>file:// caps</title><pre id="o"></pre>
<script>
const o = document.getElementById('o'); const log = s => o.textContent += s + '\n';
log('webgpu: ' + !!navigator.gpu);
log('secure: ' + window.isSecureContext);
try { indexedDB.open('t').onsuccess = () => log('indexedDB: ok'); } catch (e) { log('indexedDB: ' + e.name); }
try { localStorage.setItem('t','1'); log('localStorage: ok'); } catch (e) { log('localStorage: ' + e.name); }
try { const u = URL.createObjectURL(new Blob(['postMessage(1)'])); new Worker(u).onmessage = () => log('blobWorker: ok'); } catch (e) { log('blobWorker: ' + e.name); }
(async () => { try { const c = new AudioContext(); await c.audioWorklet.addModule(URL.createObjectURL(new Blob(['registerProcessor("p",class extends AudioWorkletProcessor{process(){return true}})'],{type:'text/javascript'}))); log('audioWorklet: ok'); } catch (e) { log('audioWorklet: ' + e.name); } })();
</script>
```

- [ ] **Step 2: Open it from disk in Chrome, Firefox and (if available) Safari; paste each output into `spikes.md` under S3**

Decision rule (write it next to the results): IndexedDB "ok" everywhere → IDBFS is the primary save path; otherwise export/import is primary and IDBFS best-effort. AudioWorklet "ok" everywhere is required; if not, note ScriptProcessorNode as the `file://` fallback.

- [ ] **Step 3: Write the bitfield audit**

```python
#!/usr/bin/env python3
"""Spike S4: list every bitfield in game headers and flag any that would
cross its storage unit boundary under MSB-first (PPC) or LSB-first (wasm32)."""
import sys, json, subprocess, pathlib
from clang import cindex
root = pathlib.Path(__file__).resolve().parents[2]
cc = json.load(open(root / "build/GALE01/compile_commands.json"))
args = ["-xc", "-std=c99", "-DTARGET_PC", "-DLINT", f"-I{root}/src", f"-isystem{root}/port/extern/aurora/include", "-fparse-all-comments"]
idx = cindex.Index.create()
seen, crossings, total = set(), [], 0
for unit in cc:
    tu = idx.parse(unit["file"], args=args)
    for c in tu.cursor.walk_preorder():
        if c.kind in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.UNION_DECL) and c.is_definition():
            key = (c.location.file.name if c.location.file else "", c.spelling, c.location.line)
            if key in seen: continue
            seen.add(key)
            bitpos = 0
            for f in c.get_children():
                if f.kind != cindex.CursorKind.FIELD_DECL: continue
                if not f.is_bitfield(): bitpos = f.get_field_offsetof() + f.type.get_size()*8; continue
                total += 1
                w, unit_bits = f.get_bitfield_width(), f.type.get_size()*8
                off = f.get_field_offsetof()
                if (off % unit_bits) + w > unit_bits:
                    crossings.append((key, f.spelling, off, w, unit_bits))
print(f"bitfields: {total}; storage-unit crossings: {len(crossings)}")
for k in crossings: print(k)
sys.exit(1 if crossings else 0)
```

- [ ] **Step 4: Run it**

Run: `python3 ./configure.py --no-progress && python3 port/tools/check_bitfields.py`
Expected: `storage-unit crossings: 0`. If non-zero, list each in `spikes.md`; those structs get a hand-written `opaque` + custom swapper entry in Task 14's annotations.

- [ ] **Step 5: Commit**

```bash
git add port/spikes/file-url/index.html port/tools/check_bitfields.py port/docs/spikes.md
git commit -m "port: spikes S3 (file:// capabilities) and S4 (bitfield audit)"
```

---

## Milestone M1 — Link and boot to the first `OSReport`

### Task 5: Game static library for wasm

**Files:**
- Create: `port/cmake/game_sources.cmake`
- Modify: `port/CMakeLists.txt`

**Interfaces:**
- Produces: CMake target `melee_game` (STATIC) with include dirs `${GAME_ROOT}/src`, Aurora `include`; defines `TARGET_PC LINT bool=int VERSION_GALE01 BUILD_VERSION=0`.

- [ ] **Step 1: Write `game_sources.cmake`**

```cmake
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
  ${GAME_ROOT}/src/melee/*.c ${GAME_ROOT}/src/sysdolphin/*.c)
# Units that need work before they build; each is re-enabled by a later task.
set(GAME_EXCLUDE
  dberror.c debug.c debugconsole_main.c   # PPC register dumps
  hsd_3915.c sislib_font.c                # debug/SIS font atlas blobs (Task 17)
  lb_01F8.c lbmthp.c                      # THP (M7)
  fog.c pobj.c video.c lb_0195.c gmmain.c lbcardnew.c  # API gaps closed in Task 9
)
foreach (x IN LISTS GAME_EXCLUDE)
  list(FILTER GAME_SOURCES EXCLUDE REGEX "/${x}$")
endforeach ()
add_library(melee_game STATIC ${GAME_SOURCES})
target_include_directories(melee_game PUBLIC ${GAME_ROOT}/src ${CMAKE_CURRENT_SOURCE_DIR}/extern/aurora/include)
target_compile_definitions(melee_game PUBLIC TARGET_PC LINT bool=int VERSION_GALE01 BUILD_VERSION=0)
target_compile_options(melee_game PRIVATE -std=c99 -fno-strict-aliasing -Wno-everything)
```

- [ ] **Step 2: Configure and build the library only**

Run: `source port/tools/env.sh && cd port && cmake --preset web-debug && cmake --build --preset web-debug --target melee_game 2>&1 | tail -20`
Expected: `melee_game` builds. Any unit that fails is added to `GAME_EXCLUDE` **with a comment naming the missing symbol**, and a line is added to `port/docs/milestones.md` under "Excluded units".

- [ ] **Step 3: Commit**

```bash
git add port/cmake/game_sources.cmake port/CMakeLists.txt port/docs/milestones.md
git commit -m "port: compile game sources as a wasm static library"
```

### Task 6: OS shim

**Files:**
- Create: `port/src/port.h`, `port/src/os_shim/os_shim.c`, `port/src/os_shim/os_alarm.c`, `port/src/os_shim/os_alarm.h`
- Test: `port/tests/os_alarm_test.c`

**Interfaces:**
- Consumes: Aurora's `dolphin/os.h` types (`OSAlarm`, `OSTime`, `OSTick`).
- Produces: `void port_alarm_tick(OSTime now)` (called once per frame by Task 10), plus SDK-named `OSInitAlarm`, `OSSetAlarm`, `OSSetPeriodicAlarm`, `OSCancelAlarm`, `OSDisableInterrupts`, `OSRestoreInterrupts`, `OSEnableInterrupts`, `DCFlushRange`, `DCInvalidateRange`, `DCStoreRange`, `ICInvalidateRange`, `OSResetSystem`, `OSGetConsoleType`, `OSSetErrorHandler`, `OSGetResetCode`, `OSGetSoundMode`, `OSSetSoundMode`, `OSGetProgressiveMode`, `OSSetProgressiveMode`, `OSGetLanguage`, `OSSetLanguage`.

- [ ] **Step 1: Write the failing alarm test**

```c
/* port/tests/os_alarm_test.c */
#include "check.h"
#include "os_shim/os_alarm.h"
static int fired; static void cb(OSAlarm* a, OSContext* c) { (void)a; (void)c; fired++; }
static void run(void) {
    OSAlarm a; OSInitAlarm(); fired = 0;
    OSSetAlarm(&a, 100, cb);
    port_alarm_tick(50);  CHECK_EQ_U32(fired, 0);
    port_alarm_tick(100); CHECK_EQ_U32(fired, 1);
    port_alarm_tick(500); CHECK_EQ_U32(fired, 1);          /* one-shot */
    OSSetPeriodicAlarm(&a, 1000, 10, cb);
    port_alarm_tick(1000); port_alarm_tick(1010); port_alarm_tick(1015);
    CHECK_EQ_U32(fired, 3);
    OSCancelAlarm(&a); port_alarm_tick(2000); CHECK_EQ_U32(fired, 3);
}
TEST_MAIN(run)
```

Add to `port/tests/CMakeLists.txt`: `port_add_test(os_alarm_test os_alarm_test.c ${PORT_SRC_DIR}/os_shim/os_alarm.c)` and set `target_compile_definitions(os_alarm_test PRIVATE PORT_HOST_TEST)`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset host-tests 2>&1 | tail -3`
Expected: compile error, `os_shim/os_alarm.h` not found.

- [ ] **Step 3: Implement**

```c
/* port/src/os_shim/os_alarm.h */
#ifndef PORT_OS_ALARM_H
#define PORT_OS_ALARM_H
#include <stdint.h>
#ifdef PORT_HOST_TEST
typedef int64_t OSTime; typedef struct OSContext OSContext; typedef struct OSAlarm OSAlarm;
typedef void (*OSAlarmHandler)(OSAlarm*, OSContext*);
struct OSAlarm { OSAlarmHandler handler; uint32_t tag; OSTime fire; OSAlarm* prev; OSAlarm* next; OSTime period; OSTime start; };
void OSInitAlarm(void); void OSSetAlarm(OSAlarm*, OSTime, OSAlarmHandler);
void OSSetPeriodicAlarm(OSAlarm*, OSTime, OSTime, OSAlarmHandler); void OSCancelAlarm(OSAlarm*);
#else
#include <dolphin/os.h>
#endif
void port_alarm_tick(OSTime now);
#endif
```

```c
/* port/src/os_shim/os_alarm.c — intrusive doubly linked list, sorted by fire time */
#include "os_alarm.h"
#include <stddef.h>
static OSAlarm* head;
static void unlink_alarm(OSAlarm* a) {
    if (a->prev) a->prev->next = a->next; else if (head == a) head = a->next;
    if (a->next) a->next->prev = a->prev;
    a->prev = a->next = NULL;
}
static void insert_alarm(OSAlarm* a) {
    OSAlarm** pp = &head; OSAlarm* prev = NULL;
    while (*pp && (*pp)->fire <= a->fire) { prev = *pp; pp = &(*pp)->next; }
    a->next = *pp; a->prev = prev; if (*pp) (*pp)->prev = a; *pp = a;
}
void OSInitAlarm(void) { head = NULL; }
void OSSetAlarm(OSAlarm* a, OSTime tick, OSAlarmHandler h) {
    if (a->prev || a->next || head == a) unlink_alarm(a);
    a->handler = h; a->period = 0; a->fire = tick; insert_alarm(a);
}
void OSSetPeriodicAlarm(OSAlarm* a, OSTime start, OSTime period, OSAlarmHandler h) {
    if (a->prev || a->next || head == a) unlink_alarm(a);
    a->handler = h; a->period = period; a->start = start; a->fire = start; insert_alarm(a);
}
void OSCancelAlarm(OSAlarm* a) { if (a->prev || a->next || head == a) unlink_alarm(a); a->handler = NULL; }
void port_alarm_tick(OSTime now) {
    while (head && head->fire <= now) {
        OSAlarm* a = head; unlink_alarm(a);
        if (a->period > 0) { a->fire += a->period; insert_alarm(a); }
        if (a->handler) a->handler(a, NULL);
    }
}
```

Note the one-shot semantics in the test: `OSSetAlarm` takes an absolute tick in the port (the SDK takes a relative tick; `main_loop.c` converts by adding `OSGetTime()` — see Task 10, and document this in `port.h`).

`port/src/os_shim/os_shim.c` (no test; trivial no-ops):
```c
#include <dolphin/os.h>
#include "../port.h"
static int irq_depth;
BOOL OSDisableInterrupts(void) { return irq_depth++ == 0; }
BOOL OSEnableInterrupts(void) { irq_depth = 0; return TRUE; }
BOOL OSRestoreInterrupts(BOOL level) { if (level) irq_depth = 0; else if (irq_depth) irq_depth--; return level; }
void DCFlushRange(void* p, u32 n) { (void)p; (void)n; }
void DCInvalidateRange(void* p, u32 n) { (void)p; (void)n; }
void DCStoreRange(void* p, u32 n) { (void)p; (void)n; }
void ICInvalidateRange(void* p, u32 n) { (void)p; (void)n; }
void OSResetSystem(int reset, u32 code, BOOL menu) { (void)reset; (void)code; (void)menu; port_log("OSResetSystem"); port_request_exit(); }
u32 OSGetConsoleType(void) { return OS_CONSOLE_RETAIL1; /* 0x00000001 in dolphin/os.h */ }
u32 OSGetResetCode(void) { return 0; }
OSErrorHandler OSSetErrorHandler(OSError e, OSErrorHandler h) { (void)e; (void)h; return NULL; }
u32 OSGetSoundMode(void) { return 1; } void OSSetSoundMode(u32 m) { (void)m; }
u32 OSGetProgressiveMode(void) { return 0; } void OSSetProgressiveMode(u32 m) { (void)m; }
u8 OSGetLanguage(void) { return 0; } void OSSetLanguage(u8 l) { (void)l; }
```

`port/src/port.h`:
```c
#ifndef PORT_H
#define PORT_H
#include <stdint.h>
void port_log(const char* fmt, ...);          /* console.log with [melee] prefix */
void port_yield(void);                        /* emscripten_sleep(0) under Asyncify; no-op on host */
void port_request_exit(void);                 /* stops the main loop */
#endif
```

- [ ] **Step 4: Run the test**

Run: `cmake --build --preset host-tests && ctest --preset host-tests`
Expected: `os_alarm_test ... Passed`

- [ ] **Step 5: Commit**

```bash
git add port/src/port.h port/src/os_shim port/tests/os_alarm_test.c port/tests/CMakeLists.txt
git commit -m "port: OS shim with a frame-driven OSAlarm implementation"
```

### Task 7: FST parser

**Files:**
- Create: `port/src/dvd_web/fst.h`, `port/src/dvd_web/fst.c`
- Test: `port/tests/fst_test.c`

**Interfaces:**
- Produces:
  ```c
  typedef struct { uint32_t fst_offset, fst_size; char game_id[7]; } port_disc_header;
  int  port_disc_parse_header(const uint8_t* sector0, size_t n, port_disc_header* out); /* 0 ok, -1 bad */
  typedef struct port_fst port_fst;
  port_fst* port_fst_parse(uint8_t* fst_bytes, uint32_t size);   /* takes ownership; swaps in place */
  int32_t   port_fst_lookup(const port_fst*, const char* path);   /* entry index or -1; case-insensitive like the SDK */
  int       port_fst_is_dir(const port_fst*, int32_t entry);
  uint32_t  port_fst_file_offset(const port_fst*, int32_t entry);
  uint32_t  port_fst_file_length(const port_fst*, int32_t entry);
  int32_t   port_fst_entry_count(const port_fst*);
  ```

GameCube FST layout (all big-endian): 12-byte entries `{u8 is_dir; u24 name_offset; u32 file_offset_or_parent; u32 file_length_or_next}`; the root entry's `next` is the total entry count; the string table follows the entries. Disc header: game ID at 0x000 (6 bytes), FST offset at 0x424, FST size at 0x428.

- [ ] **Step 1: Write the failing test with a synthetic FST**

```c
/* port/tests/fst_test.c */
#include "check.h"
#include "dvd_web/fst.h"
static void be32(uint8_t* p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
/* root(3 entries) -> dir "audio"(next=3) -> file "audio/us/title.hps" as "title.hps"? keep it flat: */
/* entries: 0 root, 1 dir "audio" (next=3), 2 file "smash2.sem" inside audio; strings: "\0audio\0smash2.sem\0" */
static void run(void) {
    uint8_t fst[12*3 + 32] = {0};
    be32(fst+0, 0x01000000u); be32(fst+4, 0); be32(fst+8, 3);            /* root: dir, next=3 */
    be32(fst+12, 0x01000001u); be32(fst+16, 0); be32(fst+20, 3);         /* "audio": dir, parent 0, next 3 */
    be32(fst+24, 0x00000007u); be32(fst+28, 0x1000); be32(fst+32, 0x40); /* "smash2.sem": name off 7 */
    memcpy(fst+36, "\0audio\0smash2.sem\0", 18);
    uint8_t* buf = malloc(sizeof fst); memcpy(buf, fst, sizeof fst);
    port_fst* f = port_fst_parse(buf, sizeof fst);
    CHECK(f != NULL);
    CHECK_EQ_U32(port_fst_entry_count(f), 3);
    CHECK_EQ_U32(port_fst_lookup(f, "audio/smash2.sem"), 2);
    CHECK_EQ_U32(port_fst_lookup(f, "AUDIO/SMASH2.SEM"), 2);
    CHECK_EQ_U32(port_fst_lookup(f, "/audio/smash2.sem"), 2);
    CHECK_EQ_U32(port_fst_lookup(f, "nope.dat"), (uint32_t)-1);
    CHECK_EQ_U32(port_fst_file_offset(f, 2), 0x1000);
    CHECK_EQ_U32(port_fst_file_length(f, 2), 0x40);
    CHECK(port_fst_is_dir(f, 1)); CHECK(!port_fst_is_dir(f, 2));
    uint8_t hdr[0x440] = {0}; memcpy(hdr, "GALE01", 6); be32(hdr+0x424, 0x456E00); be32(hdr+0x428, 0x7A00);
    port_disc_header h; CHECK_EQ_U32(port_disc_parse_header(hdr, sizeof hdr, &h), 0);
    CHECK(strcmp(h.game_id, "GALE01") == 0); CHECK_EQ_U32(h.fst_offset, 0x456E00); CHECK_EQ_U32(h.fst_size, 0x7A00);
    memcpy(hdr, "GALP01", 6); CHECK_EQ_U32(port_disc_parse_header(hdr, sizeof hdr, &h), (uint32_t)-1);
}
TEST_MAIN(run)
```

Add `port_add_test(fst_test fst_test.c ${PORT_SRC_DIR}/dvd_web/fst.c)`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset host-tests 2>&1 | tail -3` — Expected: `dvd_web/fst.h` not found.

- [ ] **Step 3: Implement**

```c
/* port/src/dvd_web/fst.c */
#include "fst.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
struct port_fst { uint8_t* bytes; uint32_t size; uint32_t count; const char* strings; };
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static const uint8_t* ent(const port_fst* f, int32_t i) { return f->bytes + 12u * (uint32_t)i; }
int port_disc_parse_header(const uint8_t* s, size_t n, port_disc_header* out) {
    if (n < 0x440 || memcmp(s, "GALE01", 6) != 0) return -1;
    memcpy(out->game_id, s, 6); out->game_id[6] = 0;
    out->fst_offset = rd32(s + 0x424); out->fst_size = rd32(s + 0x428);
    return 0;
}
port_fst* port_fst_parse(uint8_t* b, uint32_t size) {
    if (size < 12) return NULL;
    uint32_t count = rd32(b + 8);
    if (count == 0 || 12u * count > size) return NULL;
    port_fst* f = calloc(1, sizeof *f);
    f->bytes = b; f->size = size; f->count = count; f->strings = (const char*)(b + 12u * count);
    return f;
}
int32_t  port_fst_entry_count(const port_fst* f) { return (int32_t)f->count; }
int      port_fst_is_dir(const port_fst* f, int32_t i) { return ent(f, i)[0] != 0; }
uint32_t port_fst_file_offset(const port_fst* f, int32_t i) { return rd32(ent(f, i) + 4); }
uint32_t port_fst_file_length(const port_fst* f, int32_t i) { return rd32(ent(f, i) + 8); }
static const char* ent_name(const port_fst* f, int32_t i) { return f->strings + (rd32(ent(f, i)) & 0x00FFFFFFu); }
static int name_eq(const char* a, const char* b, size_t n) {
    for (size_t k = 0; k < n; k++) if (tolower((unsigned char)a[k]) != tolower((unsigned char)b[k])) return 0;
    return b[n] == 0;
}
int32_t port_fst_lookup(const port_fst* f, const char* path) {
    int32_t dir = 0;                              /* current directory entry */
    while (*path == '/') path++;
    while (*path) {
        const char* seg_end = strchr(path, '/');
        size_t n = seg_end ? (size_t)(seg_end - path) : strlen(path);
        int32_t end = (int32_t)rd32(ent(f, dir) + 8);           /* dir's "next" = first entry after it */
        int32_t i = dir + 1, found = -1;
        while (i < end) {
            if (name_eq(path, ent_name(f, i), n)) { found = i; break; }
            i = port_fst_is_dir(f, i) ? (int32_t)rd32(ent(f, i) + 8) : i + 1;
        }
        if (found < 0) return -1;
        if (!seg_end) return found;
        if (!port_fst_is_dir(f, found)) return -1;
        dir = found; path = seg_end + 1;
    }
    return -1;
}
```

`fst.h` declares exactly the interface block above plus `#include <stdint.h>` / `<stddef.h>`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build --preset host-tests && ctest --preset host-tests` — Expected: `fst_test ... Passed`

- [ ] **Step 5: Commit**

```bash
git add port/src/dvd_web/fst.c port/src/dvd_web/fst.h port/tests/fst_test.c port/tests/CMakeLists.txt
git commit -m "port: GameCube disc header and FST parser"
```

### Task 8: `dvd_web` — DVD API over an async byte source

**Files:**
- Create: `port/src/dvd_web/dvd_web.h`, `port/src/dvd_web/dvd_web.c`, `port/src/dvd_web/disc_io.h`
- Test: `port/tests/dvd_web_test.c`

**Interfaces:**
- Consumes: Task 7's `port_fst_*`.
- Produces:
  ```c
  /* disc_io.h — the only thing the JS side implements (Emscripten import) or the test fakes */
  typedef void (*port_disc_read_cb)(void* user, int status /*0 ok*/);
  void port_disc_read(uint32_t offset, uint32_t length, void* dst, port_disc_read_cb cb, void* user);
  uint32_t port_disc_size(void);
  /* dvd_web.h */
  int  port_dvd_init(const uint8_t* fst_bytes, uint32_t fst_size);  /* called by DVDInit after JS delivers the FST */
  void port_dvd_pump(void);                                          /* runs completed-read callbacks; once per frame and inside port_yield */
  ```
  plus the SDK entry points the game uses: `DVDInit`, `DVDConvertPathToEntrynum`, `DVDFastOpen`, `DVDOpen`, `DVDClose`, `DVDReadAsyncPrio`, `DVDReadPrio`, `DVDGetCommandBlockStatus`, `DVDGetDriveStatus`, `DVDGetCurrentDiskID`, `DVDCheckDisk`, `DVDSetAutoInvalidation`, `DVDGetFileInfoStatus`, `DVDCancel`, `DVDCancelAsync`.

- [ ] **Step 1: Write the failing test with a fake byte source**

```c
/* port/tests/dvd_web_test.c */
#include "check.h"
#include "dvd_web/dvd_web.h"
#include "dvd_web/disc_io.h"
#include <dolphin/dvd.h>
static uint8_t disc[0x2000];
static struct { port_disc_read_cb cb; void* user; uint32_t off, len; void* dst; int pending; } q;
void port_disc_read(uint32_t off, uint32_t len, void* dst, port_disc_read_cb cb, void* user) {
    q.cb = cb; q.user = user; q.off = off; q.len = len; q.dst = dst; q.pending = 1;
}
uint32_t port_disc_size(void) { return sizeof disc; }
static void deliver(void) { memcpy(q.dst, disc + q.off, q.len); q.pending = 0; q.cb(q.user, 0); }
static int done; static void cb(s32 result, DVDFileInfo* fi) { (void)fi; done = result; }
static void be32(uint8_t* p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static void run(void) {
    uint8_t fst[12*2 + 16] = {0};
    be32(fst+0, 0x01000000u); be32(fst+8, 2);
    be32(fst+12, 0x00000001u); be32(fst+16, 0x1000); be32(fst+20, 16);  /* "a.dat" at 0x1000, 16 bytes */
    memcpy(fst+24, "\0a.dat\0", 7);
    memcpy(disc + 0x1000, "0123456789abcdef", 16);
    CHECK_EQ_U32(port_dvd_init(fst, sizeof fst), 0);
    CHECK_EQ_U32(DVDConvertPathToEntrynum("a.dat"), 1);
    DVDFileInfo fi; CHECK(DVDFastOpen(1, &fi));
    CHECK_EQ_U32(fi.length, 16);
    uint8_t out[32] = {0}; done = -99;
    CHECK_EQ_U32(DVDReadAsyncPrio(&fi, out, 32, 0, cb, 2), 1);
    CHECK_EQ_U32(DVDGetCommandBlockStatus(&fi.cb), DVD_STATE_BUSY);
    port_dvd_pump(); CHECK_EQ_U32(done, (uint32_t)-99);            /* not delivered yet */
    deliver(); CHECK_EQ_U32(done, (uint32_t)-99);                  /* callback deferred to pump */
    port_dvd_pump(); CHECK_EQ_U32(done, 16);                        /* result = bytes read, clamped to file */
    CHECK_MEM_EQ(out, "0123456789abcdef", 16);
    CHECK_EQ_U32(DVDGetCommandBlockStatus(&fi.cb), DVD_STATE_END);
    CHECK(DVDClose(&fi));
}
TEST_MAIN(run)
```

Add `port_add_test(dvd_web_test dvd_web_test.c ${PORT_SRC_DIR}/dvd_web/dvd_web.c ${PORT_SRC_DIR}/dvd_web/fst.c)` and `target_include_directories(dvd_web_test PRIVATE ${CMAKE_SOURCE_DIR}/extern/aurora/include)` so Aurora's `dolphin/dvd.h` types are used on the host too.

- [ ] **Step 2: Run to verify it fails** — Expected: `dvd_web/dvd_web.h` not found.

- [ ] **Step 3: Implement**

```c
/* port/src/dvd_web/dvd_web.c */
#include "dvd_web.h"
#include "disc_io.h"
#include "fst.h"
#include <dolphin/dvd.h>
#include <stdlib.h>
#include <string.h>
static port_fst* g_fst;
typedef struct pending { DVDFileInfo* fi; int32_t result; struct pending* next; } pending;
static pending* g_done_head; static pending* g_done_tail;
int port_dvd_init(const uint8_t* fst_bytes, uint32_t n) {
    uint8_t* copy = malloc(n); memcpy(copy, fst_bytes, n);
    g_fst = port_fst_parse(copy, n);
    return g_fst ? 0 : -1;
}
void DVDInit(void) {}
s32 DVDConvertPathToEntrynum(const char* path) { return g_fst ? port_fst_lookup(g_fst, path) : -1; }
BOOL DVDFastOpen(s32 entry, DVDFileInfo* fi) {
    if (!g_fst || entry < 0 || entry >= port_fst_entry_count(g_fst) || port_fst_is_dir(g_fst, entry)) return FALSE;
    memset(fi, 0, sizeof *fi);
    fi->startAddr = port_fst_file_offset(g_fst, entry);
    fi->length = port_fst_file_length(g_fst, entry);
    fi->cb.state = DVD_STATE_END;
    return TRUE;
}
BOOL DVDOpen(const char* path, DVDFileInfo* fi) { s32 e = DVDConvertPathToEntrynum(path); return e >= 0 && DVDFastOpen(e, fi); }
BOOL DVDClose(DVDFileInfo* fi) { fi->cb.state = DVD_STATE_END; return TRUE; }
static void on_read(void* user, int status) {
    DVDFileInfo* fi = user;
    pending* p = calloc(1, sizeof *p); p->fi = fi;
    p->result = status == 0 ? (int32_t)fi->cb.transferredSize : -1;
    if (g_done_tail) g_done_tail->next = p; else g_done_head = p;
    g_done_tail = p;
}
s32 DVDReadAsyncPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, DVDCallback cb, s32 prio) {
    (void)prio;
    if (offset < 0 || (u32)offset > fi->length) return 0;
    u32 n = (u32)length; if (offset + n > fi->length) n = fi->length - (u32)offset;
    fi->callback = cb; fi->cb.state = DVD_STATE_BUSY; fi->cb.transferredSize = n;
    fi->cb.addr = addr; fi->cb.length = n; fi->cb.offset = fi->startAddr + (u32)offset;
    port_disc_read(fi->startAddr + (u32)offset, n, addr, on_read, fi);
    return 1;
}
s32 DVDReadPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, s32 prio) {
    if (!DVDReadAsyncPrio(fi, addr, length, offset, NULL, prio)) return -1;
    while (fi->cb.state == DVD_STATE_BUSY) port_yield();   /* Asyncify point */
    return (s32)fi->cb.transferredSize;
}
void port_dvd_pump(void) {
    while (g_done_head) {
        pending* p = g_done_head; g_done_head = p->next; if (!g_done_head) g_done_tail = NULL;
        p->fi->cb.state = p->result < 0 ? DVD_STATE_FATAL_ERROR : DVD_STATE_END;
        if (p->fi->callback) p->fi->callback(p->result, p->fi);
        free(p);
    }
}
s32 DVDGetCommandBlockStatus(const DVDCommandBlock* b) { return b->state; }
s32 DVDGetFileInfoStatus(const DVDFileInfo* fi) { return fi->cb.state; }
s32 DVDGetDriveStatus(void) { return DVD_STATE_END; }
DVDDiskID* DVDGetCurrentDiskID(void) { static DVDDiskID id = { "GALE", "01", 0, 0, 0, 0, {0} }; return &id; }
BOOL DVDCheckDisk(void) { return TRUE; }
void DVDSetAutoInvalidation(BOOL v) { (void)v; }
s32 DVDCancel(DVDCommandBlock* b) { (void)b; return 0; }
s32 DVDCancelAsync(DVDCommandBlock* b, DVDCBCallback cb) { (void)b; (void)cb; return 1; }
```

`port_yield()` on the host (tests) is provided by a `port_host_stubs.c` added to every test target that needs it: `void port_yield(void) {}` plus `port_log` printing to stderr. Include it in `dvd_web_test`'s sources. Field names (`startAddr`, `length`, `cb.state`, `cb.transferredSize`, `callback`) must match Aurora's `dolphin/dvd.h`; check `port/extern/aurora/include/dolphin/dvd.h` and adjust names, not semantics.

- [ ] **Step 4: Run the tests** — Expected: `dvd_web_test ... Passed`

- [ ] **Step 5: Commit**

```bash
git add port/src/dvd_web port/tests/dvd_web_test.c port/tests/port_host_stubs.c port/tests/CMakeLists.txt
git commit -m "port: DVD layer over an async byte source with frame-pumped callbacks"
```

### Task 9: Aurora patches for the game's API surface

**Files:**
- Create: `port/extern/aurora-patches/0002-single-threaded-option.patch`, `0003-melee-api-gaps.patch`
- Modify: `port/cmake/game_sources.cmake` (remove `fog.c pobj.c video.c lb_0195.c lbcardnew.c` from `GAME_EXCLUDE`)

**Interfaces:**
- Produces: `AURORA_SINGLE_THREADED` CMake option; functions `GXInitFogAdjTable`, `GXSetFogRangeAdj`, `PADSetSamplingRate`, the `VIPadFrameBufferWidth` macro, `GXSetArray` C-compatible 4-argument macro (`#define GXSetArray(a,d,s,st) GXSetArrayEx(a,d,s,st,false)` under `__EMSCRIPTEN__` in `include/dolphin/gx/GXGeometry.h` with the 5-argument function renamed `GXSetArrayEx`), and `CARDReadAsync`/`CARDWriteAsync` completing via a pump called from `port_frame` (they already exist; ensure callbacks fire synchronously or from `aurora_update`).

- [ ] **Step 1: Write the single-threaded option patch**

In `port/extern/aurora`, edit then `git diff > ../aurora-patches/0002-single-threaded-option.patch`:
- `CMakeLists.txt`: `option(AURORA_SINGLE_THREADED "No worker threads (Emscripten)" OFF)`; when ON, `target_compile_definitions(aurora_core PUBLIC AURORA_SINGLE_THREADED)`.
- `lib/gfx/pipeline_cache.cpp`: under `#ifdef AURORA_SINGLE_THREADED`, `start_pipeline_thread()` and the cache writer do nothing; `compile_pending()` compiles inline when called from `end_frame`.
- `lib/gfx/render_worker.cpp`: `start()` returns without setting `g_running` (the existing inline path then handles all work).
- `lib/dolphin/dvd`: excluded from the build when `AURORA_ENABLE_DVD=OFF` (already), nothing else.

- [ ] **Step 2: Write the API-gap patch**

```cpp
// lib/dolphin/gx/GXPixel.cpp (append)
void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, f32 projmtx[4][4]) {
  // Fog range adjustment is a hardware artifact correction; the WebGPU path applies none.
  (void)width; (void)projmtx; std::memset(table, 0, sizeof(*table));
}
void GXSetFogRangeAdj(u8 enable, u16 center, const GXFogAdjTable* table) { (void)enable; (void)center; (void)table; }
// include/dolphin/vi.h (append; the SDK defines this as a macro, see extern/dolphin/include/dolphin/vi.h:9)
#define VIPadFrameBufferWidth(width) ((u16)(((u16)(width) + 15) & ~15))
// lib/dolphin/pad/pad.cpp (append; SDK signature: void PADSetSamplingRate(unsigned long msec))
void PADSetSamplingRate(unsigned long msec) { (void)msec; }
```

- [ ] **Step 3: Re-run setup to apply, rebuild the game library**

Run: `bash port/tools/setup.sh && cd port && cmake --build --preset web-debug --target melee_game`
Expected: the five re-enabled units compile. If `pobj.c` still fails on `GXSetArray`, the macro is missing from the header; fix the patch rather than editing `pobj.c`.

- [ ] **Step 4: Commit**

```bash
git add port/extern/aurora-patches port/cmake/game_sources.cmake
git commit -m "port: Aurora patches — single-threaded mode and Melee API gaps"
```

### Task 10: Main loop adapter, silent audio, first link

**Files:**
- Create: `port/src/main_loop.c`, `port/src/ax_hle/ax_stub.c`, `port/src/js_bridge.c`, `port/web/js/imports.js`, `port/web/js/disc_source.js`, `port/web/js/boot.js`, `port/web/shell/index.html`, `port/cmake/emscripten_link.cmake`
- Modify: `src/melee/gm/gmmain.c` (`main` → `melee_main` under `TARGET_PC`; skip `init_spr_unk`; keep `CARDInit`), `src/melee/gm/gmscene.c:271-384` (extract loop body), `port/CMakeLists.txt`, `port/cmake/game_sources.cmake` (re-enable `gmmain.c`)

**Interfaces:**
- Consumes: `port_dvd_pump`, `port_alarm_tick`, Aurora `aurora_initialize/update/begin_frame/end_frame`.
- Produces: `int port_main(int, char**)` (the wasm entry), `void port_frame(void*)`, `void port_yield(void)`; game-side `int gm_801A4D34_frame(void)` returning 0 when the scene loop wants to exit.
- JS imports (`imports.js`, exposed through `--js-library`): `port_disc_read(off, len, dstPtr, cbPtr, userPtr)`, `port_disc_size()`, `port_log(ptr)`, `port_audio_push(ptr, frames)`.

- [ ] **Step 1: Patch the game loop**

In `src/melee/gm/gmscene.c`, wrap the body of the `while (temp_r25->unk_C == 0)` loop in `gm_801A4D34` so that under `TARGET_PC` it becomes:

```c
#ifdef TARGET_PC
/* One iteration of the scene loop. Returns 0 when the loop condition ends. */
int gm_801A4D34_frame(void)
{
    /* identical body to the while loop below, with `break` replaced by `return 0` */
    ...
    return temp_r25->unk_C == 0;
}
#endif
```

with the original `while` kept under `#ifndef TARGET_PC`, and the state the loop reads (`temp_r25`) hoisted to a file-static under `TARGET_PC`. The inner `while ((pad_queue_count = lb_80019894()) == 0)` spin becomes `if (pad_queue_count == 0) return 1;` under `TARGET_PC` (the port ticks the pad queue before each frame, so this only guards an empty frame).

In `src/melee/gm/gmmain.c`: `#ifdef TARGET_PC int melee_main(void) #else int main(void) #endif`, and guard `init_spr_unk()`'s call with `#ifndef TARGET_PC`.

- [ ] **Step 2: Write the adapter**

```c
/* port/src/main_loop.c */
#include <emscripten.h>
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <dolphin/os.h>
#include "port.h"
#include "dvd_web/dvd_web.h"
#include "os_shim/os_alarm.h"
int melee_main(void);
int gm_801A4D34_frame(void);
void HSD_PadRenewMasterStatus(void);
static int g_exit;
void port_request_exit(void) { g_exit = 1; }
void port_yield(void) { port_dvd_pump(); emscripten_sleep(0); }
static void handle_events(void) {
    const AuroraEvent* e = aurora_update();
    for (; e && e->type != AURORA_NONE; ++e) if (e->type == AURORA_EXIT) g_exit = 1;
}
void port_frame(void* arg) {
    (void)arg;
    handle_events();
    if (g_exit) { emscripten_cancel_main_loop(); aurora_shutdown(); return; }
    port_dvd_pump();
    port_alarm_tick(OSGetTime());
    HSD_PadRenewMasterStatus();            /* what the VI retrace interrupt does on hardware */
    if (!aurora_begin_frame()) return;
    if (!gm_801A4D34_frame()) g_exit = 1;
    aurora_end_frame();
}
int main(int argc, char** argv) {
    AuroraConfig cfg = { .appName = "Melee", .mem1Size = MEM1_DEFAULT_SIZE, .mem2Size = ARAM_DEFAULT_SIZE,
                         .desiredBackend = BACKEND_WEBGPU, .vsync = true, .windowWidth = 1280, .windowHeight = 960 };
    aurora_initialize(argc, argv, &cfg);
    melee_main();                          /* runs the init chain; returns after gm_801A4510 is patched not to loop */
    emscripten_set_main_loop_arg(port_frame, NULL, 0, 1);
    return 0;
}
```

`gm_801A4510()` (called at the end of `melee_main`) leads into the scene loop; under `TARGET_PC` the call site in `gm_1A3F.c` that enters `gm_801A4D34` is made to return after scene setup instead of looping — mark it with the same `#ifdef TARGET_PC` pattern and document the exact function in `port/docs/milestones.md`.

- [ ] **Step 3: Write the silent audio stub and JS bridge**

`port/src/ax_hle/ax_stub.c`: empty bodies for `DSPInit`, `DSPCheckInit`, `DSPAddTask`, `DSPSendMailToDSP`, `DSPCheckMailToDSP`, `DSPAssertTask`, `AIInit`, `AIInitDMA`, `AIStartDMA`, `AIStopDMA`, `AIRegisterDMACallback` (returns previous), `AISetDSPSampleRate`, `AIGetDSPSampleRate` (returns 1), `AISetStreamVolLeft/Right`, `AISetStreamPlayState`, `AIGetStreamPlayState`, `AIGetStreamSampleRate`, `AISetStreamSampleRate`, `AIReset`. Task 21 replaces this file.

`port/src/js_bridge.c`: `port_log` (vsnprintf → `emscripten_log` / imported `port_log`).

```js
// port/web/js/imports.js  (emcc --js-library)
mergeInto(LibraryManager.library, {
  port_disc_read: function (off, len, dst, cb, user) {
    Module.discSource.read(off >>> 0, len >>> 0).then(buf => {
      HEAPU8.set(new Uint8Array(buf), dst); {{{ makeDynCall('vii', 'cb') }}}(user, 0);
    }, () => { {{{ makeDynCall('vii', 'cb') }}}(user, -1); });
  },
  port_disc_size: function () { return Module.discSource.size; },
  port_log: function (p) { console.log('[melee] ' + UTF8ToString(p)); },
  port_audio_push: function (ptr, frames) { if (Module.audioSink) Module.audioSink.push(HEAPF32.subarray(ptr >> 2, (ptr >> 2) + frames * 2)); },
});
```

```js
// port/web/js/disc_source.js
export class DiscSource {
  constructor(file) { this.file = file; this.size = file.size; }
  async read(offset, length) { return this.file.slice(offset, offset + length).arrayBuffer(); }
  async validate() {
    const hdr = new Uint8Array(await this.read(0, 0x440));
    const id = String.fromCharCode(...hdr.subarray(0, 6));
    if (id !== 'GALE01') throw new Error(`Expected a GALE01 (NTSC-U Melee) disc, got "${id}"`);
    const dv = new DataView(hdr.buffer);
    return { fstOffset: dv.getUint32(0x424), fstSize: dv.getUint32(0x428) };
  }
}
```

`boot.js`: checks `navigator.gpu`, wires `<input type=file>` to `new DiscSource(file)`, calls `validate()`, reads the FST bytes, then starts the module with `Module.discSource` set and `Module.preRun` pushing the FST into wasm through `ccall('port_dvd_init', 'number', ['array','number'], [fstBytes, fstBytes.length])`.

- [ ] **Step 4: Write the link flags and executable target**

```cmake
# port/cmake/emscripten_link.cmake
set(PORT_LINK_FLAGS
  --use-port=emdawnwebgpu -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536
  -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=1024MB -sINITIAL_MEMORY=256MB
  -sEXPORTED_FUNCTIONS=_main,_port_dvd_init -sEXPORTED_RUNTIME_METHODS=ccall,HEAPU8,HEAPF32
  -sSTACK_SIZE=1MB -sNO_EXIT_RUNTIME=1
  --js-library ${CMAKE_CURRENT_SOURCE_DIR}/web/js/imports.js
  -lidbfs.js)
if (PORT_SINGLE_FILE)
  list(APPEND PORT_LINK_FLAGS -sSINGLE_FILE=1)
endif ()
```

In `port/CMakeLists.txt` under `if (EMSCRIPTEN)`: add Aurora (`AURORA_SINGLE_THREADED ON`, `AURORA_ENABLE_DVD OFF`, `AURORA_ENABLE_THP OFF` for now), `add_executable(melee src/main_loop.c src/js_bridge.c src/os_shim/os_shim.c src/os_shim/os_alarm.c src/dvd_web/dvd_web.c src/dvd_web/fst.c src/ax_hle/ax_stub.c)`, link `melee_game aurora::core aurora::gx aurora::vi aurora::pad aurora::card aurora::main`, apply `PORT_LINK_FLAGS`, and copy `web/shell/index.html` + `web/js/*.js` to the binary dir post-build.

- [ ] **Step 5: Link and fix undefined symbols**

Run: `cmake --build --preset web-debug 2>&1 | grep -E 'undefined symbol|error' | sort | uniq -c | sort -rn | head -40`

Expected: a list of undefined SDK symbols. Rule for each: if Aurora should own it (GX/VI/PAD/CARD/AR/OS-memory) → add to `0003-melee-api-gaps.patch`; if it is OS/DSP/AI/EXI/SI → `os_shim.c` or `ax_stub.c`; if it is MSL/`Runtime` (e.g. `__div2i`, `sqrtf__Ff`) → provide in `port/src/os_shim/runtime_shim.c` using libc. Repeat until the link succeeds. Record the final list in `port/docs/milestones.md`.

- [ ] **Step 6: Boot in Chrome**

Run: `python3 -m http.server -d port/build/web-debug 8080`, open, pick an ISO.
Expected: console shows `[melee]`-prefixed output from the game's first `OSReport` calls; no uncaught exception before the first DVD read. Record the console output and wasm size (S2 measurement) in `spikes.md`.

- [ ] **Step 7: Commit**

```bash
git add port src/melee/gm/gmmain.c src/melee/gm/gmscene.c src/melee/gm/gm_1A3F.c
git commit -m "port: main loop adapter, JS bridge, first wasm link (M1)"
```

---

## Milestone M2 — Archive conversion and the title screen

### Task 11: Archive header, relocation and symbol-table swap

**Files:**
- Create: `port/src/hsd_endian/archive_swap.h`, `port/src/hsd_endian/archive_swap.c`
- Test: `port/tests/archive_swap_test.c`
- Modify: `src/sysdolphin/baselib/archive.c` (under `TARGET_PC`, `HSD_ArchiveParse` calls `port_archive_parse`)

**Interfaces:**
- Produces:
  ```c
  typedef struct { uint32_t file_size, data_size, nb_reloc, nb_public, nb_extern; } port_archive_hdr;
  /* Swaps header, reloc table, public/extern tables, and every relocated u32 in data; relocates them by adding `data`.
     Fills `reloc_set` (sorted offsets) for the walker. Returns 0, or -1 on a malformed archive. */
  int port_archive_fixup(uint8_t* file, uint32_t file_size, port_archive_hdr* hdr,
                         uint32_t** reloc_set_out, uint32_t* reloc_count_out);
  ```

- [ ] **Step 1: Write the failing test**

```c
/* port/tests/archive_swap_test.c */
#include "check.h"
#include "hsd_endian/archive_swap.h"
static void be32(uint8_t* p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static void run(void) {
    /* data: 16 bytes: [ptr to +8][u32 0x11223344][u32 0][u32 0] ; 1 reloc at 0 ; 1 public "root" at data+0 */
    uint8_t f[0x20 + 16 + 4 + 8 + 8] = {0};
    uint32_t data_size = 16, nb_reloc = 1, nb_public = 1, nb_extern = 0;
    be32(f+0, sizeof f); be32(f+4, data_size); be32(f+8, nb_reloc); be32(f+12, nb_public); be32(f+16, nb_extern);
    uint8_t* data = f + 0x20;
    be32(data+0, 8); be32(data+4, 0x11223344u);
    be32(data+16, 0);                       /* reloc[0].offset = 0 */
    be32(data+20, 0); be32(data+24, 0);     /* public[0] = {offset 0, symbol 0} */
    memcpy(data+28, "root\0", 5);
    port_archive_hdr h; uint32_t* rs; uint32_t rn;
    CHECK_EQ_U32(port_archive_fixup(f, sizeof f, &h, &rs, &rn), 0);
    CHECK_EQ_U32(h.nb_reloc, 1); CHECK_EQ_U32(h.nb_public, 1);
    CHECK_EQ_U32(rn, 1); CHECK_EQ_U32(rs[0], 0);
    uint32_t p; memcpy(&p, data, 4);
    CHECK(p == (uint32_t)(uintptr_t)(data + 8));           /* relocated, native-endian */
    uint32_t v; memcpy(&v, data+4, 4); CHECK_EQ_U32(v, 0x44332211u); /* NOT swapped: walker's job */
    be32(f+0, 1234); CHECK_EQ_U32(port_archive_fixup(f, sizeof f, &h, &rs, &rn), (uint32_t)-1); /* size mismatch */
}
TEST_MAIN(run)
```

- [ ] **Step 2: Run to verify it fails** — Expected: header not found.

- [ ] **Step 3: Implement**

```c
/* port/src/hsd_endian/archive_swap.c */
#include "archive_swap.h"
#include <stdlib.h>
#include <string.h>
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void wr_native(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
static int cmp_u32(const void* a, const void* b) { uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b; return x < y ? -1 : x > y; }
int port_archive_fixup(uint8_t* f, uint32_t n, port_archive_hdr* h, uint32_t** rs_out, uint32_t* rn_out) {
    if (n < 0x20) return -1;
    h->file_size = rd32(f); h->data_size = rd32(f+4); h->nb_reloc = rd32(f+8); h->nb_public = rd32(f+12); h->nb_extern = rd32(f+16);
    if (h->file_size != n) return -1;
    uint8_t* data = f + 0x20;
    uint8_t* reloc = data + h->data_size;
    uint8_t* pub   = reloc + 4u * h->nb_reloc;
    uint8_t* ext   = pub + 8u * h->nb_public;
    if ((uint32_t)(ext + 8u * h->nb_extern - f) > n) return -1;
    uint32_t* rs = malloc(4u * (h->nb_reloc ? h->nb_reloc : 1));
    for (uint32_t i = 0; i < h->nb_reloc; i++) {
        uint32_t off = rd32(reloc + 4u*i); wr_native(reloc + 4u*i, off);
        if (off + 4 > h->data_size) { free(rs); return -1; }
        rs[i] = off;
        wr_native(data + off, rd32(data + off) + (uint32_t)(uintptr_t)data);
    }
    for (uint32_t i = 0; i < h->nb_public; i++) { wr_native(pub+8*i, rd32(pub+8*i)); wr_native(pub+8*i+4, rd32(pub+8*i+4)); }
    for (uint32_t i = 0; i < h->nb_extern; i++) { wr_native(ext+8*i, rd32(ext+8*i)); wr_native(ext+8*i+4, rd32(ext+8*i+4)); }
    qsort(rs, h->nb_reloc, 4, cmp_u32);
    *rs_out = rs; *rn_out = h->nb_reloc;
    return 0;
}
```

Then in `src/sysdolphin/baselib/archive.c`, under `#ifdef TARGET_PC`, `HSD_ArchiveParse` becomes: call `port_archive_fixup`, copy `hdr` into `archive->header`, set the `data/reloc_info/public_info/extern_info/symbols` pointers exactly as the original does, skip the original `Locate()`, then call `port_archive_swap_roots(archive, reloc_set, reloc_count)` (Task 14; until then a no-op declared in `archive_swap.h`).

- [ ] **Step 4: Run the tests** — Expected: `archive_swap_test ... Passed`

- [ ] **Step 5: Commit**

```bash
git add port/src/hsd_endian/archive_swap.[ch] port/tests/archive_swap_test.c port/tests/CMakeLists.txt src/sysdolphin/baselib/archive.c
git commit -m "port: archive header/relocation fixup for little-endian hosts"
```

### Task 12: Schema descriptors and the graph walker

**Files:**
- Create: `port/src/hsd_endian/schema.h`, `port/src/hsd_endian/walker.h`, `port/src/hsd_endian/walker.c`
- Test: `port/tests/walker_test.c`

**Interfaces:**
- Produces:
  ```c
  typedef enum { F_U8, F_U16, F_U32, F_U64, F_F32, F_F64, F_PTR, F_STRUCT, F_ARRAY, F_PTR_ARRAY, F_BITS, F_UNION, F_OPAQUE } port_fkind;
  typedef enum { LEN_CONST, LEN_FIELD_U32, LEN_FIELD_U16, LEN_FIELD_U8, LEN_NULL_TERM } port_lenkind;
  typedef struct port_type port_type;
  typedef struct {
      port_fkind kind; uint32_t offset;           /* byte offset in parent */
      const port_type* type;                      /* F_PTR/F_STRUCT/F_ARRAY/F_PTR_ARRAY/F_UNION element type */
      port_lenkind len_kind; uint32_t len;        /* LEN_CONST: count; LEN_FIELD_*: byte offset of the count field in the parent */
      uint8_t bits_storage;                       /* F_BITS: storage bytes (1/2/4) */
      const uint8_t* bit_widths; uint8_t nbits;   /* F_BITS: declared widths, in declaration order */
      uint32_t disc_offset; const uint32_t* disc_values; const port_type* const* disc_types; uint8_t ncases; /* F_UNION */
  } port_field;
  struct port_type { const char* name; uint32_t size; const port_field* fields; uint32_t nfields; };
  typedef struct { const uint8_t* base; uint32_t size; const uint32_t* reloc_set; uint32_t reloc_count;
                   void* visited; int strict; } port_walk_ctx;
  int  port_walk_ctx_init(port_walk_ctx*, const uint8_t* base, uint32_t size, const uint32_t* reloc_set, uint32_t reloc_count, int strict);
  void port_walk_ctx_free(port_walk_ctx*);
  int  port_walk(port_walk_ctx*, const port_type*, void* obj);   /* 0 ok; -1 on a strict-mode violation */
  ```
  Walker rules: skip (and, in strict mode, assert) any 4-byte field whose archive offset is in `reloc_set` unless its kind is `F_PTR`; never swap an address twice (visited set of `(addr,type)`); `F_OPAQUE` fields are skipped; `F_PTR` fields are followed (target type `type`) when non-null and inside `[base, base+size)`; `F_ARRAY` is inline; `F_PTR_ARRAY` is a pointer to `len` elements; `F_BITS` re-packs from MSB-first to LSB-first order.

- [ ] **Step 1: Write the failing test**

```c
/* port/tests/walker_test.c */
#include "check.h"
#include "hsd_endian/walker.h"
typedef struct { uint16_t a; uint16_t b; float f; } Leaf;                    /* 8 bytes */
static const port_field leaf_fields[] = { {F_U16,0}, {F_U16,2}, {F_F32,4} };
static const port_type Leaf_t = { "Leaf", 8, leaf_fields, 3 };
typedef struct { uint32_t n; Leaf* items; Leaf* shared; uint32_t flags; } Root;   /* 16 bytes, flags is a bitfield group */
static const uint8_t root_bits[] = { 3, 5, 24 };
static const port_field root_fields[] = {
    {F_U32, 0}, {F_PTR_ARRAY, 4, &Leaf_t, LEN_FIELD_U32, 0}, {F_PTR, 8, &Leaf_t},
    {F_BITS, 12, NULL, 0, 0, 4, root_bits, 3} };
static const port_type Root_t = { "Root", 16, root_fields, 4 };
static void be16(uint8_t* p, uint16_t v) { p[0]=v>>8; p[1]=v; }
static void be32(uint8_t* p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static void run(void) {
    uint8_t buf[16 + 2*8] = {0};                       /* Root at 0, items[2] at 16; shared -> items[1] */
    be32(buf+0, 2);
    uint32_t p_items = (uint32_t)(uintptr_t)(buf+16), p_shared = (uint32_t)(uintptr_t)(buf+24);
    memcpy(buf+4, &p_items, 4); memcpy(buf+8, &p_shared, 4);    /* already relocated (native) by Task 11 */
    be32(buf+12, (0x5u << 29) | (0x1Fu << 24) | 0x123456u);       /* MSB-first: a=5 (3 bits), b=31 (5 bits), c=0x123456 */
    be16(buf+16, 0x1234); be16(buf+18, 0xABCD); be32(buf+20, 0x3F800000u);
    be16(buf+24, 0x0001); be16(buf+26, 0x0002); be32(buf+28, 0x40000000u);
    uint32_t relocs[] = { 4, 8 };
    port_walk_ctx c; CHECK_EQ_U32(port_walk_ctx_init(&c, buf, sizeof buf, relocs, 2, 1), 0);
    CHECK_EQ_U32(port_walk(&c, &Root_t, buf), 0);
    Root* r = (Root*)buf;
    CHECK_EQ_U32(r->n, 2);
    CHECK_EQ_U32(r->items[0].a, 0x1234); CHECK_EQ_U32(r->items[0].b, 0xABCD); CHECK(r->items[0].f == 1.0f);
    CHECK_EQ_U32(r->items[1].a, 1); CHECK_EQ_U32(r->items[1].b, 2); CHECK(r->items[1].f == 2.0f); /* visited once via shared */
    CHECK_EQ_U32(r->flags & 7u, 5); CHECK_EQ_U32((r->flags >> 3) & 31u, 31); CHECK_EQ_U32(r->flags >> 8, 0x123456u); /* LSB-first */
    /* strict mode: a U32 field sitting on a reloc offset is an error */
    static const port_field bad_fields[] = { {F_U32, 4} }; static const port_type Bad_t = { "Bad", 16, bad_fields, 1 };
    port_walk_ctx c2; port_walk_ctx_init(&c2, buf, sizeof buf, relocs, 2, 1);
    CHECK_EQ_U32(port_walk(&c2, &Bad_t, buf), (uint32_t)-1);
    port_walk_ctx_free(&c); port_walk_ctx_free(&c2);
}
TEST_MAIN(run)
```

- [ ] **Step 2: Run to verify it fails** — Expected: header not found.

- [ ] **Step 3: Implement**

```c
/* port/src/hsd_endian/walker.c */
#include "walker.h"
#include <stdlib.h>
#include <string.h>
typedef struct { uintptr_t addr; const port_type* type; } vkey;
typedef struct { vkey* keys; uint32_t n, cap; } vset;
static int vset_add(vset* s, uintptr_t a, const port_type* t) {   /* returns 1 if newly added */
    for (uint32_t i = 0; i < s->n; i++) if (s->keys[i].addr == a && s->keys[i].type == t) return 0;
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 256; s->keys = realloc(s->keys, s->cap * sizeof *s->keys); }
    s->keys[s->n].addr = a; s->keys[s->n].type = t; s->n++; return 1;
}
int port_walk_ctx_init(port_walk_ctx* c, const uint8_t* base, uint32_t size, const uint32_t* rs, uint32_t rn, int strict) {
    memset(c, 0, sizeof *c); c->base = base; c->size = size; c->reloc_set = rs; c->reloc_count = rn; c->strict = strict;
    c->visited = calloc(1, sizeof(vset)); return c->visited ? 0 : -1;
}
void port_walk_ctx_free(port_walk_ctx* c) { vset* s = c->visited; if (s) { free(s->keys); free(s); } c->visited = NULL; }
static int in_reloc(const port_walk_ctx* c, uint32_t off) {
    uint32_t lo = 0, hi = c->reloc_count;
    while (lo < hi) { uint32_t m = (lo + hi) / 2; if (c->reloc_set[m] < off) lo = m + 1; else if (c->reloc_set[m] > off) hi = m; else return 1; }
    return 0;
}
static int inside(const port_walk_ctx* c, const void* p, uint32_t n) {
    const uint8_t* q = p; return q >= c->base && q + n <= c->base + c->size;
}
static void swap16(uint8_t* p) { uint8_t t = p[0]; p[0] = p[1]; p[1] = t; }
static void swap32(uint8_t* p) { uint8_t t = p[0]; p[0] = p[3]; p[3] = t; t = p[1]; p[1] = p[2]; p[2] = t; }
static void swap64(uint8_t* p) { for (int i = 0; i < 4; i++) { uint8_t t = p[i]; p[i] = p[7-i]; p[7-i] = t; } }
static void repack_bits(uint8_t* p, uint8_t storage, const uint8_t* widths, uint8_t n) {
    uint64_t v = 0;                              /* read big-endian numeric value */
    for (int i = 0; i < storage; i++) v = (v << 8) | p[i];
    uint64_t out = 0; unsigned total = storage * 8u, msb_pos = total, lsb_pos = 0;
    for (uint8_t i = 0; i < n; i++) {
        unsigned w = widths[i]; msb_pos -= w;
        uint64_t field = (v >> msb_pos) & ((1ull << w) - 1);
        out |= field << lsb_pos; lsb_pos += w;
    }
    if (lsb_pos < total) out |= (v & ((1ull << (total - lsb_pos)) - 1)) << lsb_pos;  /* trailing padding bits keep numeric position */
    for (int i = 0; i < storage; i++) p[i] = (uint8_t)(out >> (8 * i));            /* write little-endian */
}
static uint32_t read_len(const port_field* f, const uint8_t* obj) {
    uint32_t v = 0;
    switch (f->len_kind) {
    case LEN_CONST: return f->len;
    case LEN_FIELD_U32: memcpy(&v, obj + f->len, 4); return v;
    case LEN_FIELD_U16: { uint16_t s; memcpy(&s, obj + f->len, 2); return s; }
    case LEN_FIELD_U8: return obj[f->len];
    case LEN_NULL_TERM: return 0xFFFFFFFFu;
    }
    return 0;
}
static int walk_obj(port_walk_ctx* c, const port_type* t, uint8_t* obj);
static int walk_field(port_walk_ctx* c, const port_field* f, uint8_t* obj) {
    uint8_t* p = obj + f->offset;
    uint32_t off = (uint32_t)(p - c->base);
    if (f->kind != F_PTR && f->kind != F_OPAQUE && f->kind != F_STRUCT && f->kind != F_ARRAY && f->kind != F_UNION && in_reloc(c, off))
        return c->strict ? -1 : 0;
    switch (f->kind) {
    case F_U8: case F_OPAQUE: return 0;
    case F_U16: swap16(p); return 0;
    case F_U32: case F_F32: swap32(p); return 0;
    case F_U64: case F_F64: swap64(p); return 0;
    case F_BITS: repack_bits(p, f->bits_storage, f->bit_widths, f->nbits); return 0;
    case F_STRUCT: return walk_obj(c, f->type, p);
    case F_ARRAY: { uint32_t n = read_len(f, obj); for (uint32_t i = 0; i < n; i++) if (walk_obj(c, f->type, p + i * f->type->size)) return -1; return 0; }
    case F_PTR: { uint8_t* q; memcpy(&q, p, sizeof q); if (!q || !f->type) return 0; return inside(c, q, f->type->size) ? walk_obj(c, f->type, q) : (c->strict ? -1 : 0); }
    case F_PTR_ARRAY: {
        uint8_t* q; memcpy(&q, p, sizeof q); if (!q) return 0;
        uint32_t n = read_len(f, obj);
        for (uint32_t i = 0; i < n; i++) {
            uint8_t* e = q + i * f->type->size;
            if (!inside(c, e, f->type->size)) return c->strict ? -1 : 0;
            if (f->len_kind == LEN_NULL_TERM) { uint8_t* z; memcpy(&z, e, sizeof z); if (!z) break; }
            if (walk_obj(c, f->type, e)) return -1;
        }
        return 0; }
    case F_UNION: {
        uint32_t d; memcpy(&d, obj + f->disc_offset, 4);   /* discriminator already swapped: generator orders it first */
        for (uint8_t i = 0; i < f->ncases; i++) if (f->disc_values[i] == d) return walk_obj(c, f->disc_types[i], p);
        return c->strict ? -1 : 0; }
    }
    return -1;
}
static int walk_obj(port_walk_ctx* c, const port_type* t, uint8_t* obj) {
    if (!vset_add(c->visited, (uintptr_t)obj, t)) return 0;
    for (uint32_t i = 0; i < t->nfields; i++) if (walk_field(c, &t->fields[i], obj)) return -1;
    return 0;
}
int port_walk(port_walk_ctx* c, const port_type* t, void* obj) { return walk_obj(c, t, obj); }
```

The visited set is linear here on purpose (simple, testable); Task 14 replaces `vset_add` with an open-addressing hash when a profile shows it matters. `F_PTR_ARRAY` with `LEN_NULL_TERM` treats the element as a pointer-sized record whose first word is the pointer (HSD's `Desc**` lists); arrays of pointers-to-structs use element type `{ "ptr", 4, {{F_PTR,0,&T}}, 1 }`.

- [ ] **Step 4: Run the tests** — Expected: `walker_test ... Passed`

- [ ] **Step 5: Commit**

```bash
git add port/src/hsd_endian/schema.h port/src/hsd_endian/walker.[ch] port/tests/walker_test.c port/tests/CMakeLists.txt
git commit -m "port: schema-driven big-endian archive walker with bitfield repacking"
```

### Task 13: Schema generator

**Files:**
- Create: `port/tools/gen_schema.py`, `port/schema/annotations.yml`, `port/schema/roots.yml`
- Test: `port/tests/gen_schema_test.py`, fixture `port/tests/fixtures/schema_fixture.h`

**Interfaces:**
- Consumes: `port/src/hsd_endian/schema.h` layout.
- Produces: `port/src/hsd_endian/schema_tables.c` defining `const port_type port_T_<Name>` for each requested type and `const port_root port_roots[]` (`{ const char* prefix; const port_type* type; }`, null-terminated) from `roots.yml`.

`annotations.yml` format:
```yaml
HSD_JointDesc:            # struct name as in headers
  child:  { ptr: HSD_JointDesc }        # pointer target when the header has void*/forward type
  dobjdesc: { ptr: HSD_DObjDesc }
HSD_TExpDesc:
  __opaque__: true         # whole struct left big-endian (consumed by GX)
Item_DataFileEntry:
  entries: { ptr_array: ItemDesc, len_field: count }
  u:       { union_on: kind, cases: { 0: ItemKindA, 1: ItemKindB } }
HSD_ImageDesc:
  data: { opaque: true }   # texel data stays BE
```

- [ ] **Step 1: Write the failing test**

```python
# port/tests/gen_schema_test.py
import subprocess, sys, pathlib, re
root = pathlib.Path(__file__).resolve().parents[1]
def test_fixture():
    out = subprocess.run([sys.executable, root/"tools/gen_schema.py", "--header", root/"tests/fixtures/schema_fixture.h",
        "--annotations", root/"tests/fixtures/schema_fixture.yml", "--roots", root/"tests/fixtures/schema_roots.yml",
        "--types", "Root,Leaf", "-"], capture_output=True, text=True, check=True).stdout
    assert 'const port_type port_T_Leaf = { "Leaf", 8,' in out
    assert re.search(r'\{F_U16, 0', out) and re.search(r'\{F_F32, 4', out)
    assert re.search(r'\{F_PTR_ARRAY, 4, &port_T_Leaf, LEN_FIELD_U32, 0', out)
    assert re.search(r'\{F_BITS, 12, NULL, 0, 0, 4, bits_Root_12, 3', out)
    assert 'static const uint8_t bits_Root_12[] = { 3, 5, 24 };' in out
    assert '{ "root", &port_T_Root }' in out
if __name__ == "__main__": test_fixture(); print("ok")
```

Fixture header:
```c
/* port/tests/fixtures/schema_fixture.h */
#include <stdint.h>
typedef struct { uint16_t a; uint16_t b; float f; } Leaf;
typedef struct { uint32_t n; Leaf* items; Leaf* shared; uint32_t x : 3; uint32_t y : 5; uint32_t z : 24; } Root;
```
`schema_fixture.yml`: `Root: { items: { ptr_array: Leaf, len_field: n } }`. `schema_roots.yml`: `- { prefix: root, type: Root }`.

Add to `port/tests/CMakeLists.txt`: `add_test(NAME gen_schema_test COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/gen_schema_test.py)`.

- [ ] **Step 2: Run to verify it fails** — Expected: `gen_schema.py` not found.

- [ ] **Step 3: Implement the generator**

```python
#!/usr/bin/env python3
"""Generate port_type tables from C headers via libclang.
Usage: gen_schema.py --header H [--header H2 ...] --annotations A.yml --roots R.yml --types T1,T2 [--clang-arg X ...] OUT|-"""
import argparse, sys, yaml
from clang import cindex
K = cindex.CursorKind; T = cindex.TypeKind
SCALAR = { T.UCHAR:"F_U8", T.CHAR_S:"F_U8", T.SCHAR:"F_U8", T.BOOL:"F_U8", T.USHORT:"F_U16", T.SHORT:"F_U16",
           T.UINT:"F_U32", T.INT:"F_U32", T.ULONG:"F_U32", T.LONG:"F_U32", T.ENUM:"F_U32",
           T.ULONGLONG:"F_U64", T.LONGLONG:"F_U64", T.FLOAT:"F_F32", T.DOUBLE:"F_F64" }
def canon(t):
    while t.kind in (T.TYPEDEF, T.ELABORATED): t = t.get_canonical() if t.kind == T.TYPEDEF else t.get_named_type()
    return t
class Gen:
    def __init__(self, ann, roots):
        self.ann, self.roots, self.decls, self.emitted, self.out, self.order = ann, roots, {}, set(), [], []
    def collect(self, tu):
        for c in tu.cursor.walk_preorder():
            if c.kind == K.STRUCT_DECL and c.is_definition() and c.spelling: self.decls[c.spelling] = c
            if c.kind == K.TYPEDEF_DECL:
                u = canon(c.underlying_typedef_type)
                if u.kind == T.RECORD and u.get_declaration().is_definition(): self.decls[c.spelling] = u.get_declaration()
    def need(self, name):
        if name not in self.emitted: self.emit(name)
        return f"&port_T_{name}"
    def emit(self, name):
        self.emitted.add(name)
        d = self.decls[name]; a = self.ann.get(name, {}); fields = []; bits_defs = []
        if a.get("__opaque__"):
            self.order.append(f'const port_type port_T_{name} = {{ "{name}", {d.type.get_size()}, NULL, 0 }};'); return
        kids = [f for f in d.get_children() if f.kind == K.FIELD_DECL]
        i = 0
        while i < len(kids):
            f = kids[i]; off = f.get_field_offsetof() // 8; fa = a.get(f.spelling, {})
            if f.is_bitfield():
                storage = f.type.get_size(); widths = []; unit_start = off
                while i < len(kids) and kids[i].is_bitfield() and kids[i].get_field_offsetof() // 8 // storage == unit_start // storage:
                    widths.append(kids[i].get_bitfield_width()); i += 1
                bits_defs.append(f"static const uint8_t bits_{name}_{unit_start}[] = {{ {', '.join(map(str, widths))} }};")
                fields.append(f"{{F_BITS, {unit_start}, NULL, 0, 0, {storage}, bits_{name}_{unit_start}, {len(widths)}}}"); continue
            i += 1
            if fa.get("opaque"): fields.append(f"{{F_OPAQUE, {off}}}"); continue
            if "union_on" in fa:
                disc = next(k for k in kids if k.spelling == fa["union_on"]).get_field_offsetof() // 8
                cases = fa["cases"]; vals = ", ".join(str(v) for v in cases); tys = ", ".join(self.need(t) for t in cases.values())
                self.out.append(f"static const uint32_t disc_{name}_{off}[] = {{ {vals} }};")
                self.out.append(f"static const port_type* const disc_t_{name}_{off}[] = {{ {tys} }};")
                fields.append(f"{{F_UNION, {off}, NULL, 0, 0, 0, NULL, 0, {disc}, disc_{name}_{off}, disc_t_{name}_{off}, {len(cases)}}}"); continue
            t = canon(f.type)
            if "ptr_array" in fa:
                lk, lv = self.len_spec(fa, kids)
                fields.append(f"{{F_PTR_ARRAY, {off}, {self.need(fa['ptr_array'])}, {lk}, {lv}}}"); continue
            if t.kind == T.POINTER:
                target = fa.get("ptr")
                if target is None:
                    pt = canon(t.get_pointee())
                    target = pt.get_declaration().spelling if pt.kind == T.RECORD and pt.get_declaration().is_definition() else None
                fields.append(f"{{F_PTR, {off}, {self.need(target) if target else 'NULL'}}}"); continue
            if t.kind == T.CONSTANTARRAY:
                et = canon(t.element_type); n = t.element_count
                if et.kind == T.RECORD: fields.append(f"{{F_ARRAY, {off}, {self.need(et.get_declaration().spelling)}, LEN_CONST, {n}}}")
                elif et.kind == T.POINTER: fields.append(f"{{F_ARRAY, {off}, &port_T___ptr, LEN_CONST, {n}}}"); self.need_ptr()
                elif et.kind in SCALAR and SCALAR[et.kind] != "F_U8":
                    for k in range(n): fields.append(f"{{{SCALAR[et.kind]}, {off + k * et.get_size()}}}")
                continue
            if t.kind == T.RECORD: fields.append(f"{{F_STRUCT, {off}, {self.need(t.get_declaration().spelling)}}}"); continue
            if t.kind in SCALAR:
                if SCALAR[t.kind] != "F_U8": fields.append(f"{{{SCALAR[t.kind]}, {off}}}")
                continue
            raise SystemExit(f"{name}.{f.spelling}: unsupported type {t.spelling}; add an annotation")
        self.out.extend(bits_defs)
        self.out.append(f"static const port_field fields_{name}[] = {{ {', '.join(fields) or '{F_OPAQUE, 0}'} }};")
        self.order.append(f'const port_type port_T_{name} = {{ "{name}", {d.type.get_size()}, fields_{name}, {len(fields)} }};')
    def need_ptr(self):
        if "__ptr" not in self.emitted:
            self.emitted.add("__ptr"); self.out.append("static const port_field fields___ptr[] = { {F_PTR, 0, NULL} };")
            self.order.append('const port_type port_T___ptr = { "ptr", 4, fields___ptr, 1 };')
    def len_spec(self, fa, kids):
        if "len_const" in fa: return "LEN_CONST", fa["len_const"]
        if fa.get("null_term"): return "LEN_NULL_TERM", 0
        k = next(k for k in kids if k.spelling == fa["len_field"]); sz = k.type.get_size()
        return {4: "LEN_FIELD_U32", 2: "LEN_FIELD_U16", 1: "LEN_FIELD_U8"}[sz], k.get_field_offsetof() // 8
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--header", action="append", required=True)
    ap.add_argument("--annotations", required=True); ap.add_argument("--roots", required=True)
    ap.add_argument("--types", required=True); ap.add_argument("--clang-arg", action="append", default=[]); ap.add_argument("out")
    a = ap.parse_args()
    ann = yaml.safe_load(open(a.annotations)) or {}; roots = yaml.safe_load(open(a.roots)) or []
    g = Gen(ann, roots); idx = cindex.Index.create()
    for h in a.header: g.collect(idx.parse(h, args=["-xc", "-std=c99", "-DTARGET_PC", "-DLINT", "--target=wasm32-unknown-emscripten"] + a.clang_arg))
    for t in a.types.split(","): g.need(t)
    for r in roots: g.need(r["type"])
    # forward declarations so mutually recursive types link
    fwd = [f"extern const port_type port_T_{n};" for n in g.emitted]
    body = ['#include "schema.h"', '/* GENERATED by port/tools/gen_schema.py — do not edit */'] + fwd + g.out + g.order
    body.append("const port_root port_roots[] = { " + ", ".join(f'{{ "{r["prefix"]}", &port_T_{r["type"]} }}' for r in roots) + ", { NULL, NULL } };")
    text = "\n".join(body) + "\n"
    (sys.stdout.write(text) if a.out == "-" else open(a.out, "w").write(text))
if __name__ == "__main__": main()
```

Note the forward-declaration line requires `port_type` structs to be non-`static`; `schema.h` declares `typedef struct { const char* prefix; const port_type* type; } port_root; extern const port_root port_roots[];`. The bitfield grouping keys on the storage unit index so a group never spans units (guaranteed by Spike S4). The `--target=wasm32-unknown-emscripten` flag makes libclang compute wasm32 offsets, which is what the walker runs on; `ASSERT_SIZE` then proves those equal the GameCube offsets.

- [ ] **Step 4: Run** — `python3 port/tests/gen_schema_test.py` — Expected: `ok`

- [ ] **Step 5: Commit**

```bash
git add port/tools/gen_schema.py port/schema port/tests/gen_schema_test.py port/tests/fixtures port/tests/CMakeLists.txt
git commit -m "port: libclang schema generator for archive endian conversion"
```

### Task 14: Root dispatch and boot/title coverage

**Files:**
- Modify: `port/src/hsd_endian/archive_swap.c` (implement `port_archive_swap_roots`), `port/schema/roots.yml`, `port/schema/annotations.yml`, `port/CMakeLists.txt` (custom command running `gen_schema.py` over `src/sysdolphin/baselib/*.h` and the `types.h` files into `${CMAKE_BINARY_DIR}/schema_tables.c`)
- Test: `port/tests/archive_roots_test.c` (synthetic); `port/tests/fixtures_iso_test.c` (skipped unless `$MELEE_ISO`)

**Interfaces:**
- Produces: `int port_archive_swap_roots(HSD_Archive*, const uint32_t* reloc_set, uint32_t reloc_count)` — for each public symbol, longest-prefix match in `port_roots`, walk; unknown symbol → `port_log` and, when `PORT_STRICT_SCHEMA` is defined (debug builds), `abort()`.

- [ ] **Step 1: Write the failing synthetic test**

Build an archive as in Task 11 with two public symbols `"leafA"` (matches root prefix `leaf`) and `"mystery"`; check that after `port_archive_swap_roots` the `Leaf` fields are little-endian and that with `strict=0` the unknown symbol only logs (test captures `port_log` via the host stub's `port_log_last` buffer). Register with `port_add_test(archive_roots_test ... walker.c archive_swap.c)` using a hand-written `port_roots[]` in the test file (link without `schema_tables.c`).

- [ ] **Step 2: Run to verify it fails** — Expected: undefined `port_archive_swap_roots`.

- [ ] **Step 3: Implement**

```c
/* appended to archive_swap.c */
#include "walker.h"
#include "../port.h"
#include <sysdolphin/baselib/archive.h>
int port_archive_swap_roots(HSD_Archive* ar, const uint32_t* rs, uint32_t rn) {
    port_walk_ctx c;
#ifdef PORT_STRICT_SCHEMA
    const int strict = 1;
#else
    const int strict = 0;
#endif
    port_walk_ctx_init(&c, ar->data, ar->header.data_size, rs, rn, strict);
    int rc = 0;
    for (uint32_t i = 0; i < ar->header.nb_public; i++) {
        const char* sym = ar->symbols + ar->public_info[i].symbol;
        const port_root* best = NULL; size_t best_len = 0;
        for (const port_root* r = port_roots; r->prefix; r++) {
            size_t n = strlen(r->prefix);
            if (n > best_len && strncmp(sym, r->prefix, n) == 0) { best = r; best_len = n; }
        }
        if (!best) { port_log("hsd_endian: no schema for root symbol '%s' in %s", sym, ar->name ? ar->name : "?"); if (strict) abort(); continue; }
        if (port_walk(&c, best->type, ar->data + ar->public_info[i].offset)) {
            port_log("hsd_endian: strict violation under '%s' (%s)", sym, best->type->name); rc = -1; if (strict) abort();
        }
    }
    port_walk_ctx_free(&c);
    return rc;
}
```

- [ ] **Step 4: Fill `roots.yml` for boot and title**

Start from the symbols the boot path requests (grep `HSD_ArchiveGetPublicAddress`/`lbArchive_LoadSymbols` call sites in `src/melee/lb`, `src/melee/if`, `src/melee/mn`, `src/melee/gm`): e.g. `scene_data` → `HSD_SObjDesc`, `map_head` → `map_head`, `SIS_` → SIS text tables, `ftData` → `ftData`, `itemdata`/`itCommonItems` → item tables, `eff` → effect tables, `grGroundParam` → ground params, `coll_data` → collision data, `TyDisplayTbl`, `ImgBg`, etc. Add annotations as the generator reports unsupported fields. Add a CMake custom command:

```cmake
file(GLOB SCHEMA_HEADERS ${GAME_ROOT}/src/sysdolphin/baselib/*.h ${GAME_ROOT}/src/melee/*/types.h ${GAME_ROOT}/src/melee/*/*.h)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/schema_tables.c
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_schema.py --header ${CMAKE_CURRENT_SOURCE_DIR}/schema/all_headers.h
          --annotations ${CMAKE_CURRENT_SOURCE_DIR}/schema/annotations.yml --roots ${CMAKE_CURRENT_SOURCE_DIR}/schema/roots.yml
          --types "" --clang-arg -I${GAME_ROOT}/src --clang-arg -isystem${CMAKE_CURRENT_SOURCE_DIR}/extern/aurora/include
          ${CMAKE_BINARY_DIR}/schema_tables.c
  DEPENDS ${SCHEMA_HEADERS} schema/annotations.yml schema/roots.yml tools/gen_schema.py)
```
where `schema/all_headers.h` is a hand-maintained umbrella `#include` list.

- [ ] **Step 5: ISO-backed fixture test**

`port/tests/fixtures_iso_test.c`: if `getenv("MELEE_ISO")` is null print `skipped` and exit 0; else read the FST with Task 7, extract `IfAll.usd` and `GmTitle.usd`, run `port_archive_fixup` + `port_archive_swap_roots` in strict mode and assert `rc == 0`. Nothing from the ISO is written to disk.

- [ ] **Step 6: Build the web target, boot, iterate until the title screen renders**

Run the hosted build; in the console, every `hsd_endian: no schema` line is a `roots.yml` entry to add; each strict violation is an annotation to add. Done when the title screen (Nintendo/HAL logos → title) renders with correct geometry and textures. Take a reference screenshot into `port/tests/browser/ref/title.png`.

- [ ] **Step 7: Commit**

```bash
git add port src/sysdolphin/baselib/archive.c
git commit -m "port: root-symbol schema dispatch; boot and title archives convert (M2)"
```

### Task 15: Fixed-address patches and Asyncify yield points

**Files:**
- Modify: `src/melee/lb/lbfile.c:126` and `waitForDisc`, `src/melee/lb/lbmemory.c:68,281`, `port/cmake/emscripten_link.cmake`

- [ ] **Step 1: Patch under `TARGET_PC`**

```c
/* lbfile.c */
#ifdef TARGET_PC
    type = port_is_aram_address(dst) ? 0x23 : 0x21;
#else
    type = (dst >= 0x80000000) ? 0x21 : 0x23;
#endif
```
`port_is_aram_address(uintptr_t)` in `port/src/os_shim/os_shim.c` compares against `ARGetStorageAddress()` and `ARGetSize()`. In `waitForDisc()`'s spin loop add `#ifdef TARGET_PC port_yield(); #endif`. In `lbmemory.c`, replace the two `< 0x80000000U` checks with `#ifdef TARGET_PC (0) #else ... #endif` so the assertions are skipped.

- [ ] **Step 2: Constrain Asyncify**

Add to `PORT_LINK_FLAGS`: `-sASYNCIFY_IMPORTS=emscripten_sleep` and `-sASYNCIFY_REMOVE=@${CMAKE_CURRENT_SOURCE_DIR}/cmake/asyncify_remove.txt` listing Aurora's `aurora::gx` internal symbols (`*aurora::gfx*`, `*aurora::gx*`, `*wgpu*`) so the render path is not instrumented. Build with `-sASYNCIFY_ADVISE=1` once, save the advice output under `port/docs/asyncify-advice.txt`, and record wasm size before/after in `spikes.md` (S2).

- [ ] **Step 3: Verify**

Boot to title from a cold page load; no `RuntimeError: unreachable` or "invalid state" Asyncify errors in the console during the boot loads.

- [ ] **Step 4: Commit**

```bash
git add src/melee/lb/lbfile.c src/melee/lb/lbmemory.c port
git commit -m "port: ARAM address test, arena checks, and Asyncify yield tuning"
```

---

## Milestone M3 — Menus, character select, first sound

### Task 16: Playwright smoke test for boot-to-title

**Files:**
- Create: `port/tests/browser/package.json`, `port/tests/browser/playwright.config.ts`, `port/tests/browser/boot.spec.ts`

- [ ] **Step 1: Write the test**

```ts
import { test, expect } from '@playwright/test';
const iso = process.env.MELEE_ISO;
test.skip(!iso, 'MELEE_ISO not set');
test('boots to title', async ({ page }) => {
  const logs: string[] = []; page.on('console', m => logs.push(m.text()));
  await page.goto('/index.html');
  await page.setInputFiles('input[type=file]', iso!);
  await expect(page.locator('canvas')).toBeVisible();
  await page.waitForFunction(() => (window as any).meleeState === 'title', null, { timeout: 120_000 });
  expect(logs.filter(l => l.includes('no schema'))).toEqual([]);
  await expect(page).toHaveScreenshot('title.png', { maxDiffPixelRatio: 0.02 });
});
```
`boot.js` sets `window.meleeState = 'title'` when the wasm calls the exported `port_notify_state("title")` from the title scene's init (under `TARGET_PC` in `src/melee/gm/gm_1601.c` or wherever the title scene starts — find it via `grep -n Title src/melee/gm/*.c`).

- [ ] **Step 2: Run** — `cd port/tests/browser && npm i && MELEE_ISO=/path/to/GALE01.iso npx playwright test --project=chromium` with Chromium launched with `--enable-unsafe-webgpu --enable-features=Vulkan` on Linux. Expected: 1 passed.

- [ ] **Step 3: Commit**

### Task 17: Menu, CSS and SIS text coverage; gamepad input

**Files:**
- Modify: `port/schema/roots.yml`, `port/schema/annotations.yml`; `port/cmake/game_sources.cmake` (re-enable `sislib_font.c`, `hsd_3915.c`); Aurora patch for `PADRead`/`PADClamp` behaviours Melee expects
- Create: `port/src/assets/sis_font.c` — loads the two dtk-extracted atlases (`HSD_SisLib_FontAtlas`, `HSD_DebugFontAtlas`) from the DOL inside the ISO (`DVDGetDOLLocation` equivalent: read `boot.bin`'s DOL offset at `0x420`, then the DOL section containing the atlas address from `config/GALE01/symbols.txt`)

Acceptance: from the title, press Start on gamepad 1 → main menu → VS Mode → character select; all four ports show controllers when connected; menu text renders; no `no schema` logs. Playwright: extend `boot.spec.ts` with a `menu.spec.ts` that presses Start via `page.keyboard` (SDL keyboard-as-gamepad mapping documented in `port/README.md`).

### Task 18: `.sem`/`.ssm` loading and the AX mixer skeleton

**Files:**
- Create: `port/src/hsd_endian/formats.c` (`port_swap_sem`, `port_swap_ssm_header`), `port/src/ax_hle/ax_hle.h`, `ax_voice.c` (ADPCM/PCM decode + SRC), `ax_mix.c` (bus mixing, 5 ms frames), `ax_dsp.c` (replaces `ax_stub.c`: `DSP*`/`AI*` entry points, drives `ax_mix` on a 5 ms accumulator from `port_frame`), `port/web/js/audio_sink.js`, `port/web/js/audio_worklet.js`
- Test: `port/tests/ax_adpcm_test.c` (decode a known 16-byte ADPCM frame with a known coefficient set against expected PCM), `port/tests/ax_mix_test.c` (one voice at unity gain into the main bus produces its samples; two voices sum; a voice with `AX_PB_STATE_STOP` contributes zero)

Acceptance: menu cursor sounds play; music (`.hps` via `axdriver.c` streaming) plays on the main menu; no audible gaps at 60 fps in Chrome. The `.hps` stream path uses `DVDReadAsyncPrio` + ARAM staging already implemented; verify `lbAudioAx` requests land in the read cache.

Where a `.ssm` file's sample data is big-endian ADPCM, `ax_voice.c` reads the nibbles directly (ADPCM is a byte stream; no swap), and the 16-bit coefficient table in the `.ssm` header is swapped by `port_swap_ssm_header`.

---

## Milestones M4–M7 (task list; expand each into steps when M3 passes)

### M4 — A VS match

- **Task 19** Stage archives: `map_head`, `coll_data`, `grGroundParam` + per-stage roots; Final Destination renders; collision works (character stands).
- **Task 20** Fighter archives: `ftData*` (attributes, subaction table, hitboxes, model/animation joints); Mario vs Mario on FD; all `ASSERT_SIZE`d fighter structs verified against wasm32 layout.
- **Task 21** Items and effects: `itemdata`, `eff*`; Effects render; items spawn.
- **Task 22** THP disabled path: `lbmthp.c`/`lb_01F8.c` compiled with the SDK `THPDec.c` C fallback and Aurora `AURORA_ENABLE_THP`; movies skipped if decode fails (log, continue).
- **Task 23** Frame pacing: fixed-step accumulator on `requestAnimationFrame`, pad-queue depth 1–2, `HSD_PadRenewMasterStatus` cadence matched to 60 Hz on 120 Hz displays; Playwright `match.spec.ts` runs a 30-second match via scripted inputs and checks no `HSD_ASSERT` output.

### M5 — Saves and polish

- **Task 24** CARD: `memcard.raw` in MEMFS, IDBFS mount + `FS.syncfs` after every `CARDWrite`, `SaveStore` export/import buttons; Playwright: create a save, reload, save still present.
- **Task 25** Error panel: WebGPU missing, wrong disc, read failure, save failure — each a distinct message with a test.
- **Task 26** Audio latency tuning: worklet ring buffer 3× 5 ms frames; measure underruns via a counter exposed on `window`.
- **Task 27** Asyncify list tuning with `ASYNCIFY_ADVISE`; release build size budget recorded; optional `web-jspi` preset added if `WebAssembly.Suspending` exists in the target browsers.

### M6 — Single-file build

- **Task 28** `pack_single_html.py`: reads `build/web-release/index.html`, inlines `melee.js` (built with `-sSINGLE_FILE`), `boot.js`, `disc_source.js`, `save_store.js`, `audio_sink.js` and the worklet source (as a string wrapped in a Blob URL at runtime), CSS; fails if the regex `<script\s+src=|<link\s+href=|fetch\(` matches the output. Unit test in `port/tests/pack_test.py` with a small fixture page.
- **Task 29** `file://` validation on Chrome, Firefox, Safari using the S3 decision table; README section "Offline file".
- **Task 30** CI: GitHub workflow `port.yml` builds `web-release`, runs host tests, packs the offline file, uploads both as artifacts.

### M7 — Coverage

- **Task 31** Every character/costume loads (script iterates the CSS via scripted input; asserts no `no schema` and no strict violations).
- **Task 32** Every stage loads (same technique).
- **Task 33** Trophies, 1P modes, Event Match, Sound Test, movies.
- **Task 34** `port/docs/milestones.md` checklist all green; tag `web-v0.1`.

---

## Self-review

**Spec coverage.** D1 → Tasks 3, 9. D2 → Tasks 11–14, 18. D3 → Tasks 10, 15, 27. D4 → Task 10, 23. D5 → Tasks 7, 8, 10. D6 → Task 18, 26. D7 → Task 24. D8 → Tasks 28–30. §8 error handling → Task 25 (+ strict schema in 14). §10 testing → harness Task 2, schema tests 12–14, ax tests 18, Playwright 16/23/24, CI 30. §11 spikes → S1 Task 3, S3/S4 Task 4, S2 Task 10/15, S5 Task 14 step 5, S6 Task 10 step 6. Gap found and fixed: the SIS font atlas that the `.nix` build excluded now has a home (Task 17).

**Placeholders.** None of the banned phrases remain; M4–M7 tasks state files, acceptance criteria and tests but are deliberately at task granularity per the spec's §13.

**Type consistency.** `port_disc_read(offset, length, dst, cb, user)` is identical in Task 8's header, its test, and Task 10's `imports.js`; `port_dvd_pump`/`port_yield`/`port_alarm_tick` names match across Tasks 6, 8, 10, 15; `port_archive_fixup` and `port_archive_swap_roots` match across Tasks 11 and 14; walker enum and struct field order in Task 12's `schema.h` match the initialiser order the Task 13 generator emits (`{kind, offset, type, len_kind, len, bits_storage, bit_widths, nbits, disc_offset, disc_values, disc_types, ncases}`).
