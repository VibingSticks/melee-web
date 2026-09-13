#!/usr/bin/env python3
"""Pack the hosted build into one self-contained HTML file.

    python3 tools/pack_single_html.py build/web-single -o melee-offline.html

The hosted page is an HTML shell plus a handful of ES modules, an Emscripten
program and naga's wasm. The offline file has to be openable straight from
file://, where a browser will not fetch a sibling file at all, so every piece
goes inline:

  * melee.js, built with -sSINGLE_FILE (the game's wasm is embedded in it, so
    there is no melee.wasm to load) and -sMODULARIZE (it defines createMelee
    rather than starting on load), becomes a classic <script>;
  * disc_source.js, gpu-gl2.js and naga/naga.js lose their `export` keywords
    and are concatenated ahead of boot.js, which loses its `import` line, so
    the four share one inline module scope;
  * naga.wasm is base64 in that module, and `__MELEE_OFFLINE__` hands it and
    the two renderer entry points to boot.js, which takes that route instead
    of the network one.

The disc image is still chosen by the player at runtime; it is never packed.
"""

import argparse
import base64
import pathlib
import re
import sys

# Concatenated, in this order, into one module scope. boot.js goes last because
# it uses what the others define.
MODULE_PARTS = ["disc_source.js", "gpu-gl2.js", "naga/naga.js"]
ENTRY = "boot.js"

# Every symbol the parts must still export into the shared scope after the
# `export` keywords come off. Checked so a rename upstream fails the pack
# rather than producing a page that dies at runtime.
REQUIRED_SYMBOLS = ["DiscSource", "installWebGL2Fallback", "loadNaga"]

BOOT_IMPORT = re.compile(r"(?m)^import\s*\{[^}]*\}\s*from\s*'\./disc_source\.js';\s*\n")
EXPORT_KEYWORD = re.compile(r"(?m)^export\s+")
LEFTOVER_MODULE_SYNTAX = re.compile(r"(?m)^\s*(import|export)\s")
BOOT_SCRIPT_TAG = re.compile(r"(?m)^\s*<script[^>]*\bsrc=[\"']boot\.js[\"'][^>]*>\s*</script>\s*\n")


class PackError(Exception):
    pass


def read(path: pathlib.Path) -> str:
    if not path.exists():
        raise PackError(f"{path} is missing; build the web-single preset first")
    return path.read_text(encoding="utf-8")


def close_script_safe(js: str) -> str:
    """Keep a literal </script in the JS from ending the element early.

    The sequence only ever appears inside a string or a comment, where the
    backslash is either an escape JS ignores or comment text, so the split form
    behaves identically.
    """
    return js.replace("</script", "<\\/script")


def build_module(build: pathlib.Path, naga_wasm_b64: str) -> str:
    chunks = []
    for name in MODULE_PARTS:
        text = read(build / name)
        text, n = EXPORT_KEYWORD.subn("", text)
        if n == 0:
            raise PackError(f"{name} exported nothing; the pack assumed it would")
        chunks.append(f"// ---- {name} ----\n{text}")

    boot = read(build / ENTRY)
    boot, n = BOOT_IMPORT.subn("", boot)
    if n != 1:
        raise PackError(f"{ENTRY}: expected exactly one disc_source import to strip, found {n}")

    bridge = (
        "// ---- offline bridge ----\n"
        "const __b64 = (s) => {\n"
        "  const bin = atob(s);\n"
        "  const out = new Uint8Array(bin.length);\n"
        "  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);\n"
        "  return out;\n"
        "};\n"
        f'globalThis.__MELEE_OFFLINE__ = {{ loadNaga, installWebGL2Fallback, nagaWasm: __b64("{naga_wasm_b64}") }};\n'
    )

    module = "\n".join(chunks + [bridge, f"// ---- {ENTRY} ----\n{boot}"])

    leftover = LEFTOVER_MODULE_SYNTAX.search(module)
    if leftover:
        line = module[: leftover.start()].count("\n") + 1
        raise PackError(f"module syntax survived packing at line {line}: {leftover.group(0)!r}")
    for sym in REQUIRED_SYMBOLS:
        if not re.search(rf"\b(class|function|const|let|var)\s+{sym}\b", module):
            raise PackError(f"{sym} is not defined in the packed module")
    return module


