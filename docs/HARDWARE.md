# Hardware and connections

## Bill of materials

| Qty | Part | Notes |
|---|---|---|
| 1 | Arduino Uno R3 | Any AVR Uno-compatible works. Nano/Pro Mini need pin changes in `config.h`. |
| 1 | MCP2515 CAN module with TJA1050 transceiver | The common blue "niren" or black board. **Check the crystal.** |
| 1 | Momentary push button | Normally-open, any tactile switch. |
| 1 | LED + 330 Ω resistor | Optional, shows Klemme 15 state. |
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
       │              │                 │
       │          D6  ├───[LED]─[330Ω]─┤
       │         GND  ├─────────────────┘
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

### Controls

| Part | Connection |
|---|---|
| Button | D4 → switch → GND |
| LED | D6 → LED anode; cathode → 330 Ω → GND |

The button uses the AVR's internal pull-up (`INPUT_PULLUP`), so **no external
resistor is needed** — just a bare switch to ground. Pressed reads LOW.

The LED is on **D6, not D13**. On the Uno the built-in LED shares D13 with the
SPI clock, so it flickers with bus traffic and can't be driven independently.

## Connecting to the vehicle bus

### On a bench harness (the intended use)

Tap CAN-High and CAN-Low from the gateway connector on your harness. On the MQB
gateway **J533**, that is **pin 5 = CAN-High** and **pin 15 = CAN-Low**.

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

## Safety and legal notes

- **Do not inject `Klemmen_Status_01` into a complete, running vehicle.** The
  real gateway already transmits ID `0x3C0`. Two nodes sending the same ID with
  different payloads causes arbitration loss and error frames, and can bus-off
  nodes. On a real car, use **sniffer mode only** (`ENABLE_MIB_MESSAGES 0` and
  don't transmit) or an MCP2515 in listen-only mode.
- Never do this on a vehicle on a public road, or one that will be driven
  afterwards without verification.
- Disconnect the vehicle battery before making or breaking harness connections.
- Airbag (SRS) modules can deploy pyrotechnics. Do not bench-power an airbag
  control unit with squibs connected.
- This is intended for bench testing and for modules you own.
