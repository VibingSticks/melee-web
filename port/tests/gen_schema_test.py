#!/usr/bin/env python3
"""Checks gen_schema.py on a small fixture header."""
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]


def test_fixture():
    out = subprocess.run([sys.executable, root / "tools/gen_schema.py", "--header", root / "tests/fixtures/schema_fixture.h",
                          "--annotations", root / "tests/fixtures/schema_fixture.yml", "--roots", root / "tests/fixtures/schema_roots.yml",
                          "--types", "Root,Leaf", "-"], capture_output=True, text=True, check=True).stdout
    assert 'const port_type port_T_Leaf = { "Leaf", 8,' in out, out
    assert re.search(r'\{F_U16, 0, \.name = "a"\}, \{F_U16, 2, \.name = "b"\}, \{F_F32, 4, \.name = "f"\}', out), out
    assert re.search(r'\{F_PTR_ARRAY, 4, &port_T_Leaf, LEN_FIELD_U32, 0, \.name = "items"\}', out), out
    assert re.search(r'\{F_PTR, 8, &port_T_Leaf, \.name = "shared"\}', out), out
    assert re.search(r'\{F_BITS, 12, NULL, 0, 0, 4, bits_Root_12, 3, \.name = "x"\}', out), out
    assert "static const uint8_t bits_Root_12[] = { 3, 5, 24 };" in out, out
    assert re.search(r'\{F_ARRAY, 16, &port_T___f32, LEN_CONST, 3, \.name = "fs"\}', out), out
    assert re.search(r'\{F_STRUCT, 28, &port_T_Root__w, \.name = "w"\}', out), out
    assert re.search(r"fields_Root__w\[\] = \{ \{F_WORD, 0, NULL\} \}", out), out
    assert "{F_U8" not in out, out                      # char name[8] is skipped
    assert re.search(r'\{F_STRUCT, 40, &port_T_Leaf, \.name = "inl"\}', out), out
    assert 'const port_type port_T_Root = { "Root", 48,' in out, out
    assert '{ "root", NULL, &port_T_Root }' in out, out
    assert "extern const port_type port_T___f32;" in out, out


if __name__ == "__main__":
    test_fixture()
    print("ok")
