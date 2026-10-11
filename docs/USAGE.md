# Usage

## Installing the toolchain

### Arduino IDE

1. **Install the CAN library.** *Tools → Manage Libraries…*, search `mcp_can`,
   install **"MCP_CAN Library" by Cory J. Fowler**. There are several similarly
   named libraries — the API used here (`CAN.begin(MCP_ANY, ...)`,
   `CAN.setMode(MCP_NORMAL)`) is his. The display uses **LiquidCrystal** by
   Arduino, which the IDE already has.
2. Open `can-emulator.ino`. The containing folder must stay named
   `can-emulator` so it matches the sketch filename.
3. *Tools → Board → Arduino Uno*, pick the port, and upload.

### arduino-cli

No root needed — arduino-cli installs into your home directory:

```sh
mkdir -p ~/.local/bin
curl -fsSL https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Linux_64bit.tar.gz \
  | tar xz -C ~/.local/bin arduino-cli
```

Most distributions already put `~/.local/bin` on the PATH from `~/.profile`; if
`arduino-cli version` is not found, open a new login shell.

```sh
arduino-cli core update-index
arduino-cli core install arduino:avr
arduino-cli lib install "mcp_can"          # by coryjfowler -- check the author
arduino-cli lib install LiquidCrystal      # by Arduino, for the display
```

Then build, flash and monitor:

```sh
arduino-cli compile --fqbn arduino:avr:uno .
arduino-cli upload  --fqbn arduino:avr:uno -p /dev/ttyACM0 .
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

A genuine Uno is `/dev/ttyACM0`; a CH340-based clone — most of the cheap ones,
USB-C or not — shows up as `/dev/ttyUSB0`, so use that in the commands above.
`arduino-cli board list` shows connected boards and their ports. If uploads fail
with a permission error, add yourself to the serial group once, then log out and
back in:

```sh
sudo usermod -aG dialout "$USER"
```

### Verifying without a board

```sh
./check.sh
```

Runs the host tests and then a real AVR compile with `--warnings all`. A clean
run ends with the footprint:

```
Sketch uses 15888 bytes (49%) of program storage space. Maximum is 32256 bytes.
Global variables use 801 bytes (39%) of dynamic memory, leaving 1247 bytes...
```

RAM is the tighter of the two budgets; raising `SNIFFER_MAX_IDS` costs 12 bytes
per entry.

## First run

Open the serial monitor at **115200 baud**. You should see:

```
VW MQB Klemme 15 emulator
checksum self-test: PASS
MCP2515 init: ok
1/0 ign on/off   t toggle ign   x toggle Kl X   5 pulse Kl 50
s status   d toggle tx trace   r toggle rx dump   p rx stats
c clear rx stats   +/- dimming   l listen-only   h help
b short press   B long press   v what the LCD shows
V and 17 characters: send that VIN   V- stop
```

Both `PASS` and `ok` are required.

Klemme 15, Klemme X and listen-only are remembered in EEPROM
(`REMEMBER_STATE`). After a reset or a power cut the board comes back as it
was and says so: `restored: Klemme 15 ON`. A board that has never stored
anything starts with the ignition off.

- `checksum self-test: FAIL` — the code has been modified incorrectly. Run
  `./test/run.sh` to see which case broke.
- `MCP2515 init: FAILED` — SPI wiring or power. See troubleshooting below.

`Klemmen_Status_01` transmits immediately at 10 Hz whether ignition is on or
off. That is deliberate: a real gateway does the same, and it is what keeps the
bus awake. The one way to silence it is listen-only mode (`l`), which is for
sniffing a real car.

## Serial commands

| Key | Action |
|---|---|
| `1` | Klemme 15 on (ignition on) |
| `0` | Klemme 15 off |
| `t` | Toggle Klemme 15 — same as the button |
| `x` | Toggle Klemme X (load shedding) |
| `5` | Pulse Klemme 50 (starter request) for `KL50_PULSE_MS` |
| `+` / `-` | Display brightness up / down, in steps of 10 |
| `s` | Status: terminal states, clock, TX and RX counters, the controller's error flags |
| `d` | Toggle per-frame transmit trace |
| `r` | Toggle received-frame dump. 29-bit IDs are printed with an `x` in front. |
| `p` | Print per-ID receive statistics |
| `c` | Clear receive statistics |
| `l` | Toggle listen-only: nothing is transmitted, not even acknowledge bits. The mode for a real car. |
| `b` / `B` | A short / long press of the button, for driving the menu from the console |
| `v` | Print the two lines the LCD is showing |
| `V` + 17 characters | Send that VIN as `VIN_01` (`0x6B4`), until `V-` or a reset. See [Sending a VIN](#sending-a-vin). |
| `h` | Help |

### Reading the status

```
  5F0 / 6B2: ours / bus sends it, ours off
  VIN (6B4): off
  voltage  : 12.0 V (source not verified)
  tester   : present
  awake    : 10
  tx ok    : 317
  tx err   : 0
  rx frames: 506
  rx 29-bit: 40
  chip     : eflg 0x40  tec 0  rec 0
  re-inits : 0
