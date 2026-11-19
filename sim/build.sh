#!/bin/sh
# Builds and runs the Mac simulator; frames land in sim/build/*.ppm (converted to PNG when Pillow is available).
set -e
cd "$(dirname "$0")"
mkdir -p build
clang++ -std=c++17 -O2 -Wall -Wextra -I../main/game -I../devkit/firmware/components/yaykit_ui/include \
    ../main/game/game.cpp ../main/game/synth.cpp game_sim.cpp -o build/game_sim
./build/game_sim build "${1:-zh}"
