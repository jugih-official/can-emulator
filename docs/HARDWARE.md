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

## Connecting to the vehicle bus

### On a bench harness (the intended use)

Tap CAN-High and CAN-Low of the bus the module sits on — normally at the
module's own connector. If the harness runs to the gateway **J533** instead,
its 20-pin connector **T20e** carries every bus. From the VW workshop
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
you have to supply it physically.

```
 Bench PSU 12 V ──[ 10-15 A fuse ]──► module Kl. 30 pin
 Bench PSU GND  ─────────────────┬──► module ground pin
                                 └──► Arduino GND / MCP2515 GND
```

Use a supply that can hold 12–14 V under load. A MIB head unit drawing 5 A from
a 2 A wall wart will brown out and reboot in a loop, which looks exactly like a
CAN problem and isn't.

![The bench: PC, Uno, MCP2515, module under test and the 12 V supply](img/bench-overview.svg)

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
