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
// Held this long, a press opens the menu or acts on the menu item instead of
// being a short press; held on, it repeats at the second interval.
#define BUTTON_LONG_MS     700
#define BUTTON_REPEAT_MS   300
// A menu left alone returns to the home screen after this long.
#define MENU_TIMEOUT_MS    20000

// Klemme 15 indicator LED, optional and off by default now that the LCD shows
// the state. If wanted: an LED + ~330R from this pin to GND, and 1 here.
// Deliberately NOT LED_BUILTIN: on the Uno that is D13, which is also the SPI
// clock driving the MCP2515.
#define ENABLE_STATUS_LED  0
#define STATUS_LED_PIN     6

// A 1602-type 16x2 character display (HD44780, 4-bit mode) shows the terminal
// state and the counters, so the bench needs no laptop. Six pins in one row of
// the analog header; on the LCD, VSS/RW/K go to GND, VDD to 5 V, A to 5 V via
// 220R, V0 to the contrast trimmer, D0..D3 stay unconnected.
#define ENABLE_LCD         1
#define LCD_RS_PIN         A0
#define LCD_E_PIN          A1
#define LCD_D4_PIN         A2
#define LCD_D5_PIN         A3
#define LCD_D6_PIN         A4
#define LCD_D7_PIN         A5
// Refresh period. Only the characters that changed are written, about 0.3 ms
// each, so the 100 ms CAN cycle does not notice.
#define LCD_REFRESH_MS     250

// --- What is read from the bus -------------------------------------------------
// Supply voltage, shown permanently on the home screen. It is taken from one
// byte of a frame some unit broadcasts:  volts = (byte * MV_PER_BIT + OFFSET) / 1000.
// The default is a CANDIDATE, not a confirmed signal: byte 5 of 0x663, which a
// J533 gateway sends every 100 ms, at 0.05 V per bit plus 5 V. No public DBC
// names that frame; the scaling is the one the older PQ platform uses for its
// battery voltage. On the bench the byte flickered between 0x9A and 0x9B, which
// is 12.70 and 12.75 V, while the gateway's own measured value read 12.720 V.
// That is one voltage. Until the value has been seen to follow the supply up
// and down, leave VOLTAGE_VERIFIED at 0 and the display marks it with '?'.
// (Byte 0 of the same frame, the first guess, is a constant 0x78.)
#define ENABLE_VOLTAGE        1
#define VOLTAGE_CAN_ID        0x663
#define VOLTAGE_BYTE          5
#define VOLTAGE_MV_PER_BIT    50
#define VOLTAGE_OFFSET_MV     5000
#define VOLTAGE_VERIFIED      0
#define VOLTAGE_STALE_MS      3000    // no frame for this long: "--.-V"
#define VOLTAGE_LOW_DV        115     // below 11.5 V the battery icon blinks

// A diagnostic frame (0x700..0x7FF) keeps the tester icon on this long. A
// tester sends "tester present" every 2 s.
#define TESTER_HOLD_MS        3000

// Who is awake: units sending network management, 29-bit ID 0x1B0000xx with
// xx the unit's node address, about every 200 ms.
#define NM_ID_BASE            0x1B000000UL
#define NM_MAX_NODES          6
#define NM_AWAKE_MS           3000

// Learning view in the bus monitor: bytes that change during the first period
// on a page are taken as changing by themselves; afterwards a byte that
// changes blinks for this many display refreshes.
#define LEARN_BASELINE_MS     3000
#define LEARN_BLINK_REFRESHES 12

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

// How often the MCP2515 is asked whether it is still in the mode it should be
// in, and how often a controller that is not answering is tried again.
#define HEALTH_CHECK_MS    1000
#define BUS_RETRY_MS       3000

// --- Options ----------------------------------------------------------------
// Also send Dimmung_01 (0x5F0) and Diagnose_01 (0x6B2), which a MIB head unit
// wants for display dimming and its clock. Sent only while Klemme 15 is on.
#define ENABLE_MIB_MESSAGES   1
// If another node (a gateway) is heard sending one of those two IDs, ours is
// withheld until that one has been silent for this long.
#define MIB_YIELD_MS          5000

// VIN_01 (0x6B4): the vehicle identification number, in three frames sent in
// turn. On a car the gateway sends it. Here it starts when a VIN is typed on
// the console ('V' and the 17 characters), stops with 'V-', and is forgotten
// at a reset: a number made up for the bench must not come back by itself.
// Withheld like the two above while another node sends it. The DBC gives no
// cycle time for it, so this one is assumed. Needs the serial console.
#define ENABLE_VIN            1
#define VIN_CYCLE_MS          200
// A VIN left half typed is given up after this long.
#define VIN_ENTRY_MS          5000

// Remember Klemme 15, Klemme X and listen-only in EEPROM and come back up with
// them after a reset or a power cut. With 0 every start is ignition off.
#define REMEMBER_STATE        1
#define STATE_EEPROM_ADDR     0

// Come up in listen-only mode: receive and count frames but transmit nothing,
// not even the acknowledge bit. The only mode to use on a complete car, where
// the real gateway already sends 0x3C0. Toggled at runtime with 'l'. With
// REMEMBER_STATE this is only the default for a board that has stored nothing.
#define LISTEN_ONLY_DEFAULT   0

// Sniffer mode: how many distinct CAN IDs to keep statistics for. Each entry
// costs 12 bytes of RAM, and the Uno only has 2 KB.
#define SNIFFER_MAX_IDS      24
// Serial console on 115200 baud for control and diagnostics.
#define ENABLE_SERIAL_CONTROL 1

// Print every transmitted frame to serial. Very noisy at 10 Hz -- toggle at
// runtime with 'd' instead of enabling permanently.
#define TRACE_TX_DEFAULT      0
