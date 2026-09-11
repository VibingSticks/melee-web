# Spike results

Each spike from spec §11 gets an entry: date, PASS/FAIL, measurements, and
what the result decided.

| # | Question | Result |
|---|---|---|
| S1 | Does Aurora's `simple` example build and run under Emscripten + emdawnwebgpu? | not run |
| S2 | Is the Asyncify size/CPU cost acceptable for the full game? | not run |
| S3 | Which storage/audio APIs work on `file://` in Chrome, Firefox, Safari? | not run |
| S4 | Does any bitfield cross its storage unit under either ABI? | not run |
| S5 | Are all pointer fields in game data listed in archive relocation tables? | not run (needs a disc image) |
| S6 | Peak wasm heap with MEM1 + ARAM + read cache? | not run |
