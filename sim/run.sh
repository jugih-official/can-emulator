#!/usr/bin/env bash
# Builds the simulator and serves it at http://127.0.0.1:8515/
set -euo pipefail
cd "$(dirname "$0")"
./build.sh
exec python3 server.py
