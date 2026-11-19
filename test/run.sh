#!/usr/bin/env bash
# Host tests for the game core (no device, no ESP-IDF needed).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
c++ -std=c++17 -Wall -Wextra -Werror -O1 -I../devkit/firmware/components/yaykit_ui/include \
    -o "$OUT/test_core" test_core.cpp ../main/pk_core.cpp \
    ../main/game/game.cpp ../main/game/synth.cpp
"$OUT/test_core"
rm -rf "$OUT"