```

- `5F0 / 6B2` — whether the emulator sends each of the two MIB messages. When
  another node is heard sending one, a gateway for instance, the emulator's own
  is withheld until that node has been silent for `MIB_YIELD_MS`: two senders of
  one ID with different payloads would collide.
- `VIN (6B4)` — `off`, or the VIN being sent. Like the two above it reads
  `bus sends it, ours off` while another node sends that ID.
- `rx 29-bit` — frames with an extended ID, which on MQB is network management
  (`0x1B0000xx`) and some `0x17xxxxxx` messages. They are counted here and
  shown by `r`, but the `p` table holds 11-bit IDs only.
- `voltage`, `tester`, `awake` — the same three things the display shows: the
  supply voltage read from the bus, whether a tester is talking, and the node
  addresses of the units sending network management.
- `chip` — read from the MCP2515. `tec` / `rec` are the CAN transmit and receive
  error counters; `tec` climbing means frames are not being acknowledged.
  `eflg` bits `0x40` / `0x80` mean a received frame was lost because the sketch
  was busy, typically printing a dump; bit `0x20` is bus-off.
- `re-inits` — how often the once-a-second health check found the controller
  in the wrong mode and set it up again. It should stay 0; counting up points
  at the module's 5 V or ground.

Klemme 50 is refused while ignition is off — you cannot crank without T15, and
neither can the real car.

## The button and the menu

One push button on **D4** does everything, by telling a short press from a held
one (`BUTTON_LONG_MS`, 0.7 s).

| Screen | Short press | Hold |
|---|---|---|
| Home | Toggle Klemme 15, like `t` | Open the menu |
| Menu | Next item | Act on the item |
| Bus monitor | Next ID | Back to the menu |

The menu shows the item and its position on the top line, its value and what
holding does on the bottom line:

```
Dimming      3/9
100    hold=+10
```

| Item | Holding the button |
|---|---|
| `Kl X` | Switches Klemme X on / off |
| `Kl 50 crank` | Starter request for as long as it is held. Needs Klemme 15 on. |
| `Dimming` | +10 per step, 10…250, then round again; keeps stepping while held |
| `Clock hour` | +1 hour per step |
| `Clock minute` | +1 minute per step, seconds back to 0 |
| `Bus monitor` | Opens the monitor, below |
| `Listen-only` | On / off, like `l` |
| `Health` | Nothing; it is there to be read |
| `Back` | Home screen |

A menu left alone for 20 s (`MENU_TIMEOUT_MS`) returns to the home screen,
except on `Health` and in the bus monitor. Dimming and the clock exist only
with `ENABLE_MIB_MESSAGES`.

`Health` reads `t  0 r  0 e40 i0`: the controller's transmit and receive error
counters, its error flags in hex and the number of re-inits, as explained under
[Reading the status](#reading-the-status).

### Bus monitor

The first page is the summary, the second says who is awake, then comes one
page per 11-bit ID in the order they were first seen:

```
BUS    85 fps       frames per second, all IDs
ids 13 ext    84    11-bit IDs seen, and 29-bit frames received

awake  2            units sending network management right now
10 5F               their node addresses, in hex

