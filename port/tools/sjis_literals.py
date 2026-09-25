#!/usr/bin/env python3
"""Re-encode a game source's string and character literals as Shift-JIS.

The decomp keeps its sources in UTF-8, and the GameCube build runs the
compiler through sjiswrap, which hands it Shift-JIS text: every non-ASCII
string the game draws or compares ("Ｍａｒｉｏ" on the character select, the
name-entry tables) is Shift-JIS in the original binary. Clang has no
-fexec-charset for Shift-JIS, so the web build compiles a copy of each such
file in which every non-ASCII character inside a literal is written as the
\\xNN escapes of its Shift-JIS bytes. Comments and code are copied unchanged,
and a #line directive keeps __FILE__ and diagnostics pointing at the original.

    sjis_literals.py SRC OUT     convert one file
    sjis_literals.py --list F... print the files that need converting
"""
import os
import sys

ENCODING = "cp932"  # what sjiswrap's Shift-JIS means (Windows-31J)
HEX = set("0123456789abcdefABCDEF")


def needs(text):
    return any(ord(ch) > 0x7F for ch in text)


def convert(text):
    out = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(text[i:j])
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(text[i:j])
            i = j
        elif ch in "\"'":
            quote = ch
            out.append(ch)
            i += 1
            after_hex = False  # the last thing written was a \xNN escape
            while i < n and text[i] != quote:
                c = text[i]
                if c == "\\":
                    esc = text[i:i + 2]
                    if esc in ("\\x", "\\X"):
                        j = i + 2
                        while j < n and text[j] in HEX:
                            j += 1
                        out.append(text[i:j])
                        i = j
                        after_hex = True
                        continue
                    out.append(esc)
                    i += 2
                    after_hex = False
                    continue
                if ord(c) > 0x7F:
                    out.append("".join("\\x%02X" % b for b in c.encode(ENCODING)))
                    after_hex = True
                    i += 1
                    continue
                if after_hex and c in HEX:
                    # "\x82\xA0" "A": a hex digit after an escape would extend it
                    if quote != '"':
                        raise SystemExit("sjis_literals: hex digit after an escape in a character literal")
                    out.append('""')
                out.append(c)
                after_hex = False
                i += 1
            if i < n:
                out.append(text[i])
                i += 1
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def main(argv):
    if len(argv) >= 2 and argv[1] == "--list":
        for path in argv[2:]:
            if not os.path.isfile(path):  # generated sources not built yet
                continue
            with open(path, encoding="utf-8") as f:
                text = f.read()
            if needs(text) and convert(text) != text:
                print(path)
        return 0
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    src, out = argv[1], argv[2]
    with open(src, encoding="utf-8") as f:
        text = f.read()
    body = convert(text)
    with open(out, "w", encoding="utf-8") as f:  # comments keep their UTF-8
        f.write('#line 1 "%s"\n' % src.replace("\\", "/"))
        f.write(body)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
