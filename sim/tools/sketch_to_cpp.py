#!/usr/bin/env python3
"""Turns can-emulator.ino into a .cpp a plain C++ compiler accepts.

An .ino may call a function before defining it; the Arduino build adds the
missing prototypes. This does the same, so that the simulator needs neither
arduino-cli nor the Arduino libraries: it puts a prototype for every function
of the sketch in front of the first function definition.

It relies on how the sketch is written: a function starts at column 0 and its
signature is on one line that ends with "{".
"""
import re
import sys

SIGNATURE = re.compile(r"^(?:static\s+)?[A-Za-z_][\w\s\*&:<>]*?\b[A-Za-z_]\w*\([^;{}]*\)\s*\{\s*$")

source, target = sys.argv[1], sys.argv[2]
lines = open(source).read().split("\n")
found = [(i, l) for i, l in enumerate(lines) if SIGNATURE.match(l)]
if not found:
    sys.exit("no function definitions found in " + source)

first = found[0][0]
out = ["#include <Arduino.h>", '#line 1 "%s"' % source]
out += lines[:first]
out += [l.rstrip()[:-1].rstrip() + ";" for _, l in found]
out.append('#line %d "%s"' % (first + 1, source))
out += lines[first:]
open(target, "w").write("\n".join(out))
