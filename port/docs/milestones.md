# Milestone checklist

Manual acceptance list from spec §10. Tick with date and browser.

- [x] M1 Link: wasm links; first `OSReport` appears in the console (2026-09-11, Chrome 151, synthetic disc)
- [ ] M2 Title: title screen renders with correct textures
- [ ] M3 Menus: main menu, VS mode, character select; gamepad on all four ports; first sound; music
- [ ] M4 Match: two humans, Final Destination, full match with audio
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

All other units, including `lb_01F8.c`, `lbmthp.c`, `fog.c`, `pobj.c`, `video.c`, `lb_0195.c`, `lbcardnew.c` and `gmmain.c`, now build (THP decoding itself is stubbed until Task 22).
