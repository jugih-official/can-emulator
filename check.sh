#!/usr/bin/env bash
# Runs everything that can be verified without a board:
#   1. host unit tests for the frame builders
#   2. a real AVR compile of the sketch
#   3. the sketch in the simulator, checking what the LCD shows
set -euo pipefail
cd "$(dirname "$0")"

echo "== host tests =="
./test/run.sh

echo
echo "== AVR compile (arduino:avr:uno) =="
if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli not found. See docs/USAGE.md for installation." >&2
  exit 1
fi
arduino-cli compile --fqbn arduino:avr:uno --warnings all .

echo
echo "== simulator: the screens =="
./sim/smoke.sh
