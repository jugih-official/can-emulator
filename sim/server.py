#!/usr/bin/env python3
"""Serves the simulator page and keeps the simulated board running in real time.

    ./run.sh            builds the simulator and starts this
    http://127.0.0.1:8515/

The board is the build/sim process: the real sketch compiled for this machine.
This script feeds it wall-clock time and whatever the page asks for, and hands
the page the board's state. "Reset" restarts the process, which is what a
reset is; the EEPROM file survives it, as the real one does.
"""
import json
import os
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
BINARY = os.path.join(HERE, "build", "sim")
EEPROM = os.path.join(HERE, "build", "eeprom.bin")
PORT = int(os.environ.get("SIM_PORT", "8515"))
BENCH_KEYS = ("gateway", "radio", "tester", "car", "power", "volt", "knob")


class Board:
    def __init__(self):
        self.lock = threading.Lock()
        self.state = {}
        self.serial = ""
        self.bench = {}
        self.process = None
        self.start()

    def start(self):
        env = dict(os.environ, SIM_EEPROM=EEPROM)
        self.process = subprocess.Popen(
            [BINARY], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1, env=env
        )
        self.last = time.monotonic()
        # The bench stays as it was set up; only the board restarts.
        for key, value in self.bench.items():
            self._send("set %s %d" % (key, value))
        self._send("state")

    def _send(self, line):
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()
        self.state = json.loads(self.process.stdout.readline())
        self.serial = (self.serial + self.state.pop("ser", ""))[-6000:]

    def command(self, line):
        with self.lock:
            if line == "reset" or line == "forget":
                self.process.kill()
                self.process.wait()
                if line == "forget" and os.path.exists(EEPROM):
                    os.remove(EEPROM)
                self.serial += "\n--- reset ---\n"
                self.start()
                return
            parts = line.split()
            if len(parts) == 3 and parts[0] == "set" and parts[1] in BENCH_KEYS:
                self.bench[parts[1]] = int(parts[2])
            self._send(line)

    def tick(self):
        with self.lock:
            now = time.monotonic()
            elapsed = int((now - self.last) * 1000)
            if elapsed <= 0:
                return
            self.last += elapsed / 1000.0
            self._send("run %d" % min(elapsed, 2000))

    def snapshot(self):
        with self.lock:
            return dict(self.state, ser=self.serial)


board = None


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _reply(self, body, kind):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/state":
            self._reply(json.dumps(board.snapshot()).encode(), "application/json")
        elif self.path == "/font5x8.json":
            with open(os.path.join(HERE, "font5x8.json"), "rb") as f:
                self._reply(f.read(), "application/json")
        elif self.path in ("/", "/index.html"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                self._reply(f.read(), "text/html; charset=utf-8")
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path != "/cmd":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        for line in self.rfile.read(length).decode().splitlines():
            if line.strip():
                board.command(line.strip())
        self._reply(json.dumps(board.snapshot()).encode(), "application/json")


def ticker():
    while True:
        time.sleep(0.04)
        board.tick()


def main():
    global board
    if not os.path.exists(BINARY):
        sys.exit("build/sim is missing: run ./build.sh, or use ./run.sh")
    board = Board()
    threading.Thread(target=ticker, daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print("Simulator at http://127.0.0.1:%d/   (Ctrl-C stops it)" % PORT)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    board.process.kill()


if __name__ == "__main__":
    main()
