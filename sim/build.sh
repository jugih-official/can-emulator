#!/usr/bin/env bash
# Builds the simulator: the sketch, with the function prototypes an Arduino
# build would add, compiled for this machine against the mocks in mock/.
# Needs g++ and python3 only -- no arduino-cli, no Arduino libraries.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
python3 tools/sketch_to_cpp.py ../can-emulator.ino build/sketch.cpp
g++ -std=c++17 -O1 -Wall -Wextra -I mock -I .. -o build/sim \
    sim_main.cpp build/sketch.cpp ../mqb_klemmen.cpp ../mqb_mib.cpp ../mqb_vin.cpp
