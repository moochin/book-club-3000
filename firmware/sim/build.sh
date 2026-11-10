#!/usr/bin/env bash
# Builds the host UI simulator (needs g++ and libsdl2-dev for LovyanGFX's
# desktop back end). Run `pio pkg install -e cyd` first to fetch the libraries.
set -euo pipefail
cd "$(dirname "$0")/.."
LGFX=.pio/libdeps/cyd/LovyanGFX/src
AJ=.pio/libdeps/cyd/ArduinoJson/src
python3 ../tools/build_art.py
mkdir -p sim/out
SRCS=(sim/sim_main.cpp sim/stubs.cpp src/ui.cpp src/mascot.cpp src/display.cpp src/scenarios.cpp
      lib/bookclub_state/src/*.cpp)
LGFX_SRCS=$(find "$LGFX/lgfx/v1" -maxdepth 1 -name '*.cpp'; find "$LGFX/lgfx/v1/platforms/sdl" -name '*.cpp'; find "$LGFX/lgfx/utility" "$LGFX/lgfx/Fonts" -name '*.c')
for f in $LGFX_SRCS; do
  o=sim/out/$(basename "$f").o
  [ -f "$o" ] || g++ -x $( [[ $f == *.c ]] && echo c || echo c++ ) -std=$( [[ $f == *.c ]] && echo gnu11 || echo gnu++17 ) \
    -O1 -w -DLGFX_SDL -I"$LGFX" $(sdl2-config --cflags) -c "$f" -o "$o"
done
g++ -std=gnu++17 -O1 -Wall -DBC_SIM -DLGFX_SDL -Isim/shim -Isrc -Ilib/bookclub_state/src -I"$AJ" -I"$LGFX" \
  $(sdl2-config --cflags) "${SRCS[@]}" sim/out/*.o $(sdl2-config --libs) -lpthread -o sim/out/sim
echo "built sim/out/sim"
