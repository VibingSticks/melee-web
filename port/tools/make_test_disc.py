#!/usr/bin/env python3
"""Write a tiny synthetic GALE01 disc image for boot smoke tests.

It has a valid header and file table but only the files given on the command
line (as name=path or name=size-of-zeros), so the game boots, initialises, and
fails at its first real asset load. No game data is involved.

Usage: make_test_disc.py OUT.iso [name=path|name=NNN ...]
"""
import struct
import sys
import pathlib


def main():
    out = pathlib.Path(sys.argv[1])
    files = []
    for spec in sys.argv[2:]:
        name, _, src = spec.partition("=")
        data = pathlib.Path(src).read_bytes() if not src.isdigit() else bytes(int(src))
        files.append((name, data))

    # FST: root, then per top-level directory a dir entry followed by its
    # files, then root-level files, then the string table. One level of
    # directories ("audio/main.ssm") is supported.
    groups = {}
    root_files = []
    for name, data in files:
        if "/" in name:
            d, f = name.split("/", 1)
            groups.setdefault(d, []).append((f, data))
        else:
            root_files.append((name, data))
    count = 1 + len(groups) + len(files)
    strings = b"\0"
    entries = [struct.pack(">BBHII", 1, 0, 0, 0, count)]
    data_start = 0x10000
    blobs = []
    pos = data_start

    def add_file(name, data):
        nonlocal strings, pos
        name_off = len(strings)
        strings += name.encode() + b"\0"
        entries.append(struct.pack(">BBHII", 0, name_off >> 16, name_off & 0xFFFF, pos, len(data)))
        blobs.append((pos, data))
        pos += (len(data) + 0x7FFF) & ~0x7FFF

    for d, members in groups.items():
        name_off = len(strings)
        strings += d.encode() + b"\0"
        next_index = len(entries) + 1 + len(members)
        entries.append(struct.pack(">BBHII", 1, name_off >> 16, name_off & 0xFFFF, 0, next_index))
        for f, data in members:
            add_file(f, data)
    for name, data in root_files:
        add_file(name, data)
    fst = b"".join(entries) + strings
    fst_off = 0x2440

    img = bytearray(pos)
    img[0:6] = b"GALE01"
    img[6:8] = b"\x00\x02"                       # disc 0, version 1.02
    img[0x20:0x20 + 32] = b"Super Smash Bros. Melee".ljust(32, b"\0")
    img[0x1C:0x20] = b"\xC2\x33\x9F\x3D"           # GameCube magic
    struct.pack_into(">III", img, 0x420, 0, fst_off, len(fst))  # DOL offset (none), FST offset, FST size
    struct.pack_into(">I", img, 0x42C, len(fst))                # max FST size
    img[fst_off:fst_off + len(fst)] = fst
    for off, data in blobs:
        img[off:off + len(data)] = data
    out.write_bytes(img)
    print(f"{out}: {len(img)} bytes, {len(files)} file(s), FST at 0x{fst_off:x} ({len(fst)} bytes)")


if __name__ == "__main__":
    main()
