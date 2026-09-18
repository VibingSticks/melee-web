# Milestone checklist

Manual acceptance list from spec §10. Tick with date and browser.

- [x] M1 Link: wasm links; first `OSReport` appears in the console (2026-09-11, Chrome 151, synthetic disc)
- [x] M2 Title: title screen renders with correct textures (2026-09-12, Chrome 151, real GALE01 disc; the intro movie is skipped until the THP decoder is ported)
- [~] M3 Menus: main menu, VS mode, character select (2026-09-12, Chrome 151, real GALE01 disc).
      Keyboard and gamepad work on port 1. Every screen from the memory-card
      notice through the title, main menu and character select renders with
      readable text, and the character-icon grid shows all portraits and name
      plates. Outstanding: only port 1 is usable (the player-type toggle does
      not respond).

      Two things this entry used to blame were wrong, for the record. Text was
      blank because no font data was compiled in at all -- hsd_3915.c and
      sislib_font.c were excluded for want of a generated .inc -- and because
      hsd_3A76.c read the big-endian SIS glyph stream with a native u16 load.
      The icon grid was blank not because "texture animations do not select a
      portrait" (they do) but because PObjs sharing one HSD_VtxDescList each
      re-registered the shared vertex array at their own size; see
      setupArrayDesc in pobj.c.
- [~] M4 Match: two humans, Final Destination, full match with audio (2026-09-12,
      Chrome 151, real GALE01 disc). `Module._port_debug_start_vs()` enters
      Melee's own debug VS mode and the match runs: the stage, both fighters,
      the HUD and particle effects all render, inputs move the fighters, and a
      64-second run (3840 frames) and five back-to-back scene loads finish with
      no assertion, no trap and a flat heap. Outstanding: a match has not yet
      been played through to a result screen.

      Audio (2026-09-12): the AX mixer is on by default. Menu and match music
      stream from the .hps files and SFX play through the .sem macro driver;
      the mixer reports non-silent PCM and the SDL queue drains. Three
      byte-order bugs had kept the synth engine silent or wedged: the .ssm
      voice blocks were swapped on the wrong layout (loopFlag/format
      exchanged, so every SFX voice stopped at once), the .hps block header's
      "next" offset was used raw (a DVD read at 0xa0000100 that never
      completed left HSD's DevCom busy forever, which is what hung the
      nr_vs.ssm load), and the .sem macro bytecode was never swapped, so no
      macro reached its play command. The game's u32 reads of the DSP
      parameter block's hi/lo u16 pairs go through PB_GET32/PB_SET32 now.
      Not mixed: the aux busses (reverb, chorus, delay) and ITD.
- [~] M5 Saves: save created, page reloaded, save present (2026-09-17, Chrome,
      real GALE01 disc). Aurora's CARD implementation keeps the card image on
      the filesystem, which under Emscripten is MEMFS and is discarded with the
      tab; the card directory is now an IDBFS mount instead (port/src/save_web.c).
      The stored image is read in before aurora_initialize, because Aurora opens
      the card during init and formats a blank one over it otherwise, and it is
      written back on a debounce after the game writes.
      `tests/browser/save_persist_test.mjs` boots, waits for the store, reloads
      in the same browser context and checks Aurora loads the image rather than
      formatting. Outstanding: the export/import round-trip.
- [~] M6 Single-file: `melee-offline.html` boots from `file://` on Chrome, Firefox, Safari
      (2026-09-12: Chrome 151 on WebGPU and Firefox on the WebGL2 fallback both
      boot the 14 MB packed file straight from `file://`, take a disc through the
      picker and run a match. `tools/pack_single_html.py` refuses to write a page
      that still names a second file. Safari is untested -- no macOS here.)
- [ ] M7 Coverage: every character, stage, mode; movies; trophies

## Excluded units

Game translation units currently excluded from `port/cmake/game_sources.cmake`,
with the reason. Every entry here is a to-do. As of 2026-09-11 every other unit
(978) compiles for wasm32 with the layout asserts enabled.

| Unit | Reason | Re-enabled by |
|---|---|---|
| `dberror.c`, `debug.c`, `debugconsole_main.c` | PPC register dumps / debug console thread | never (debug-only) |
| `hsd_3915.c`, `sislib_font.c` | need font atlases extracted from the DOL | plan Task 17 |

## Data the schema does not cover yet

Every root symbol the boot, menu, character-select and stage paths request now
has a schema (`port/schema/roots.yml`); `port/tools/archive_coverage.c` reports
the rest. Arrays whose length lives in the code rather than the archive are
converted where the game reads them (`port/src/hsd_endian/formats.c`): the
`.ssm` and `.sem` audio headers, the particle command and texture banks, and
the fighter animation tables. Known gaps: `lbRefData`'s float array,
`ALDYakuAll`, `yakumono_param`, `itPublicData` and the trophy tables are left
big-endian behind relocated pointers.

All other units, including `lb_01F8.c`, `lbmthp.c`, `fog.c`, `pobj.c`, `video.c`, `lb_0195.c`, `lbcardnew.c` and `gmmain.c`, now build (THP decoding itself is stubbed until Task 22).
