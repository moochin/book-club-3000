#!/usr/bin/env bash
# Renders the mascot art on the real UI screens, without a board.
# Needs: g++, libsdl2-dev, PlatformIO (once: cd firmware && pio pkg install -e cyd).
# Output: art/preview/screens.png and art/preview/poses.png
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/build_art.py
firmware/sim/build.sh >/dev/null
(cd firmware && ./sim/out/sim sim/out >/dev/null)
python3 tools/art_preview.py
