# VW MQB Klemme 15 emulator

Arduino Uno + MCP2515 firmware that broadcasts the VW MQB `Klemmen_Status_01`
CAN frame, so modules on a bench harness believe the ignition is on without a
body control module, steering column module (J527) or immobiliser present.

Ignition is toggled with a push button on the Arduino or over a serial console.
A built-in sniffer lets you capture what a real car sends and check your
assumptions against it.

```
   ┌─────────┐  SPI   ┌──────────┐  CAN   ┌──────────────────┐
   │ Arduino ├───────►│ MCP2515  ├───────►│ module under test│
   │   Uno   │        │ +TJA1050 │        │  (MIB, cluster…) │
   └────┬────┘        └──────────┘        └────────┬─────────┘
        │ button = Klemme 15                       │ Klemme 30
     ───┴───                                   ────┴──── 12 V PSU
```

## Documentation

| Document | Contents |
|---|---|
| [docs/HARDWARE.md](docs/HARDWARE.md) | Bill of materials, wiring, termination, grounding, powering the module, safety |
| [docs/USAGE.md](docs/USAGE.md) | Toolchain setup, serial commands, workflows, configuration, troubleshooting |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | Frame layouts, checksum algorithm and how it was derived, adding messages |

## Klemme 30 vs Klemme 15 — read this first

These are not the same kind of thing, and only one of them is a CAN signal:

| Terminal | What it is | How you provide it |
|---|---|---|
| **Kl. 30** | Permanent battery +12 V, always live | **Physical wiring only.** There is no CAN signal for it — you must actually feed 12 V to the module. |
| **Kl. 15** | "Ignition on" | On MQB the BCM generates the physical T15 rail, but modules act on the **CAN broadcast**. That is what this project sends. |

So: **supply Kl. 30 from a bench PSU, emulate Kl. 15 with this sketch.** Most
MQB modules will not power up on the CAN message alone.

## Messages sent

| ID | Name | DLC | Cycle | Purpose |
|---|---|---|---|---|
| `0x3C0` | `Klemmen_Status_01` | 4 | 100 ms | Terminal status (S, 15, X, 50). Always sent. |
| `0x5F0` | `Dimmung_01` | 8 | 500 ms | Display/panel dimming. Sent while Kl. 15 on. |
| `0x6B2` | `Diagnose_01` | 8 | 1000 ms | Odometer + date/time (the MIB clock). Sent while Kl. 15 on. |

The two MIB support messages are switched off with `ENABLE_MIB_MESSAGES` in
`config.h`. Their cycle times are plausible defaults rather than measured
values — see [PROTOCOL.md](docs/PROTOCOL.md#cycle-times).

## The checksum

Byte 0 of `0x3C0` is **CRC-8, polynomial `0x2F`, init `0xFF`, no reflection,
final XOR `0xFF`**, over bytes 1–3 followed by the magic byte **`0xC3`**.

Published bench setups hardcode a 16-entry table of checksums instead. That
table only covers one terminal state — ignition permanently on. Deriving the
algorithm (all 16 published values reduce to the single magic byte `0xC3`) means
this project can also send a valid *ignition off* frame, and drive Klemme X and
Klemme 50. The derivation is written up in
[PROTOCOL.md](docs/PROTOCOL.md#how-the-magic-byte-was-determined).

## Quick start

1. Check the crystal on your MCP2515 board and set `MCP_CRYSTAL` in
   [`config.h`](config.h) to match. Ships configured for 8 MHz. **Getting this
   wrong is the most common failure** — init reports OK and nothing works.
2. Wire it up per [HARDWARE.md](docs/HARDWARE.md): SPI to the Uno, button from
   D4 to GND, LED on D6, common ground with the module under test.
3. Install the **MCP_CAN** library by Cory J. Fowler.
4. Upload `can-emulator.ino` and open the serial monitor at **115200 baud**.
5. Press the button, or send `1`, to switch Klemme 15 on.

## Tests

```sh
./check.sh        # host tests + a real AVR compile
./test/run.sh     # host tests only, no toolchain needed
```

Host-side, no hardware needed. Checks the checksum against 16 reference values
captured from a real gateway, round-trips every `Dimmung_01` and `Diagnose_01`
signal through the bit packer, and verifies maxed-out fields don't bleed into
their neighbours. The same checksum self-test also runs on the Arduino at boot.

## Layout

```
check.sh             host tests + AVR compile; run this to verify a change
can-emulator.ino     scheduler, button, serial console, sniffer, MCP2515 setup
config.h             all pin, timing and bitrate constants
mqb_klemmen.{h,cpp}  Klemmen_Status_01 (0x3C0) and the checksum
mqb_mib.{h,cpp}      Dimmung_01, Diagnose_01, generic little-endian bit packer
test/                host test harness with an Arduino.h shim
docs/                hardware, usage and protocol documentation
```

The `mqb_*` files are free of Arduino dependencies so they stay host-testable.

## Safety

Do **not** inject `0x3C0` into a complete, running vehicle — the real gateway
already transmits that ID, and two nodes sending the same ID with different
payloads causes error frames and can bus-off nodes. On a real car, use sniffer
mode only. Full notes in
[HARDWARE.md](docs/HARDWARE.md#safety-and-legal-notes).

## Sources

- [Ross-Tech forum thread 5918](https://forums.ross-tech.com/index.php?threads/5918/page-10)
  — frame layout and reference checksums.
- [`vw_mqb_2010.dbc`](https://github.com/RoosterIllusion/opendbc-7.x/blob/master/vw_mqb_2010.dbc)
  — signal definitions, from commaai/opendbc.

Not affiliated with or endorsed by Volkswagen AG. "MQB", "MIB" and VW part
designations are used descriptively.

## License

MIT — see [LICENSE](LICENSE).
