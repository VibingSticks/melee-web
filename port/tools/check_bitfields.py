#!/usr/bin/env python3
"""Spike S4: list every bitfield in the game sources and flag any that would
straddle its storage unit.

CodeWarrior on PowerPC packs bitfields MSB-first, clang for wasm32 LSB-first.
The port's archive converter re-packs each storage unit (spec D2 step 3),
which is only correct when no field straddles a unit boundary. Both ABIs place
a field that does not fit into the next unit, so a field that straddles under
one ABI straddles under the other; checking the wasm32 layout is sufficient.

Usage: check_bitfields.py [--jobs N] [--json OUT]
Parses every .c under src/melee and src/sysdolphin with libclang for the
wasm32-unknown-emscripten target and TARGET_PC, deduplicating struct
definitions by (file, line).
"""
import argparse
import json
import multiprocessing
import pathlib
import sys

from clang import cindex

ROOT = pathlib.Path(__file__).resolve().parents[2]
ARGS = [
    "-xc", "-std=c99", "--target=wasm32-unknown-emscripten",
    "-DTARGET_PC", "-DLINT", "-Dbool=int", "-DVERSION_GALE01", "-DBUILD_VERSION=0",
    f"-I{ROOT}/src", f"-isystem{ROOT}/port/extern/aurora/include",
    "-ferror-limit=0",
]


def scan_unit(path: str):
    idx = cindex.Index.create()
    tu = idx.parse(path, args=ARGS, options=cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    found = {}
    for c in tu.cursor.walk_preorder():
        if c.kind not in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.UNION_DECL) or not c.is_definition():
            continue
        fields = [f for f in c.get_children() if f.kind == cindex.CursorKind.FIELD_DECL and f.is_bitfield()]
        if not fields:
            continue
        loc = c.location
        key = f"{loc.file.name if loc.file else '?'}:{loc.line}"
        entries = []
        for f in fields:
            width = f.get_bitfield_width()
            unit_bits = f.type.get_size() * 8
            off = f.get_field_offsetof()
            if off < 0 or width <= 0 or unit_bits <= 0:
                continue
            crosses = (off % unit_bits) + width > unit_bits
            entries.append({"name": f.spelling, "offset_bits": off, "width": width, "unit_bits": unit_bits, "crosses": crosses})
        found[key] = {"struct": c.spelling or "(anonymous)", "fields": entries}
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=max(1, multiprocessing.cpu_count() - 1))
    ap.add_argument("--json", type=pathlib.Path)
    a = ap.parse_args()
    units = sorted(str(p) for d in ("src/melee", "src/sysdolphin") for p in (ROOT / d).rglob("*.c"))
    merged = {}
    with multiprocessing.Pool(a.jobs) as pool:
        for found in pool.imap_unordered(scan_unit, units, chunksize=8):
            merged.update(found)
    total = sum(len(v["fields"]) for v in merged.values())
    crossings = [(k, v["struct"], f) for k, v in merged.items() for f in v["fields"] if f["crosses"]]
    print(f"units parsed: {len(units)}; structs with bitfields: {len(merged)}; bitfield members: {total}; storage-unit crossings: {len(crossings)}")
    for k, s, f in crossings:
        print(f"  {k} {s}.{f['name']}: offset {f['offset_bits']} width {f['width']} unit {f['unit_bits']}")
    if a.json:
        a.json.write_text(json.dumps(merged, indent=1))
    sys.exit(1 if crossings else 0)


if __name__ == "__main__":
    main()
