#!/usr/bin/env python3
"""Checks pack_single_html.py on a fixture build directory."""
import importlib.util
import pathlib
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("pack", root / "tools/pack_single_html.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)

SHELL = """<!doctype html>
<html><head><title>Melee</title></head>
<body>
<canvas id="canvas"></canvas>
<script type="module" src="boot.js"></script>
</body></html>
"""

# Stands in for the Emscripten output: defines the factory, keeps its wasm
# inline, and contains a literal </script> the way a minified bundle can.
MELEE_JS = """var createMelee = (() => {
  var wasmBinaryFile = 'data:application/octet-stream;base64,AGFzbQEAAAA=';
  var tag = '</script>';
  return async function (cfg) { return { cfg, tag, wasmBinaryFile }; };
})();
"""

DISC_SOURCE = "export class DiscSource {\n  constructor(f) { this.file = f; }\n}\n"
GPU_GL2 = "export function installWebGL2Fallback(opts) { return opts; }\n"
NAGA_JS = "export async function loadNaga(source) { return { source }; }\n"
BOOT = """import { DiscSource } from './disc_source.js';
const OFFLINE = globalThis.__MELEE_OFFLINE__ ?? null;
function rendererModules() {
  if (OFFLINE) return Promise.resolve(OFFLINE);
  return Promise.all([import('./naga/naga.js'), import('./gpu-gl2.js')]);
}
const nagaSource = () => (OFFLINE ? OFFLINE.nagaWasm : 'naga/naga.wasm');
function meleeFactory() {
  if (globalThis.createMelee) return Promise.resolve(globalThis.createMelee);
  const script = document.createElement('script');
  script.src = 'melee.js';
  return new Promise((res) => { script.onload = res; document.body.appendChild(script); });
}
new DiscSource(null); rendererModules(); nagaSource(); meleeFactory();
"""


def make_build(tmp: pathlib.Path, **overrides) -> pathlib.Path:
    build = tmp / "build"
    (build / "naga").mkdir(parents=True, exist_ok=True)
    files = {
        "index.html": SHELL,
        "melee.js": MELEE_JS,
        "boot.js": BOOT,
        "disc_source.js": DISC_SOURCE,
        "gpu-gl2.js": GPU_GL2,
        "naga/naga.js": NAGA_JS,
    }
    files.update(overrides)
    for name, text in files.items():
        (build / name).write_text(text, encoding="utf-8")
    (build / "naga/naga.wasm").write_bytes(b"\0asm\x01\0\0\0" + bytes(range(256)))
    return build


def expect_error(build, fragment):
    try:
        pack.pack(build)
    except pack.PackError as e:
        assert fragment in str(e), f"wanted {fragment!r} in {e!r}"
        return
    raise AssertionError(f"expected a PackError mentioning {fragment!r}")


def test_pack():
    with tempfile.TemporaryDirectory() as d:
        build = make_build(pathlib.Path(d))
        html, runtime = pack.pack(build)

        assert pack.verify(html, runtime) == [], pack.verify(html, runtime)
        # The module parts share one scope and no module syntax survives.
        assert "class DiscSource" in html and "export class" not in html
        assert "function installWebGL2Fallback" in html
        assert "function loadNaga" in html
        assert "import { DiscSource }" not in html
        # The bridge carries the naga bytes and is defined before boot.js runs.
        assert "__MELEE_OFFLINE__" in html
        assert html.index("__MELEE_OFFLINE__ =") < html.index("// ---- boot.js ----")
        assert "nagaWasm: __b64(" in html
        # The factory is a classic script, so it runs before the module.
        assert html.index("var createMelee") < html.index('<script type="module">')
        # Exactly two closers -- the classic block and the module block -- so the
        # literal </script> inside the JS was defused rather than ending one early.
        assert html.count("</script>") == 2, html.count("</script>")
        assert "<\\/script" in html
        # The disc is still the player's to supply.
        assert "GALE01" not in html


def test_rejects_a_plain_script_build():
    with tempfile.TemporaryDirectory() as d:
        build = make_build(pathlib.Path(d), **{"melee.js": "var Module = {};\n"})
        expect_error(build, "createMelee")


def test_rejects_a_separate_wasm():
    with tempfile.TemporaryDirectory() as d:
        js = "var createMelee = 1; var wasmBinaryFile = 'melee.wasm';\n"
        build = make_build(pathlib.Path(d), **{"melee.js": js})
        expect_error(build, "SINGLE_FILE")


def test_rejects_an_unexpected_shell():
    with tempfile.TemporaryDirectory() as d:
        build = make_build(pathlib.Path(d), **{"index.html": SHELL.replace('src="boot.js"', 'src="other.js"')})
        expect_error(build, "boot.js script tag")


def test_verify_catches_a_leftover_reference():
    with tempfile.TemporaryDirectory() as d:
        build = make_build(pathlib.Path(d))
        html, runtime = pack.pack(build)
        problems = pack.verify(html + '\n<link rel="stylesheet" href="app.css">')
        assert any("link element" in p for p in problems), problems
        problems = pack.verify(html + '\n<script>fetch("melee.wasm")</script>')
        assert any("fetch" in p for p in problems), problems
        # A data: URL is inlined content, not a second file.
        assert pack.verify(html + '\n<script>fetch("data:application/wasm;base64,AA")</script>') == []


if __name__ == "__main__":
    test_pack()
    test_rejects_a_plain_script_build()
    test_rejects_a_separate_wasm()
    test_rejects_an_unexpected_shell()
    test_verify_catches_a_leftover_reference()
    print("ok")
