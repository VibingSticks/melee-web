#!/usr/bin/env bash
# Build the WGSL->GLSL translator (naga) to wasm for the WebGL2 fallback.
# Output: port/web/js/naga/naga.wasm. Needs rustup's wasm32-unknown-unknown target
# (tools/setup.sh does not install Rust; see docs/superpowers/specs/2026-09-11-webgl2-fallback-design.md).
set -euo pipefail
PORT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="$HOME/.cargo/bin:$PATH"
cd "$PORT_DIR/tools/naga-wasm"
# The repo root has a cargo config that redirects target-dir; pin ours explicitly.
TARGET_DIR="$PORT_DIR/build/naga-target"
cargo build --release --target wasm32-unknown-unknown --target-dir "$TARGET_DIR"
mkdir -p "$PORT_DIR/web/js/naga"
cp "$TARGET_DIR/wasm32-unknown-unknown/release/naga_wasm.wasm" "$PORT_DIR/web/js/naga/naga.wasm"
ls -la "$PORT_DIR/web/js/naga/naga.wasm"