# Nothing here may make the page reach for a second file.
#
# The structural checks apply to the whole document: a <script src> or a <link
# href> is a load however it got there.
STRUCTURAL_REFS = [
    ("script element with a src", re.compile(r"<script[^>]+\bsrc\s*=", re.I)),
    ("link element with an href", re.compile(r"<link[^>]+\bhref\s*=", re.I)),
]

# The code checks apply to the shell and to our own JS, but not to the Emscripten
# runtime. That bundle carries XHR, fetch and URL helpers for every environment
# it can be built for -- Node, workers, a separate .wasm -- and keeps them even
# when, as here, the wasm is inlined and none of them run. Grepping a minified
# 12 MB bundle for dead branches reports noise, so what proves this file is
# self-contained is opening it from file:// and watching it boot, not this list.
CODE_REFS = [
    ("stylesheet or image url()", re.compile(r"\burl\(\s*[\"']?(?!data:)[^)\"']+\)")),
    ("fetch of something other than a data: URL", re.compile(r"fetch\(\s*[\"'](?!data:)")),
    ("XMLHttpRequest", re.compile(r"\bnew\s+XMLHttpRequest\b")),
    ("importScripts", re.compile(r"\bimportScripts\s*\(")),
]


def verify(html: str, runtime_js: str = "") -> list[str]:
    problems = []
    ours = html.replace(runtime_js, "\n", 1) if runtime_js else html
    for text, checks in ((html, STRUCTURAL_REFS), (ours, CODE_REFS)):
        for what, pattern in checks:
            for m in pattern.finditer(text):
                line = text[: m.start()].count("\n") + 1
                problems.append(f"line {line}: {what}: {m.group(0)[:80]!r}")
    if "__MELEE_OFFLINE__" not in html:
        problems.append("the offline bridge is missing; boot.js would take the network path")
    return problems


def pack(build: pathlib.Path) -> tuple[str, str]:
    """Returns the packed page and the embedded Emscripten runtime text."""
    shell = read(build / "index.html")
    melee_js = read(build / "melee.js")
    if "createMelee" not in melee_js:
        raise PackError("melee.js does not define createMelee: build with -sMODULARIZE")
    if "melee.wasm" in melee_js:
        raise PackError("melee.js still names melee.wasm: build with -sSINGLE_FILE")

    naga_wasm = build / "naga" / "naga.wasm"
    if not naga_wasm.exists():
        raise PackError(f"{naga_wasm} is missing; run tools/build_naga.sh")
    naga_b64 = base64.b64encode(naga_wasm.read_bytes()).decode("ascii")

    module = build_module(build, naga_b64)

    shell, n = BOOT_SCRIPT_TAG.subn("", shell)
    if n != 1:
        raise PackError(f"index.html: expected exactly one boot.js script tag, found {n}")

    blocks = (
        "<script>\n"
        + close_script_safe(melee_js)
        + "\n</script>\n<script type=\"module\">\n"
        + close_script_safe(module)
        + "\n</script>\n"
    )
    marker = "</body>"
    if marker not in shell:
        raise PackError("index.html has no </body> to insert before")
    return shell.replace(marker, blocks + marker, 1), close_script_safe(melee_js)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", type=pathlib.Path, help="a build directory from the web-single preset")
    ap.add_argument("-o", "--output", type=pathlib.Path, default=pathlib.Path("melee-offline.html"))
    a = ap.parse_args()

    try:
        html, runtime_js = pack(a.build)
    except PackError as e:
        print(f"pack_single_html: {e}", file=sys.stderr)
        return 2

    problems = verify(html, runtime_js)
    if problems:
        print("pack_single_html: the packed page still reaches outside itself:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    a.output.write_bytes(html.encode("utf-8"))
    print(f"{a.output}: {len(html) / 1048576:.1f} MB, self-contained")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
