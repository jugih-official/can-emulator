# Hardware and connections

## Bill of materials

| Qty | Part | Notes |
|---|---|---|
| 1 | Arduino Uno R3 | Any AVR Uno-compatible works. Nano/Pro Mini need pin changes in `config.h`. |
| 1 | USB A–B cable | Powers the Uno and carries the serial console. |
| 1 | MCP2515 CAN module with TJA1050 transceiver | The common blue "niren" or black board. **Check the crystal.** |
| 1 | Momentary push button | Normally-open, any tactile switch. |
| 1 | 16×2 character LCD, 1602A type | The plain 16-pin parallel module, no I²C backpack. Optional. |
| 1 | 10 kΩ trimmer | LCD contrast. A fixed 1–2 kΩ from V0 to GND does on most modules. |
| 1 | 220 Ω resistor | LCD backlight. |
| 1 | Gateway J533 with its T20e connector or pigtail | The permanent centre of the bench: see [The bench with a gateway](#the-bench-with-a-gateway). |
| 1 | OBD-II socket (16-pin, female) | For the diagnostic tester. |
| 1 | Bench PSU, 12 V, ≥10 A | For Klemme 30 to the module under test. A MIB unit can pull 5 A+. |
| — | Jumper wires, 0.5 mm² wire for the 12 V side | |
| 1 | Inline fuse, 10–15 A | On the 12 V feed. Not optional. |

## Identify your crystal first

This is the single most common reason a build appears dead. Look at the silver
oblong can on the MCP2515 board:

- Marked `8.000` / `8M` → 8 MHz → `#define MCP_CRYSTAL MCP_8MHZ`
- Marked `16.000` / `16M` → 16 MHz → `#define MCP_CRYSTAL MCP_16MHZ`

Blue "niren" boards are usually 8 MHz; black boards usually 16 MHz — but check,
don't assume. With the wrong setting the sketch reports `MCP2515 init: ok` and
happily transmits, just at the wrong bitrate, so nothing ever answers.

The project ships configured for **8 MHz**.

## Wiring

![Arduino Uno R3 to MCP2515 module, every wire](img/wiring-uno-mcp2515.svg)

The Uno is drawn to scale with the USB to the left, the 16×2 LCD at half that
scale under the analog header it hangs from. The module is the common blue or
black MCP2515 + TJA1050 board: a 7-pin header that reads
`INT SCK SI SO CS GND VCC`, a `CAN H` / `CAN L` screw terminal, the crystal and
the `J1` termination jumper. Go by the printed names if your board's header is
ordered differently. The same wiring in text:

```
        Arduino Uno                      MCP2515 module
       ┌──────────────┐                 ┌────────────────┐
       │          5V  ├────────────────►┤ VCC            │
       │         GND  ├────────────────►┤ GND       CANH ├──── to CAN-High
       │         D10  ├────────────────►┤ CS        CANL ├──── to CAN-Low
       │  D11 (MOSI)  ├────────────────►┤ SI             │
       │  D12 (MISO)  ◄────────────────┤ SO             │
       │   D13 (SCK)  ├────────────────►┤ SCK            │
       │          D2  ◄────────────────┤ INT            │
       │              │                 └────────────────┘
       │          D4  ├───[ button ]───┐
       │         GND  ├─────────────────┘
       │              │                  1602A 16x2 LCD
       │          A0  ├────────────────► RS
       │          A1  ├────────────────► E
       │       A2–A5  ├────────────────► D4–D7           (D0–D3 open)
       │          5V  ├────────────────► VDD, and A via 220 Ω
       │         GND  ├────────────────► VSS, RW, K
       │              │   trimmer wiper ► V0  (trimmer ends to 5V and GND)
       └──────────────┘
```

### SPI — fixed by the Uno hardware

| MCP2515 | Uno | Direction |
|---|---|---|
| VCC | 5 V | — |
| GND | GND | — |
| CS  | D10 | Uno → MCP (configurable, `MCP_CS_PIN`) |
| SI  | D11 (MOSI) | Uno → MCP |
| SO  | D12 (MISO) | MCP → Uno |
| SCK | D13 | Uno → MCP |
| INT | D2 | MCP → Uno (`MCP_INT_PIN`) |

D11/D12/D13 are the Uno's hardware SPI pins and cannot be moved.

### Controls and display

| Part | Connection |
|---|---|
| Button | D4 → switch → GND |
| 16×2 LCD | A0 → RS, A1 → E, A2–A5 → D4–D7. VSS, RW, K → GND; VDD → 5V; A → 5V via 220 Ω; V0 → contrast; D0–D3 open |

The button uses the AVR's internal pull-up (`INPUT_PULLUP`), so **no external
resistor is needed** — just a bare switch to ground. Pressed reads LOW. A short
press toggles Klemme 15 and a long one opens the menu on the LCD.

The LCD is the plain 16-pin parallel 1602A (HD44780 type) in 4-bit mode, driven
by the standard `LiquidCrystal` library (the Arduino IDE ships it; arduino-cli
needs `lib install LiquidCrystal`). The
six signal wires drop from one row of the analog header, which the sketch uses
as digital outputs. Contrast on V0 is best set with a 10 kΩ trimmer between 5V
and GND, wiper to V0; a fixed 1–2 kΩ from V0 to GND is readable on most of
these modules, while V0 straight to GND usually gives ghost boxes. The
backlight anode A takes 5 V through 220 Ω (some modules carry that resistor on
board; the external one never hurts) and K goes to GND. The LCD is optional:
`ENABLE_LCD 0` builds without it.

An LED showing Klemme 15 is still possible on **D6** (`ENABLE_STATUS_LED 1`;
anode to D6, cathode → 330 Ω → GND). Not D13: on the Uno the built-in LED
shares D13 with the SPI clock, so it flickers with bus traffic and can't be
driven independently.

## The bench with a gateway

A gateway **J533** is a permanent part of this bench. Three things depend on
it:

- **Diagnosis and flashing.** A tester on the OBD socket talks on the
  diagnostic CAN only. The gateway carries its requests to the bus the
  addressed control unit sits on and brings the answers back. Without it the
  socket reaches nothing.
- **Ignition.** The gateway is the unit that has to see terminal 15. It takes
  it from `Klemmen_Status_01` on the convenience bus, which is where the
  emulator goes.
- **A living network.** It sends the cyclic traffic and network management
  that control units expect, including the clock message `0x6B2`, so the
  emulator stands back with its own copy.

![The bench around a J533 gateway](img/bench-gateway.svg)

### What goes where

| Part | Gateway pins (T20e) | Notes |
|---|---|---|
| +12 V, terminal 30 | 1, 2 | Through the fuse |
| Ground, terminal 31 | 11, 12 | Common with the Arduino's GND |
| Terminal 15, hard-wired | 14 | Optional, see below |
| Emulator, CAN H / CAN L | 15 / 5 (convenience) | J1 on the MCP2515 module fitted |
| OBD socket pin 6 / pin 14 | 19 / 9 (diagnostic) | Socket pin 16 to +12 V, pins 4 and 5 to ground |
| Radio or head unit | 20 / 10 (infotainment) | |
| Other control units | 16 / 6 powertrain, 17 / 7 extended, 18 / 8 running gear | On the bus they use in the car |

Each unit under test also needs its own terminal 30 and 31 from the supply.

**OBD socket.** Pin 6 is CAN high and pin 14 CAN low, pin 16 permanent +12 V,
pins 4 and 5 ground. On VW cars pin 1 carries switched terminal 15, and some
interfaces read the ignition from it: if a tester insists the ignition is off
while ODIS shows it on, put +12 V on pin 1 as well.

**Terminal 15 on pin 14.** ODIS shows two values in the gateway: *Terminal 15
status via CAN* (`$02B2`) follows the emulator, on and off. *Terminal 15
status* (`$0288`) read "on" throughout on this bench; that is presumably the
hard-wired input, which has not been checked with pin 14 open.

### What has been checked, and what has not

Checked on this bench:

- The gateway acknowledges every emulator frame on the convenience bus, with
  no transmit errors.
- ODIS, on the OBD socket, reads *Terminal 15 status via CAN* on and off as the
  emulator's button is pressed.
- The tester's "tester present" (`0x700`, every 2 s) appears on the convenience
  bus, so the gateway routes diagnostic requests across.
- The gateway sends `0x6B2` every 1000 ms.
- The gateway acknowledges `VIN_01` (`0x6B4`) sent by the emulator on the
  convenience bus, with no transmit errors, and sends no `0x6B4` there itself,
  before or while the emulator does (2026-10-11, a made-up VIN,
  `WVWZZZ3GZJE000000`).
- ODIS on the OBD socket does not pick that VIN up when it loads a vehicle
  project (the same day, with the VIN being sent throughout).

Not checked yet:

- The supply voltage on the display. It is read from byte 5 of `0x663` at
  0.05 V per bit plus 5 V. On 2026-10-11 that byte flickered between `9A` and
  `9B`, 12.70 and 12.75 V, while ODIS showed the gateway's measured value
  *Battery voltage* (`$2A07`) as 12.720 V. That is agreement at one voltage,
  to within one step; it is settled when the supply is turned and the byte
  follows. Two things to keep in mind until then: the same ODIS page showed a
  power value of 155 A, and `9B` is 155, though a fixed value would not
  flicker; and the bench supply's own display said 12.49 V that day, a quarter
  of a volt under what the gateway measures. Byte 0 of the frame, the first
  guess, is a constant `0x78`.
- Whether the gateway repeats terminal status onto the infotainment bus, so
  that a radio there wakes with the emulator on the convenience bus. The bus
  monitor on pins 10 / 20 answers it: look for `3C0`.
- Flashing a control unit end to end.
- Whether the gateway passes a `VIN_01` sent on the convenience bus on to its
  other buses, and whether a control unit there makes any use of it. On a car
  the gateway is that message's sender.

### Before flashing a control unit

- **Keep the ignition on for the whole session.** The emulator remembers
  Klemme 15 across a reset or a power cut (`REMEMBER_STATE`) and comes back
  with it on, but a reset still stops `0x3C0` for about two seconds while the
  Uno's bootloader runs. Opening the serial port from a PC is such a reset:
  during a flash, power the Uno from a USB charger or leave the port alone.
- **Hold the supply steady.** A voltage dip in the middle of a flash is the
  usual way to lose a control unit. Use a supply with headroom, not a wall
  wart.
- **Leave the emulator in normal mode.** In listen-only it sends no ignition.
- **Keep the serial dump (`r`) off.** It slows the sketch down for nothing.

### What the gateway sends

Measured with the emulator's sniffer on the convenience bus, gateway alone:

| ID | Cycle |
|---|---|
| `3DC` | 50 ms |
| `5AC`, `663`, `48B`, `3DA`, `3EB` | 100 ms |
| `569` | 500 ms |
| `585`, `6B2`, `386`, `5F5`, `5F7` | 1000 ms |
| `1B000010` (29-bit, network management) | 200 ms |
| `17332310` (29-bit) | about 1000 ms |

On 2026-10-11 the same list came back with a tester on the OBD socket and an
exterior-sound control unit (diagnostic address `C0`) on the powertrain pair,
with one more ID: `67C` every 1000 ms, payload `00 90 80 00 00 00 00 00`.
Which of the two brought it is not known. Switching the ignition on changed
byte 5 of the gateway's network-management frame from `00` to `10` and
brought one `17FC0114` (29-bit); no 11-bit ID came or went.

The control unit itself did not show, ignition off or on: no frame of its own
and only `10` in the awake list. The sniffer and the awake list see the pair
the emulator is wired to and nothing behind the gateway.

On the diagnostic pair it sends only its network-management frame until a
tester speaks. That bus is the tester's: the emulator has no business
transmitting there, and with nothing else on the pair to acknowledge, the
gateway repeats one frame without pause.

## Connecting to the vehicle bus

### Gateway pins

The gateway's 20-pin connector **T20e** carries every bus. From the VW workshop
illustration reproduced in the
[openpilot wiki](https://github.com/commaai/openpilot/wiki/VW-J533-(Gateway)-Cable):

| Bus | CAN-Low | CAN-High |
|---|---|---|
| Convenience (Komfort) | 5 | 15 |
| Powertrain (Antrieb) | 6 | 16 |
| Extended | 7 | 17 |
| Running gear sensors | 8 | 18 |
| Diagnostic | 9 | 19 |
| Infotainment | 10 | 20 |

Pins 1–2 are terminal 30, 11–12 terminal 31, 14 terminal 15, and 3 / 13 are
the LIN buses to J367 and J527. Low is on 5–10, high on 15–20; an earlier
version of this page had the Komfort pair the other way round.

VW CAN wire colours are typically orange with a coloured stripe:

| Bus | CAN-High | CAN-Low |
|---|---|---|
| Komfort | orange/green | orange/brown |
| Infotainment | orange/violet | orange/brown |
| Antrieb (powertrain) | orange/black | orange/brown |

Colours vary by model year — verify against the wiring diagram for your car
rather than trusting the stripe alone.

### Grounding

The Arduino, the MCP2515 and the module under test **must share a common
ground**. CAN is differential but still referenced to ground; a floating
Arduino ground produces intermittent, baffling errors.

Powering the Arduino from a laptop's USB while the module runs off a bench PSU
is fine *provided* PSU negative and Arduino GND are bonded.

### Termination

A CAN bus needs **exactly two 120 Ω terminators, one at each physical end**.

Most MCP2515 modules ship with a 120 Ω resistor fitted, often selectable by a
jumper (`J1`) — some boards require desoldering it.

- Bench harness with no other termination → leave the module's resistor in.
- Harness already terminated at both ends → **remove/disable** the module's
  resistor, or the bus sees ~60 Ω and comms become unreliable.

Check by measuring resistance across CANH–CANL with everything powered off: you
want to read about **60 Ω** (two 120 Ω in parallel). 120 Ω means one terminator
is missing; 40 Ω means you have three.

## Powering the module under test

Klemme 30 is permanent battery voltage and **cannot be emulated over CAN** —
you have to supply it physically. The same goes for the gateway, on pins 1–2
and 11–12.

```
 Bench PSU 12 V ──[ 10-15 A fuse ]──► module Kl. 30 pin
 Bench PSU GND  ─────────────────┬──► module ground pin
                                 └──► Arduino GND / MCP2515 GND
```

Use a supply that can hold 12–14 V under load. A MIB head unit drawing 5 A from
a 2 A wall wart will brown out and reboot in a loop, which looks exactly like a
CAN problem and isn't.

Without a gateway, one module straight on the emulator:

![The bench without a gateway: PC, Uno, MCP2515, module under test and the 12 V supply](img/bench-overview.svg)

## Safety and legal notes

- **Do not inject `Klemmen_Status_01` into a complete, running vehicle.** The
  real gateway already transmits ID `0x3C0`. Two nodes sending the same ID with
  different payloads causes arbitration loss and error frames, and can bus-off
  nodes. `0x3C0` is sent continuously by design and cannot be switched off on
  its own, so on a real car put the board in **listen-only mode** first: send
  `l`, or build with `LISTEN_ONLY_DEFAULT 1` in `config.h`. The MCP2515 then
  neither transmits nor acknowledges, and the sniffer still sees everything.
- Never do this on a vehicle on a public road, or one that will be driven
  afterwards without verification.
- Disconnect the vehicle battery before making or breaking harness connections.
- Airbag (SRS) modules can deploy pyrotechnics. Do not bench-power an airbag
  control unit with squibs connected.
- This is intended for bench testing and for modules you own.
