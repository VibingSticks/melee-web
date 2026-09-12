#!/usr/bin/env bash
# Serve the spike build and capture a headless-Chrome screenshot plus console log.
# Usage: run.sh [seconds]   (default 6)
set -euo pipefail
cd "$(dirname "$0")"
SECS="${1:-6}"
PORT=8765
python3 -m http.server -d build "$PORT" >/dev/null 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null || true' EXIT
sleep 1
CHROME="${CHROME:-google-chrome-stable}"
"$CHROME" --headless=new --no-sandbox --disable-gpu-sandbox \
  --enable-unsafe-webgpu --ignore-gpu-blocklist --use-angle=vulkan --enable-features=Vulkan \
  --enable-logging=stderr --v=0 --window-size=1280,960 \
  --virtual-time-budget=$((SECS * 1000)) --timeout=$((SECS * 1000 + 5000)) \
  --screenshot="$PWD/screenshot.png" "http://localhost:$PORT/simple.html" 2> chrome.log || true
echo "--- console (chrome.log) ---"
grep -E 'CONSOLE|Uncaught|RuntimeError|Aurora|WebGPU|aurora' chrome.log | sed 's/.*CONSOLE/CONSOLE/' | head -80
echo "--- screenshot: $PWD/screenshot.png ---"