3DC   49ms  3/13    ID, time since its previous frame, page
FF01000000010000    its latest payload, eight bytes as hex
```

**Who is awake.** Every MQB unit that is awake sends a network-management
frame about five times a second, with the 29-bit ID `0x1B0000xx`; `xx` is the
unit's node address. `10` is the gateway. A unit drops off the list 3 s after
its last frame (`NM_AWAKE_MS`). The list only listens, it asks nothing, so it
is safe next to a tester. It holds six units and shows five, with a `+` for
more.

**Learning view.** Each ID page shows which byte of the payload reacts to what
you do. For the first 3 s on a page the top line reads `learn`: bytes that
change during that time change by themselves, counters and checksums, and are
left alone from then on. After that, a byte that changes blinks for 3 s. So:
open the page, wait for `learn` to give way to the interval, press the button
on the radio or turn the knob, and watch which byte blinks. A short press goes
to the next ID and starts over; coming back to an ID learns it afresh.

It reads the same table as `p`, so `c` on the console empties it, and it holds
at most `SNIFFER_MAX_IDS` IDs. It works in listen-only mode too, which makes
the box a stand-alone sniffer for a real car.

Debounce is 40 ms (`BUTTON_DEBOUNCE_MS`). The Uno has no usable built-in button
— RESET is the only one on the board — so this needs a switch wired between D4
and GND. See [HARDWARE.md](HARDWARE.md).

## The display

A 16×2 LCD on **A0–A5** (`ENABLE_LCD`, on by default) shows the state without a
laptop. The home screen:

```
Ign on  X ?12.3V    the ignition being sent, X while Klemme X is set, supply voltage
BbDdOo         w    three icons side by side, and the wave in the corner
```

The first line says `Ign on` or `Ign off`: the Klemme 15 state the emulator is
sending. It reads `Cranking` while Klemme 50 is held and `Listen` in
listen-only mode, where no ignition is sent at all; `MCP2515 FAILED` or
`CHECKSUM FAIL` takes the line after a failed boot check.

The three icons are drawn on the glass, each across two character cells, with
nothing between them. One that does not apply is two dashes:

| Icon | Terminal | Shown when |
|---|---|---|
| Car battery, with its two posts and + − | Klemme 30 | A supply voltage is being read from the bus. It blinks below 11.5 V (`VOLTAGE_LOW_DV`). |
| Instrument cluster, a dial with its needle | Klemme 58 | The dimming message `0x5F0` is on the bus, the emulator's or another unit's. |
| Diagnostic socket, the 16-pin trapezoid | — | A diagnostic tester is talking: a frame in `0x700`–`0x7FF` within the last 3 s. |

**The wave** in the lower right corner, a dot with arcs spreading from it,
shows that the traffic is alive. It keeps waving for as long as the emulator's
frames go out and are acknowledged; in listen-only mode, for as long as frames
come in. It is replaced by `!` while frames are failing to send, usually
because nothing on the bus acknowledges them, and by a dash when nothing moves
at all. Once any frame has failed, the number of failed frames stays on the
line as `err 12`, so that a problem that came and went is not missed. It
counts frames the emulator could not send since power-up; a reset clears it.

**The voltage** is read from the bus, not measured by the Arduino: one byte of
a frame that some unit broadcasts, set by `VOLTAGE_CAN_ID`, `VOLTAGE_BYTE` and
`VOLTAGE_MV_PER_BIT`. The default, byte 0 of `0x663` at 0.1 V per bit, is a
candidate and has not been confirmed, which is why the value carries a `?`.
To confirm it, turn the bench supply up and down and watch the display follow;
then set `VOLTAGE_VERIFIED 1` and the `?` goes. If it does not follow, find the
right byte with the learning view, changing the supply voltage as the thing
you do. `--.-V` means the frame is not on the bus.

In listen-only mode the first line reads `LISTEN`. `CHECKSUM FAIL` or
`MCP2515 FAILED` takes the first line when boot found that problem. The glass
is refreshed every 250 ms (`LCD_REFRESH_MS`) and only the characters that
changed are written, so the CAN cycle does not notice it. An LED on D6 can show
Klemme 15 as well (`ENABLE_STATUS_LED`).

## Typical workflows

### Diagnosing or flashing a control unit through the gateway

The bench as drawn in [HARDWARE.md](HARDWARE.md#the-bench-with-a-gateway):
emulator on the gateway's convenience bus, tester on the OBD socket.

1. Power the bench. The wave in the corner of the LCD is waving: the gateway
   is acknowledging.
2. Press the button: the LCD says `Ign on`.
3. Connect the tester. In ODIS the gateway's measured value *Terminal 15 status
   via CAN* should read on.
4. Work on the control unit. For a flash, read
   [Before flashing a control unit](HARDWARE.md#before-flashing-a-control-unit)
   first: above all, nothing may reset the Uno while it runs.

If the tester finds no gateway, put the emulator in listen-only on the
diagnostic pair for a moment (`Listen-only`, then `Bus monitor`): a tester
that is scanning shows up as `700` and `7xx` frames. Nothing at all means the
pair itself is open or swapped.

### Waking a MIB head unit on the bench

1. Wire the MCP2515 to the Infotainment-CAN, common ground everywhere. This is
   the bench without a gateway; with one, the radio goes on gateway pins
   10 / 20 and the emulator stays on the convenience bus.
2. Feed 12 V to the module's Klemme 30 pin through a fuse.
3. Power the Arduino. `Klemmen_Status_01` starts flowing at once.
4. Press the button, or send `1`. The unit should boot.
5. Send `s` and confirm `tx err` is not climbing.

If it boots and then sleeps after a few seconds, it wants cyclic messages this
project doesn't send yet. Sniff a real car to find out which — see below.

### Sending a VIN

On a car the gateway sends the vehicle identification number round the bus as
`VIN_01` (`0x6B4`). The emulator can send it instead, for a unit on a bench
without a gateway, or to see what a unit does with one:

```
VBENCH0123456789AZ        V and the 17 characters, no space
  VIN (6B4): BENCH0123456789AZ, ours
