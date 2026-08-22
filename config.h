#pragma once
// ---------------------------------------------------------------------------
// Hardware / behaviour configuration for the VW MQB Klemme 15 emulator.
// ---------------------------------------------------------------------------

// --- MCP2515 wiring ---------------------------------------------------------
// SPI is fixed on the Uno: SCK=D13, MISO=D12, MOSI=D11.
#define MCP_CS_PIN        10
#define MCP_INT_PIN        2   // not used for TX-only, wired for future RX use

// Crystal fitted to YOUR MCP2515 board. Getting this wrong is the single most
// common reason nothing appears on the bus: the controller comes up happily but
// every frame goes out at the wrong bitrate. Blue "niren" boards are usually
// 8 MHz, black boards are usually 16 MHz. Read the can on the crystal.
#define MCP_CRYSTAL       MCP_8MHZ   // or MCP_16MHZ

// MQB Komfort- / Infotainment-CAN runs at 500 kbit/s.
#define MCP_BITRATE       CAN_500KBPS

// --- Human interface --------------------------------------------------------
// The Uno has no user button, only RESET. Wire a momentary push button between
// this pin and GND; the internal pull-up handles the rest (no resistor needed).
#define BUTTON_PIN         4
#define BUTTON_DEBOUNCE_MS 40

// Deliberately NOT LED_BUILTIN: on the Uno that is D13, which is also the SPI
// clock driving the MCP2515. Wire an LED + ~330R from this pin to GND instead.
#define STATUS_LED_PIN     6

// --- Timing -----------------------------------------------------------------
// Klemmen_Status_01 is a 100 ms cyclic message. Drift beyond roughly +/-10%
// makes some gateways flag the sender as faulty, so keep this at 100.
#define KLEMMEN_CYCLE_MS   100

// Hold Klemme 50 (starter request) high for this long when pulsed.
#define KL50_PULSE_MS      800

// Cycle times for the MIB support messages. The DBC carries no cycle-time
// attributes, so these are typical values rather than confirmed ones -- use
// sniffer mode ('r' then 'p') against a real car to check them.
#define DIMMUNG_CYCLE_MS   500
#define DIAGNOSE_CYCLE_MS  1000

// --- Options ----------------------------------------------------------------
// Also send Dimmung_01 (0x5F0) and Diagnose_01 (0x6B2), which a MIB head unit
// wants for display dimming and its clock. Sent only while Klemme 15 is on.
#define ENABLE_MIB_MESSAGES   1

// Sniffer mode: how many distinct CAN IDs to keep statistics for. Each entry
// costs 12 bytes of RAM, and the Uno only has 2 KB.
#define SNIFFER_MAX_IDS      24
// Serial console on 115200 baud for control and diagnostics.
#define ENABLE_SERIAL_CONTROL 1

// Print every transmitted frame to serial. Very noisy at 10 Hz -- toggle at
// runtime with 'd' instead of enabling permanently.
#define TRACE_TX_DEFAULT      0
