Melee Web
=============

Super Smash Bros. Melee running in a web browser, built from the
[doldecomp/melee](https://github.com/doldecomp/melee) decompilation and
compiled to WebAssembly.

This repository is a fork. The game code is the decompilation project's work;
this fork adds a port layer (`port/`) that runs it in the browser (tested in
Chrome and Firefox), either from a web server or from a single offline HTML
file.

> [!IMPORTANT]
> Nothing from the game is included. You need your own disc image of
> Super Smash Bros. Melee, NTSC-U version 1.02 (game ID `GALE01`). The page
> reads it from your computer; it is never uploaded anywhere.

It builds:

|Output|What it is
-|-
`port/build/web-release/`|A hosted build: a static folder any web server can serve
`port/melee-offline.html`|One self-contained HTML file that runs from `file://`, with nothing beside it

Both play the same disc:

|Version|Game ID|Disc image
-|-|-
1.02|`GALE01`|A plain `.iso` or `.gcm` (an NKit `.iso` works too)

# Dependencies

**To play**, you only need a current Chrome or Firefox and your disc image.
Everything below is for building the port yourself.

The port is built and tested on **Linux**. The build scripts are Bash; Windows
(through WSL) and macOS have not been tried.

- [Git](https://git-scm.com/), [CMake](https://cmake.org/) 3.25 or newer,
  [Ninja](https://ninja-build.org/) and Python 3.
- The Python packages `libclang` (the schema generator reads the game's headers
  with it) and `pyyaml`:
  ```sh
  python -m venv --upgrade-deps .venv
  . .venv/bin/activate
  pip install libclang pyyaml
  ```
- [Rust](https://rustup.rs/) with the WebAssembly target, for the shader
  translator the WebGL2 renderer uses:
  ```sh
  rustup target add wasm32-unknown-unknown
  ```
- Emscripten is **not** a separate install: `port/tools/setup.sh` downloads the
  pinned version (emsdk 6.0.9) into `port/extern`.

Not needed to build or play: [Node.js](https://nodejs.org/) and
[Playwright](https://playwright.dev/) (`playwright-core`) run the automated
browser tests. They are for development, and for AI coding agents that test and
debug the game by driving a browser (see [Testing](#testing)). The host unit
tests also need a native C/C++ compiler (GCC or Clang).

# Building

- Clone the repository:
  ```sh
  git clone https://github.com/VibingSticks/melee-web.git
  cd melee-web
  ```
- Install Emscripten and Aurora (the GameCube API layer) at their pinned
  versions, with the port's patches applied. This only needs doing once:
  ```sh
  port/tools/setup.sh
  ```
- Build the shader translator (writes `port/web/js/naga/naga.wasm`):
  ```sh
  port/tools/build_naga.sh
  ```
- Put Emscripten on your `PATH` (in every new shell):
  ```sh
  source port/tools/env.sh
  ```
- Build and serve the hosted version:
  ```sh
  cd port
  cmake --preset web-release
  cmake --build --preset web-release
  python3 -m http.server -d build/web-release 8080
  ```
  Then open <http://localhost:8080>.
- Or build the single offline file:
  ```sh
  cd port
  cmake --preset web-single
  cmake --build --preset web-single
  python3 tools/pack_single_html.py build/web-single -o melee-offline.html
  ```
  `melee-offline.html` can then be copied anywhere (a USB stick works) and
  opened directly.

The `web-debug` preset builds with debug information for development.
More detail on the port's internals is in [`port/README.md`](../port/README.md).

# Playing

1. Open the page in a current Chrome or Firefox.
2. Pick your disc image.
3. A loading bar compiles the game's graphics shaders for at most 6 seconds,
   starting with the ones the title screen, menus and a first match need,
   then the game starts. On WebGPU the rest compile in the background, and
   the toolbar counts how many are left. On WebGL2, where compiling during
   play would make the game stutter, each remaining shader compiles the
   first time a frame needs it. Every session adds the shaders it met to the
   list the loading bar compiles next time.

The toolbar has **Export save** and **Import save** for the memory card, and
**Save log**, which downloads a report with frame timings for bug reports.
**Tab** hides the toolbar and log.

## Keyboard (controller port 1)

| Key | Button | | Key | Button |
|---|---|---|---|---|
| Arrow keys | Control stick | | X | A |
| I J K L | C-stick | | Z | B |
| Enter | Start | | C | X |
| Q / W | L / R | | S | Y |
| D | Z | | T F G H | D-pad |

## Renderers

- **WebGPU** is used when the browser has it: Chrome, and Firefox where its
  WebGPU is switched on (see below).
- **WebGL2** is the fallback, for example Firefox on Linux with its default
  settings. It uses a small WebGPU-on-WebGL2 layer in `port/web/js/gpu-gl2.js`.

**Auto** picks WebGPU when it is available. To choose one yourself, use the
**Renderer** menu in the toolbar before picking the disc: changing it reloads
the page, and the choice is remembered. The menu locks once the game starts.
WebGPU is the better choice where it works, including on Chromebooks.

Firefox on Linux ships WebGPU switched off. To use it, open `about:config`,
set `dom.webgpu.enabled` to `true`, and reload the page. Firefox's WebGPU
lacks a shader feature and limits how much buffer memory can be mapped, so
the port reads vertex data from textures there and uploads each frame with
`writeBuffer`; it runs at full speed on a desktop.

## URL options

Add these to the address, for example `melee-offline.html?res=640x480`.

| Option | Effect |
|---|---|
| `renderer=webgl2` / `renderer=webgpu` | Force a renderer |
| `res=WxH` | Render size (default 1280x960; 640x480 on small machines) |
| `frameskip=off` | Draw every frame, even if that means slow motion on a slow machine |
| `preload=off` | Start without compiling the shaders first |
| `preload=all` | Wait for every shader before starting |
| `preload=N` | Wait at most N seconds for shaders before starting (default 6) |
| `fx=off` | Play audio without the game's reverb and echo |
| `audio=sdl` | Use SDL's audio output instead of the AudioWorklet |

# Testing

- Host unit tests (the disc-data converter, the disc layer and the offline
  packer, among others):
  ```sh
  cd port
  cmake --preset host-tests
  cmake --build --preset host-tests
  ctest --preset host-tests
  ```
- Automated browser tests live in `port/tests/browser`. They drive the game
  through Playwright: boot it, press buttons, read its state and screenshots.
  They are meant for development and for AI coding agents debugging the port;
  players and people who only build it can skip them. Point `NODE_PATH` at a
  `node_modules` that has `playwright-core`, for example:
  ```sh
  NODE_PATH=/path/to/node_modules node port/tests/browser/boot_probe.mjs port/build/web-release /path/to/GALE01.iso 20 --headed
  ```

# Status

The whole game is compiled in: every unit of the decompilation except three
debug-only files. Tested and running: the menus, VS matches, Classic mode
through to Master Hand (bonus stages included), trophies, the intro movie,
memory card saves, and music and sound effects with the game's reverb and
echo. The other modes are compiled in but have had less play-testing.

What the port adds on top of the decompilation:

- **Graphics** through Aurora, a reimplementation of the GameCube's graphics
  API, on WebGPU or WebGL2.
- **Disc, controllers, memory card, timers and video sync** reimplemented for
  the browser.
- **Audio**: a software version of the GameCube's sound chip, played through
  an AudioWorklet, with the SDK's own reverb and delay code.
- **Game data**: the disc's files are big-endian GameCube data, converted as
  they load.
- **Movies**: decoded with a portable JPEG decoder in place of the original
  assembly.

The game was written for the GameCube's CPU, which tolerates things the
browser does not (a divide by zero, a function called with the wrong number
of arguments). Those turn up as crashes and get fixed one at a time; a report
from the **Save log** button is the most useful thing to send.

Known issues:

- A one-pixel line can appear beside the header in the Data records screens
  on the WebGL2 renderer at 1280x960.
- Resampling is linear; the GameCube used a filter whose coefficients live in
  its sound chip, not on the disc.
- Slow machines (for example a 2-core Chromebook) draw around 20 frames a
  second. Frame skip keeps the game itself at full speed.

# Branches

- `master`: the fork's release branch.
- `web-port`: development.
- Changes from [doldecomp/melee](https://github.com/doldecomp/melee) are merged
  in from time to time.

# Credits

This port stands on the work of these projects:

| Project | What it does here | Version used |
|---|---|---|
| [doldecomp/melee](https://github.com/doldecomp/melee) | The decompiled game: all of the game code, HAL's engine library and the Dolphin SDK sources (including the AXFX reverb and delay and the THP movie code) | merged regularly |
| [Aurora](https://github.com/encounter/aurora) | The GameCube API layer: GX graphics on WebGPU, controllers, video, memory card, ARAM, matrices and part of the OS | the [r-burns fork](https://github.com/r-burns/aurora) at `e6a6f02`, plus the patches in `port/extern/aurora-patches` |
| [Emscripten](https://emscripten.org/) | The C/C++ to WebAssembly compiler and browser runtime | emsdk 6.0.9 |
| [Dawn](https://dawn.googlesource.com/dawn) (emdawnwebgpu) | The WebGPU C API on top of the browser's WebGPU | remote port v20260910.214722 |
| [SDL](https://github.com/libsdl-org/SDL) | The window and keyboard input, and the fallback audio output | Emscripten's SDL3 port (3.4.2) |
| [naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga) (wgpu) | Translates the shaders from WGSL to GLSL for the WebGL2 renderer | naga 30 |
| [Abseil](https://github.com/abseil/abseil-cpp), [{fmt}](https://github.com/fmtlib/fmt), [xxHash](https://github.com/Cyan4973/xxHash), [FreeType](https://freetype.org/), [zlib](https://zlib.net/), [libpng](http://www.libpng.org/pub/png/libpng.html), [SQLite](https://sqlite.org/), [Dear ImGui](https://github.com/ocornut/imgui), [Tracy](https://github.com/wolfpld/tracy) | Libraries Aurora builds with | as pinned by Aurora |
| [LLVM / libclang](https://clang.llvm.org/) and [PyYAML](https://pyyaml.org/) | Read the game's C headers to generate the tables that convert disc data | pip packages |
| [Playwright](https://playwright.dev/) | Drives the browser for the automated tests (development and AI-agent debugging, not needed to play or build) | `playwright-core` |
| [Dolphin](https://dolphin-emu.org/) | Reference for how the game should look and behave, and for GameCube hardware details | Dolphin 2606 |

The port was developed with [Claude Code](https://claude.com/claude-code),
Anthropic's AI coding assistant.

Super Smash Bros. Melee is a trademark of Nintendo. This project is not
affiliated with or endorsed by Nintendo or HAL Laboratory.

# FAQ

## Why does it need my disc?

The repository contains code, not game data. The models, textures, music and
stages all come from your disc image, read in the browser as the game needs
them.

## Where are my saves?

In the browser's storage for the page (IndexedDB). Clearing the site's data
removes them, so use **Export save** to keep a copy; **Import save** brings one
back, on the same or another computer.

## It is slow on my computer

Try `?res=640x480`, which gives the graphics a quarter of the pixels to draw.
On a slow machine frame skip keeps the game itself at full speed and draws
fewer frames. The first boot's shader compile is the slowest part; later boots
are faster.

---

# The decompilation (upstream README)

Everything below is the upstream project's README, kept for the
decompilation itself: building the matching `main.dol`, tooling and the code
layout.

Super Smash Bros Melee \
[![Build Status]][actions]
[![Discord Badge]][discord]
[![Linked Progress]][progress]
=============

[actions]: https://github.com/doldecomp/melee/actions/workflows/build.yml
[discord]: https://discord.gg/hKx3FJJgrV
[progress]: https://decomp.dev/doldecomp/melee

[Build Status]: https://github.com/doldecomp/melee/actions/workflows/build.yml/badge.svg
[Linked Progress]: https://decomp.dev/doldecomp/melee.svg?mode=shield&measure=complete_code&label=linked&category=all
[Discord Badge]: https://img.shields.io/discord/727908905392275526?color=%237289DA&logo=discord&logoColor=%23FFFFFF

This repo contains a matching decompilation of Super Smash Bros Melee (US).

> [!TIP]
> The DOL this repository builds can be shifted! Meaning you are able to now add and remove code as you see fit, for modding or research purposes.

It builds `main.dol`:

|Version|Game ID|SHA-1
-|-|-
1.02|`GALE01`|`08e0bf20134dfcb260699671004527b2d6bb1a45`

# Dependencies

## Windows:
On Windows, it's **highly recommended** to use native tooling. WSL or msys2 are **not** required.
When running under WSL, [objdiff](#diffing) is unable to get filesystem notifications for automatic rebuilds.

- Install [Python](https://www.python.org/downloads/) and add it to `%PATH%`.
  - Also available from the [Windows Store](https://apps.microsoft.com/store/detail/python-311/9NRWMJP3717K).
- Download [ninja](https://github.com/ninja-build/ninja/releases) and add it to `%PATH%`.
  - Quick install via pip: `pip install ninja`

## macOS:
- Install [ninja](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages):
  ```
  brew install ninja
  ```
- Install [wine-crossover](https://github.com/Gcenx/homebrew-wine):
  ```
  brew install --cask --no-quarantine gcenx/wine/wine-crossover
  ```

After OS upgrades, if macOS complains about `Wine Crossover.app` being unverified, you can unquarantine it using:
```sh
sudo xattr -rd com.apple.quarantine '/Applications/Wine Crossover.app'
```

## Linux:
- Install [ninja](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages).
- For non-x86(_64) platforms: Install wine from your package manager.
  - For x86(_64), [WiBo](https://github.com/decompals/WiBo), a minimal 32-bit Windows binary wrapper, will be automatically downloaded and used.

# Building
- Clone the repository:
  ```
  git clone https://github.com/doldecomp/melee.git --depth=1
  ```
- Using [Dolphin Emulator](https://dolphin-emu.org/), find your ISO and click `Properties`. Go to the `Filesystem` tab, right-click `Disc - GALE01` and select `Extract System Data`. Choose `orig/GALE01` of this repository.
  - To save space, only `main.dol` (and `.gitkeep`) are necessary. Other files can be deleted.
  ![](assets/dolphin-extract.png)
- Configure:
  ```
  python configure.py
  ```
- Build:
  ```
  ninja
  ```

# Tooling

We use Python for our command line tooling. It is recommended that you use a [virtual environment](https://docs.python.org/3/library/venv.html).

1. Create a virtual environment.
    ```sh
    python -m venv --upgrade-deps '.venv'
    ```
1. You'll need to activate it whenever you open a new shell.
    * Windows:
        ```ps1
        .venv/Scripts/Activate.ps1
        ```
    * Linux/macOS:
        ```ps1
        . .venv/bin/activate
        ```
1. After that, you can install or update our packages with:
    ```sh
    pip install -r reqs/decomp.txt
    ```
1. Now you can run `decomp.py` to decomp a function using [m2c](https://github.com/matt-kempster/m2c). Pass it `-h` to see all the options.
    ```sh
    python tools/decomp.py my_function_name
    ```

# Modding
1. Dump the full game disc to a folder as described under [Building](#building), not just the system files.
1. After cloning the repository, you can freely add new source files/folders under `/src`.
1. Enable the non-matching build by running:
   ```
   python configure.py --non-matching
   ```
1. Add each of your source files to `configure.py`. The order determines when your files are linked, but in most cases does not matter. You can create a new `MeleeLib` definition, but you don't have to. Make sure any newly created files are marked `Equivalent`.
   ```py
       MeleeLib(
           "My Custom Library",
           [
               Object(Equivalent, "my-cool-mod/helloworld.c"),
           ],
       ),
    ```
1. Run `ninja` to build the game.
1. Move the build DOL from `build/GALE01/main.dol` to the `sys` folder of the game directory you created.
1. Make sure your game directory is configured under Paths in Dolphin, then launch your `main.dol` from the Games list.

# Containers
We use [nix](https://nixos.org/) for [most of our tontinuous integration](https://github.com/doldecomp/melee/blob/1ddf751b718f86933ca93a022f93f23f823a6744/.github/workflows/build.yml#L146-L265), which can be containerized under [nixos/nix](https://hub.docker.com/r/nixos/nix/) or [nix-toolbox](https://thrix.github.io/nix-toolbox/). We plan on fully migrating our CI to nix; see [issue #1368](https://github.com/doldecomp/melee/issues/1368).

# Diffing

Once the initial build succeeds, an `objdiff.json` should exist in the project root.

Download the latest release from [encounter/objdiff](https://github.com/encounter/objdiff). Under project settings, set `Project directory`. The configuration should be loaded automatically.

Select an object from the left sidebar to begin diffing. Changes to the project will rebuild automatically: changes to source files, headers, `configure.py`, `splits.txt` or `symbols.txt`.

![](assets/objdiff.png)

# Contributing

Contributions are welcome! If you're new to decomp, check out our [Getting Started guide](https://doldecomp.github.io/melee/getting_started.html). Before [opening a pull request](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/creating-a-pull-request), please read our [contributing guidelines](CONTRIBUTING.md). If you're new to Git and don't know how to create a pull request, we encourage you to [create an issue](https://github.com/doldecomp/melee/issues/new) with your decomp.me link and a maintainer will add your code to the repository.

Most of our efforts now are directed to naming and cleanup. See our [todo list](https://doldecomp.github.io/melee/todo.html) and [cleanup index](https://doldecomp.github.io/melee/cleanup/).

We're also happy to answer any questions in the `#smash-bros-melee` channel on Discord.

[![Gamecube/Wii Decompilation Discord](https://discordapp.com/api/guilds/727908905392275526/widget.png?style=banner2)](https://discord.gg/hKx3FJJgrV)

# FAQ
## How is the codebase structured?

The code in `src` is divided into several modules, the main one being `melee`, which is the game code.

### `melee`
The main game code is divided into several two-letter folders, which were left behind by HAL in assert messages and game data on the original disc.

Short|Full|Notes
-|-|-
`cm`|Camera|
`db`|Debug|
`ef`|Effect|Visual effects.
`ft`|Fighter|The player characters.
`gm`|Game|The main game loop.
`gr`|Ground|Stages and other levels.
`if`|Interface|User interface.
`it`|Items|
`lb`|Library|Utility functions that are often thin wrappers around `dolphin` or `baselib` code.
`mn`|Menu|
`mp`|Map|Related to stages and contains things like `mpcoll` (map collisions).
`pl`|Player|As in users.
`sc`|Scene|Menu, versus mode, single-player, etc. The game mode.
`ty`|Toy|Trophies.
`vi`|Visual|Cutscenes, etc.

#### `melee/ft/kinds`

HAL also used two-letter abbreviations for each fighter.

Short|Full|Canonical English
-|-|-
`Bo`|Zako<sup>1</sup> Boy|[Male wire frame](https://www.ssbwiki.com/Fighting_Wire_Frames#Male_Wire_Frame.2FCaptain_Falcon)
`Ca`|Captain|Captain Falcon
`Ch`|Crazy Hand|
`Cl`|Child Link|Young Link
`Co`|Common|Shared code
`Dk`|Donkey Kong|
`Dr`|Dr. Mario|
`Fc`|Falco|
`Fe`|Fire Emblem|Roy
`Fx`|Fox|
`Gk`|Giga Koopa|Giga Bowser
`Gl`|Zako Girl|[Female wire frame](https://www.ssbwiki.com/Fighting_Wire_Frames#Female_Wire_Frame.2FZelda)
`Gn`|Ganondorf|
`Gw`|Mr. Game & Watch|
`Kb`|Kirby|
`Kp`|Koopa|Bowser
`Lg`|Luigi|
`Lk`|Link|
`Mh`|Master Hand|
`Mr`|Mario|
`Ms`|Mars|Marth
`Mt`|Mewtwo|
`Nn`|Nana|
`Ns`|Ness|
`Pc`|Pichu|
`Pe`|Peach|
`Pk`|Pikachu|
`Pp`|Popo|
`Pr`|Purin|Jigglypuff
`Sb`|Sandbag|
`Sk`|Seak|Sheik
`Ss`|Samus|
`Ys`|Yoshi|
`Zd`|Zelda|

<sup>1</sup> Zako (雑魚) is Japanese for "trash mob" in video games, literally "small fish."

### `sysdolphin/baselib`

HAL's core internal library.
Class|Full
-|-
`AObj`|Animation
`CObj`|Camera
`DObj`|Draw/Display
`FObj`|Frame
`GObj`|Global/Game
`JObj`|Joint
`LObj`|Light
`MObj`|Material
`PObj`|Polygon
`TObj`|Texture
`RObj`|Reference
`SObj`|Scene
`WObj`|World

### `dolphin`

The [Dolphin SDK](https://wiki.raregamingdump.ca/index.php/Dolphin_SDK).

### `MetroTRK`

The Metrowerks Target Resident Kernel.

### `MSL`

The Metrowerks Standard Library.

### `Runtime`

The Gekko hardware runtime.

## What can be done now that the game is fully decompiled?

See our [FAQ](https://github.com/doldecomp/melee/wiki/FAQ).