V-                        stops it
  VIN (6B4): off
```

- It is not stored. A reset comes back with no VIN, so a number made up for
  the bench cannot return by itself.
- Letters may be typed in lower case. `I`, `O` and `Q` are not VIN characters;
  one of those, or anything else a VIN cannot hold, ends the typing with the
  VIN off. A VIN left half typed is given up after 5 s (`VIN_ENTRY_MS`): until
  then the keys go into the VIN, not to the commands.
- This does not give a gateway a VIN. A gateway is the sender of this message,
  not a listener; its own VIN is written by a tester. And the four
  component-protection bytes go out as zero, see
  [PROTOCOL.md](PROTOCOL.md#vin_01--id-0x6b4-1716).
- With a gateway on the bus that sends `0x6B4` itself, the emulator's stands
  back, as for `0x5F0` and `0x6B2`.

### Finding out what a module actually needs

Receive statistics are collected continuously, so you don't have to be dumping
frames to gather them.

1. Send `l` so the board transmits nothing, not even acknowledge bits; `s`
   shows `mode : listen-only`. Or build with `LISTEN_ONLY_DEFAULT 1` so it
   comes up that way.
2. Connect to a **real, working** car's bus.
3. Let it run for 30 seconds with the ignition on.
4. Press `p`:

```
  ID  count   min   max  (ms between frames)
 3C0	612	98	103
 5F0	122	497	504
 6B2	61	996	1004
