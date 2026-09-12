# Milestone checklist

Manual acceptance list from spec §10. Tick with date and browser.

- [x] M1 Link: wasm links; first `OSReport` appears in the console (2026-09-11, Chrome 151, synthetic disc)
- [x] M2 Title: title screen renders with correct textures (2026-09-12, Chrome 151, real GALE01 disc; the intro movie is skipped until the THP decoder is ported)
- [~] M3 Menus: main menu, VS mode, character select (2026-09-12, Chrome 151, real GALE01 disc). Keyboard and gamepad work on port 1;
      characters can be picked and the panel portrait renders. Outstanding: the
      character-icon grid draws blank (its texture animations do not select a
      portrait), only port 1 is usable (the player-type toggle does not respond),
      and there is no audio.
- [ ] M4 Match: two humans, Final Destination, full match with audio
      *(2026-09-12: `Module._port_debug_start_vs()` reaches Melee's own debug VS
      mode; the stage and both fighters load and build, and the run stops while
      assembling the fighter models — "fighter parts num not match" then an
      out-of-bounds read in the metal-model setup, ft_800C85B8.)*
- [ ] M5 Saves: save created, page reloaded, save present; export/import round-trip
- [ ] M6 Single-file: `melee-offline.html` boots from `file://` on Chrome, Firefox, Safari
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
