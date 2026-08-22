# Usage

## Installing the toolchain

### Arduino IDE

1. **Install the CAN library.** *Tools → Manage Libraries…*, search `mcp_can`,
   install **"MCP_CAN Library" by Cory J. Fowler**. There are several similarly
   named libraries — the API used here (`CAN.begin(MCP_ANY, ...)`,
   `CAN.setMode(MCP_NORMAL)`) is his.
2. Open `can-emulator.ino`. The containing folder must stay named
   `can-emulator` so it matches the sketch filename.
3. *Tools → Board → Arduino Uno*, pick the port, and upload.

### arduino-cli

```sh
arduino-cli core install arduino:avr
arduino-cli lib install "mcp_can"
arduino-cli compile --fqbn arduino:avr:uno .
arduino-cli upload  --fqbn arduino:avr:uno -p /dev/ttyACM0 .
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

On Linux, add yourself to the serial group once if uploads fail with a
permission error, then log out and back in:

```sh
sudo usermod -aG dialout "$USER"
```

## First run

Open the serial monitor at **115200 baud**. You should see:

```
VW MQB Klemme 15 emulator
checksum self-test: PASS
MCP2515 init: ok
1/0 ign on/off   t toggle ign   x toggle Kl X   5 pulse Kl 50
s status   d toggle tx trace   r toggle rx dump   p rx stats
c clear rx stats   +/- dimming   h help
```

Both `PASS` and `ok` are required.

- `checksum self-test: FAIL` — the code has been modified incorrectly. Run
  `./test/run.sh` to see which case broke.
- `MCP2515 init: FAILED` — SPI wiring or power. See troubleshooting below.

`Klemmen_Status_01` transmits immediately at 10 Hz whether ignition is on or
off. That is deliberate: a real gateway does the same, and it is what keeps the
bus awake.

## Serial commands

| Key | Action |
|---|---|
| `1` | Klemme 15 on (ignition on) |
| `0` | Klemme 15 off |
| `t` | Toggle Klemme 15 — same as the button |
| `x` | Toggle Klemme X (load shedding) |
| `5` | Pulse Klemme 50 (starter request) for `KL50_PULSE_MS` |
| `+` / `-` | Display brightness up / down, in steps of 10 |
| `s` | Status: terminal states, clock, TX counters |
| `d` | Toggle per-frame transmit trace |
| `r` | Toggle received-frame dump |
| `p` | Print per-ID receive statistics |
| `c` | Clear receive statistics |
| `h` | Help |

Klemme 50 is refused while ignition is off — you cannot crank without T15, and
neither can the real car.

## The button

The push button on **D4** toggles Klemme 15, exactly like `t`. The LED on **D6**
follows the state. Debounce is 40 ms (`BUTTON_DEBOUNCE_MS`).

The Uno has no usable built-in button — RESET is the only one on the board — so
this needs a switch wired between D4 and GND. See [HARDWARE.md](HARDWARE.md).

## Typical workflows

### Waking a MIB head unit on the bench

1. Wire the MCP2515 to the Infotainment-CAN, common ground everywhere.
2. Feed 12 V to the module's Klemme 30 pin through a fuse.
3. Power the Arduino. `Klemmen_Status_01` starts flowing at once.
4. Press the button, or send `1`. The unit should boot.
5. Send `s` and confirm `tx err` is not climbing.

If it boots and then sleeps after a few seconds, it wants cyclic messages this
project doesn't send yet. Sniff a real car to find out which — see below.

### Finding out what a module actually needs

Receive statistics are collected continuously, so you don't have to be dumping
frames to gather them.

1. Connect to a **real, working** car's bus, transmitting nothing.
2. Let it run for 30 seconds with the ignition on.
3. Press `p`:

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
| `BUTTON_PIN` | 4 | Ignition button |
| `STATUS_LED_PIN` | 6 | Klemme 15 indicator |
| `KLEMMEN_CYCLE_MS` | 100 | Do not change; receivers fault on drift |
| `DIMMUNG_CYCLE_MS` | 500 | Assumed, verify by sniffing |
| `DIAGNOSE_CYCLE_MS` | 1000 | Assumed, verify by sniffing |
| `KL50_PULSE_MS` | 800 | Starter request pulse length |
| `ENABLE_MIB_MESSAGES` | 1 | Send `0x5F0` and `0x6B2` |
| `ENABLE_SERIAL_CONTROL` | 1 | Serial console; disable to save flash |
| `SNIFFER_MAX_IDS` | 24 | RAM cost is 12 bytes each |

To set the MIB's starting clock, edit the defaults in `ClockState`
(`mqb_mib.h`). There is no RTC, so the time restarts from those values on every
power-up and drifts with the AVR's ceramic resonator.

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
| `MCP2515 init: FAILED` | SPI wiring, wrong `MCP_CS_PIN`, or module unpowered. Check 5 V at the module's VCC. |
| Init OK, `tx err` climbs, no responses | Wrong crystal setting. Or: no other node on the bus. A CAN transmitter needs at least one other node to ACK — with nothing else connected, every frame errors. This is normal and not a fault. |
| Init OK, `tx ok` climbs, module ignores it | Wrong bus, or the module has no 12 V on Klemme 30. Verify with a meter before suspecting CAN. |
| Module boots then sleeps | Wants additional cyclic messages. Sniff a real car. |
| Module reboots in a loop | Power supply sagging under load, not a CAN problem. |
| Erratic comms, occasional frames | Termination. Measure CANH–CANL: should be ~60 Ω unpowered. |
| Garbage on serial | Monitor not at 115200. |
| Clock wrong on MIB | Expected — starts from the `ClockState` defaults each boot. |
| Sniffer shows nothing | CANH/CANL swapped, or wrong bus, or bitrate mismatch. |
