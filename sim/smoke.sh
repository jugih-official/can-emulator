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
expect 'home, ignition off, voltage read'   '|KL15 OFF  ?12.0V|' "$out"
expect 'battery icon only'                  '|B--- ' "$out"

out=$(play 'set tester 1' 'set volt 138' 'run 1500' 'press short' 'run 2500' 'run 250' 'run 250' 'run 250' 'run 250')
expect 'ignition on, voltage follows'       '|KL15 ON   ?13.8V|' "$out"
expect 'four icons, wave at its source'     '|BKLT 0 ' "$out"
expect 'wave fully out'                     '|BKLT 3 ' "$out"

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
expect 'wave gives way to the failure mark' ' !       e' "$out"

exit $fail
