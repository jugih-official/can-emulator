#!/usr/bin/env bash
# Runs the sketch in the simulator through its screens and checks what the LCD
# shows. Time is simulated, so the result is the same on every run.
set -euo pipefail
cd "$(dirname "$0")"
./build.sh

eeprom=$(mktemp)
trap 'rm -f "$eeprom"' EXIT
rm -f "$eeprom"
fail=0

# play <commands...>: one fresh board start, prints everything it showed.
play() { printf '%s\n' "$@" | SIM_EEPROM="$eeprom" ./build/sim --text; }

# expect <what> <text> <output>: the text must be among the output.
expect() {
  if grep -qF -- "$2" <<<"$3"; then echo "  ok    $1"; else echo "  FAIL  $1: no '$2'"; fail=1; fi
}

out=$(play 'run 1500')
expect 'home, ignition off, voltage read'   '|Ign off   ?12.0V|' "$out"
expect 'battery icon only'                  '|Bb----  ' "$out"

out=$(play 'set tester 1' 'set volt 138' 'run 1500' 'press short' 'run 2500' 'run 250' 'run 250' 'run 250' 'run 250')
expect 'ignition on, voltage follows'       '|Ign on    ?13.8V|' "$out"
expect 'three icons, wave at its source'    '|BbDdOo         0|' "$out"
expect 'wave fully out'                     '|BbDdOo         3|' "$out"

out=$(play 'run 1500')
expect 'ignition restored after a reset'    'restored: Klemme 15 ON' "$out"

out=$(play 'run 500' 'press long' 'press short' 'press short' 'press long' 'press short' 'press short')
expect 'menu opens on Kl X'                 '|Kl X         1/9|' "$out"
expect 'dimming steps'                      '|110    hold=+10 |' "$out"
expect 'clock item'                         '|Clock minute 5/9|' "$out"

out=$(play 'set radio 1' 'run 1500' 'press long' 'press short' 'press short' 'press short' 'press short' \
           'press short' 'press long' 'run 1200' 'press short' \
           'press short' 'press short' 'press short' 'press short' 'press short' 'press short' 'press short' \
           'press short' 'press short' 'press short' 'press short' 'press short' 'press short' \
           'run 3500' 'set knob 77' 'run 300' 'run 250' 'run 250')
expect 'bus monitor summary'                '|ids 13 ext' "$out"
expect 'who is awake'                       '|10 5F           |' "$out"
expect 'learning view: baseline'            '|3E5  learn 13/13|' "$out"
expect 'learning view: changed byte shown'  '4D0000000000|' "$out"
expect 'learning view: changed byte blinks' '00  0000000000|' "$out"

out=$(play 'run 1000' 'set power 0' 'run 2500' 'set power 1' 'run 4000' 'ser s')
expect 'module power loss is noticed'       '|MCP2515 FAILED  |' "$out"
expect 'and recovered from'                 'MCP2515 re-init: ok' "$out"

out=$(play 'set gateway 0' 'run 2500' 'ser s')
expect 'nobody acknowledging: errors count' 'tec 128' "$out"
expect 'wave gives way to the failure mark' ' !|' "$out"

out=$(play 'run 500' 'ser d' 'ser VBENCH0123456789AZ' 'run 700' 'ser s')
expect 'a typed VIN goes out as three frames' '6B4 [ok ] 00 00 00 00 00 42 45 4E' "$out"
expect 'the third of them'                   '6B4 [ok ] 02 35 36 37 38 39 41 5A' "$out"
expect 'and the status says so'              'VIN (6B4): BENCH0123456789AZ, ours' "$out"

out=$(play 'run 500' 'ser VBENCH0123456789AZ' 'run 300' 'ser V-' 'run 300' 'ser d' 'run 700' 'ser s')
expect 'V- stops it'                         'VIN (6B4): off' "$out"
if grep -qF '6B4 [' <<<"$out"; then echo "  FAIL  a stopped VIN is still sent"; fail=1; else echo "  ok    and nothing more is sent"; fi

out=$(play 'run 500' 'ser V' 'run 6000' 'ser s')
expect 'a VIN left half typed is given up'   'VIN (6B4): off' "$out"
expect 'and the next key is a command again' '--- status ---' "$out"

exit $fail
