#!/usr/bin/env bash
# Builds the simulator: the sketch as arduino-cli preprocesses it (one .cpp
# with the function prototypes added), compiled for this machine against the
# mocks in mock/.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
arduino-cli compile --fqbn arduino:avr:uno --preprocess .. > build/sketch.cpp
g++ -std=c++17 -O1 -Wall -Wextra -I mock -I .. -o build/sim \
    sim_main.cpp build/sketch.cpp ../mqb_klemmen.cpp ../mqb_mib.cpp