```

That gives you both the ID list and the true cycle times. Press `c` to clear
between captures — for example, capture with ignition off, clear, then capture
with ignition on, and diff the two ID lists.

Use `r` to dump payloads when you need to see the bytes rather than the rates.

> Before connecting to a complete vehicle, read the safety note in
> [HARDWARE.md](HARDWARE.md#safety-and-legal-notes). Do not transmit `0x3C0`
> onto a bus where the real gateway is also transmitting it.

### Verifying your own car's frame layout

With `r` on and the ignition being switched, watch `3C0`. Byte 2 should step
`0x00 → 0x01 → 0x03` as key-in then ignition-on happen, and byte 1 should count
`0..F` repeatedly. If it doesn't, your model year differs from what this
project assumes and the layout in [PROTOCOL.md](PROTOCOL.md) needs revisiting.

## Configuration

All tunables live in [`config.h`](../config.h).

| Setting | Default | Meaning |
|---|---|---|
| `MCP_CRYSTAL` | `MCP_8MHZ` | **Must match your board.** |
| `MCP_BITRATE` | `CAN_500KBPS` | MQB Komfort/Infotainment rate |
| `MCP_CS_PIN` | 10 | SPI chip select |
| `BUTTON_PIN` | 4 | The button |
| `BUTTON_LONG_MS` | 700 | Held this long, a press is a long one |
| `BUTTON_REPEAT_MS` | 300 | Step interval while the button stays held |
| `MENU_TIMEOUT_MS` | 20000 | An idle menu returns to the home screen |
| `STATUS_LED_PIN` | 6 | Klemme 15 LED, with `ENABLE_STATUS_LED 1` |
| `KLEMMEN_CYCLE_MS` | 100 | Do not change; receivers fault on drift |
| `DIMMUNG_CYCLE_MS` | 500 | Assumed, verify by sniffing |
| `DIAGNOSE_CYCLE_MS` | 1000 | Assumed, verify by sniffing |
| `KL50_PULSE_MS` | 800 | Starter request pulse length |
| `ENABLE_MIB_MESSAGES` | 1 | Send `0x5F0` and `0x6B2` |
| `MIB_YIELD_MS` | 5000 | Withhold either of them this long after another node sent it |
| `ENABLE_VIN` | 1 | The `V` command: `VIN_01` (`0x6B4`) with a VIN typed on the console. Needs the serial console. |
| `VIN_CYCLE_MS` | 200 | One of its three frames this often. Assumed, not measured. |
| `VIN_ENTRY_MS` | 5000 | A VIN left half typed is given up after this |
| `HEALTH_CHECK_MS` | 1000 | How often the controller's mode is checked |
| `BUS_RETRY_MS` | 3000 | How often a controller that does not answer is tried again |
| `ENABLE_SERIAL_CONTROL` | 1 | Serial console; disable to save flash |
| `LISTEN_ONLY_DEFAULT` | 0 | Come up transmitting nothing; for a real car. With `REMEMBER_STATE`, only until a state has been stored. |
| `REMEMBER_STATE` | 1 | Keep Klemme 15, Klemme X and listen-only across resets and power cuts |
| `STATE_EEPROM_ADDR` | 0 | First of the three EEPROM bytes used for it |
| `ENABLE_LCD` | 1 | 16×2 LCD on the analog header |
| `LCD_RS_PIN` … `LCD_D7_PIN` | A0–A5 | RS, E, D4, D5, D6, D7 |
| `LCD_REFRESH_MS` | 250 | Display refresh period |
| `ENABLE_STATUS_LED` | 0 | LED on `STATUS_LED_PIN` showing Klemme 15 |
| `SNIFFER_MAX_IDS` | 24 | RAM cost is 12 bytes each |
| `ENABLE_VOLTAGE` | 1 | Show the supply voltage read from the bus |
| `VOLTAGE_CAN_ID`, `VOLTAGE_BYTE` | `0x663`, 0 | Where the voltage is read. A candidate until confirmed. |
| `VOLTAGE_MV_PER_BIT`, `VOLTAGE_OFFSET_MV` | 100, 0 | Its scaling |
| `VOLTAGE_VERIFIED` | 0 | 1 removes the `?` in front of the value |
| `VOLTAGE_LOW_DV` | 115 | Below this, in tenths of a volt, the battery icon blinks |
| `TESTER_HOLD_MS` | 3000 | How long a diagnostic frame keeps the plug icon on |
| `NM_MAX_NODES`, `NM_AWAKE_MS` | 6, 3000 | Size and memory of the who-is-awake list |
| `LEARN_BASELINE_MS` | 3000 | The learning view's settling time on a page |

To set the MIB's starting clock, edit the defaults in `ClockState`
(`mqb_mib.h`). There is no RTC, so the time restarts from those values on every
power-up and drifts with the AVR's ceramic resonator.

## The simulator

```sh
./sim/run.sh
```

builds the simulator and serves it at <http://127.0.0.1:8515/>, on this
machine only. It needs only `g++` and `python3`: no board, no arduino-cli and
no Arduino libraries.

What runs is the sketch itself, not a copy of its logic: `can-emulator.ino`
gets the function prototypes an Arduino build would add, and `g++` compiles
the result against the stand-ins in `sim/mock/`. So the menu, the icons, the counters and the timing
of the display are the firmware's own. What is simulated is everything around
it:

| On the page | What it stands for |
|---|---|
| The display | The LCD's memory, drawn dot by dot, with the four icons the sketch defines |
| **Button** | The push button on D4. Click is a short press, holding it a long one; so is the space bar. |
| **Reset** | A reset of the Uno. What it stored in EEPROM survives, so the ignition comes back. |
| **Erase & reset** | A board that has never stored anything |
| Gateway J533 | The IDs and rates measured on the bench, including `0x6B2` and network management as node `10` |
| Radio | An invented unit: network management as node `5F` and a frame `3E5` whose byte 2 follows the knob slider. For the who-is-awake page and the learning view. |
| Diagnostic tester | `0x700` every 2 s |
| Car's own ignition | Another sender of `0x3C0` and `0x5F0`, for trying listen-only mode |
| MCP2515 has 5 V | Off is the module's supply wire pulled out |
| Supply voltage | Byte 0 of `0x663`, where the sketch reads the voltage |
| Frames on the bus | Every frame the sketch sent and every frame the bench sends it, each with what it is, a count and its latest payload |

Things to try: switch everything on the bus off and watch `e` count up; pull
the module's 5 V and put it back; open the bus monitor, go to `3E5`, wait for
`learn` to end and move the knob.

The simulated bus is honest about one limit of the real board: the MCP2515
holds two received frames, writing a character to the LCD takes 0.3 ms during
which nothing is fetched, and frames that arrive meanwhile are lost.

`./sim/smoke.sh` runs the sketch through its screens without the page and
checks what the display shows; `./check.sh` includes it. For a scripted look
at one screen, drive the simulator directly:

```sh
printf 'run 1500\npress long\npress short\n' | ./sim/build/sim --text
```

The glyphs are a generic 5×8 pixel font, not the HD44780's own character ROM,
so letter shapes differ slightly from the real display. Where the display's
character set differs from ASCII the simulator follows the display: `~` is a
right arrow there and `\` a yen sign.

## Tests

```sh
./test/run.sh
```

Runs on the host, no board required. It checks the checksum against 16
reference values captured from a real gateway, round-trips every `Dimmung_01`
and `Diagnose_01` signal through the bit packer, and verifies that maxed-out
fields don't bleed into their neighbours.

The same checksum self-test runs on the Arduino at boot.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `MCP2515 init: FAILED` | SPI wiring, wrong `MCP_CS_PIN`, or module unpowered. Check 5 V at the module's VCC. It is tried again every 3 s, so fixing the wire is enough. |
| `MCP2515 re-init` lines, `re-inits` climbing | The module is losing power or an SPI wire is loose. |
| Init OK, `tx err` climbs, no responses | Wrong crystal setting. Or: no other node on the bus. A CAN transmitter needs at least one other node to ACK — with nothing else connected, every frame errors. This is normal and not a fault. |
| Init OK, `tx ok` climbs, module ignores it | Wrong bus, or the module has no 12 V on Klemme 30. Verify with a meter before suspecting CAN. |
| `tx ok` and `tx err` both stay at 0 | Listen-only is on; `s` shows the mode, `l` leaves it. It is remembered across resets, so a reset does not leave it. |
| Module boots then sleeps | Wants additional cyclic messages. Sniff a real car. |
| Module reboots in a loop | Power supply sagging under load, not a CAN problem. |
| Erratic comms, occasional frames | Termination. Measure CANH–CANL: should be ~60 Ω unpowered. |
| Garbage on serial | Monitor not at 115200. |
| LCD blank, or one row of boxes | Contrast: adjust V0 (trimmer, or 1–2 kΩ to GND). Boxes on one row only means it never initialised: check RS, E, D4–D7 and that RW is on GND. |
| Voltage shows `--.-V` | The frame in `VOLTAGE_CAN_ID` is not on this bus. Without a gateway there is none. |
| Voltage does not follow the supply | The candidate frame is wrong for this car: find the byte with the learning view. |
| LCD shows garbage | D4–D7 in the wrong order, or a loose E wire. |
| Clock wrong on MIB | Expected — starts from the `ClockState` defaults each boot. |
| Sniffer shows nothing | CANH/CANL swapped, or wrong bus, or bitrate mismatch. |
