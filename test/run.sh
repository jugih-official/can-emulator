#!/usr/bin/env bash
# Builds and runs the checksum tests on the host -- no Arduino hardware needed.
set -euo pipefail
cd "$(dirname "$0")"
g++ -std=c++11 -Wall -Wextra -I . -I .. test_klemmen.cpp ../mqb_klemmen.cpp ../mqb_mib.cpp ../mqb_vin.cpp -o /tmp/mqb_klemmen_test
/tmp/mqb_klemmen_test
