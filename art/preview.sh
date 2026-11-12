#!/usr/bin/env bash
# Renders the mascot art on the real UI screens, without a board.
# Needs: g++, libsdl2-dev, PlatformIO (once: cd firmware && pio pkg install -e cyd).
# Output: art/preview/screens.png and art/preview/poses.png, and the
# generated sounds as art/preview/sounds/*.wav
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/build_art.py
firmware/sim/build.sh >/dev/null
(cd firmware && ./sim/out/sim sim/out >/dev/null)
python3 tools/art_preview.py
mkdir -p art/preview/sounds firmware/sim/out
g++ -std=gnu++17 -O2 -Ifirmware/src firmware/sim/sound_preview.cpp firmware/src/audio_gen.cpp \
  -o firmware/sim/out/sound_preview
firmware/sim/out/sound_preview art/preview/sounds >/dev/null
