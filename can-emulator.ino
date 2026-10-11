// ---------------------------------------------------------------------------
// VW MQB Klemme 15 emulator -- Arduino Uno + MCP2515
//
// Broadcasts Klemmen_Status_01 (0x3C0) at 100 ms onto the Komfort-/
// Infotainment-CAN so that modules on a bench harness see "ignition on"
// without a body control module, steering column module (J527) or immobiliser
// being present. Optionally also sends the dimming and clock messages a MIB
// head unit expects.
//
// A short press of the push button on BUTTON_PIN toggles ignition; a long one
// opens a menu on the 16x2 LCD for the other terminals, dimming, the clock, a
// bus monitor and listen-only mode. Serial console at 115200 gives the same
// control plus a sniffer. See README.md for wiring.
// ---------------------------------------------------------------------------
#include <EEPROM.h>
#include <SPI.h>
#include <mcp_can.h>

#include "config.h"
#include "mqb_klemmen.h"
#include "mqb_mib.h"
#include "mqb_vin.h"
#if ENABLE_LCD
#include <LiquidCrystal.h>
#endif

static MCP_CAN CAN(MCP_CS_PIN);

static KlemmenState g_state;
static uint8_t  g_counter = 0;
static bool     g_trace   = TRACE_TX_DEFAULT;
static bool     g_busOk   = false;
static bool     g_listenOnly = LISTEN_ONLY_DEFAULT;   // transmit nothing, not even ACK
static uint32_t g_txOk    = 0;
static uint32_t g_txErr   = 0;
static uint32_t g_rxCount = 0;
static uint32_t g_rxExtended = 0;   // 29-bit frames: counted, not in the ID table
static uint8_t  g_recoveries = 0;   // times the health check re-initialised the chip
static uint32_t g_nextHealthAt = 0;
static bool     g_selfTestOk = true;

#if ENABLE_MIB_MESSAGES
static DimmungState g_dimmung;
static ClockState   g_clock;
static uint32_t g_nextDimmungAt  = 0;
static uint32_t g_nextDiagnoseAt = 0;
static uint32_t g_nextClockAt    = 0;
#endif
// When something else on the bus last sent these IDs (0 = never). A gateway
// sends both itself; two senders of one ID with different payloads collide,
// so ours stand back for as long as the other one is heard.
static uint32_t g_dimmungHeardAt  = 0;
static uint32_t g_diagnoseHeardAt = 0;

#if ENABLE_VIN
#if !ENABLE_SERIAL_CONTROL
#error "ENABLE_VIN needs the serial console: the VIN is typed there"
#endif
// VIN_01. Off until a VIN is typed on the console, and not kept across a
// reset. Stands back like the two above while another node sends the ID.
static char     g_vin[MQB_VIN_LENGTH];
static bool     g_vinOn      = false;
static uint8_t  g_vinMux     = 0;
static uint8_t  g_vinTyped   = 0xFF;   // characters taken so far; 0xFF: none being typed
static uint32_t g_vinTypedAt = 0;
static uint32_t g_nextVinAt  = 0;
static uint32_t g_vinHeardAt = 0;
#endif

// --- what the bus tells us ---------------------------------------------------
// All "when last seen" stamps are millis() | 1, so that 0 can mean never.
static uint32_t g_testerAt  = 0;      // a diagnostic frame, 0x700..0x7FF
static uint32_t g_voltAt    = 0;      // the supply voltage frame
static uint8_t  g_voltDv    = 0;      // supply voltage in tenths of a volt
// Units heard sending network management, by the low byte of 0x1B0000xx.
static uint8_t  g_nmNode[NM_MAX_NODES];
static uint32_t g_nmSeenAt[NM_MAX_NODES];

static uint32_t g_nextKlemmenAt = 0;
static uint32_t g_kl50EndsAt    = 0;

// Button: debounce, and telling a short press from a held one.
static bool     g_btnStable    = HIGH;   // pull-up: HIGH == released
static bool     g_btnLast      = HIGH;
static uint32_t g_btnChangedAt = 0;
static uint32_t g_btnDownAt    = 0;
static bool     g_btnHeld      = false;  // this press already counted as a long one
static uint32_t g_btnRepeatAt  = 0;
enum : uint8_t { BTN_NONE, BTN_SHORT, BTN_LONG, BTN_REPEAT };

// --- sniffer ---------------------------------------------------------------
static bool g_sniff = false;
struct IdStat {
  uint16_t id;
  uint16_t count;
  uint32_t lastMs;
  uint16_t minDelta;
  uint16_t maxDelta;
};
static IdStat g_stats[SNIFFER_MAX_IDS];
static uint8_t g_statCount = 0;
static bool    g_statsFull = false;

#if ENABLE_LCD
// --- one-button menu ---------------------------------------------------------
enum : uint8_t { SCR_HOME, SCR_MENU, SCR_MONITOR };
enum : uint8_t {
  ITEM_KL_X,
  ITEM_KL_50,
#if ENABLE_MIB_MESSAGES
  ITEM_DIMMING,
  ITEM_HOUR,
  ITEM_MINUTE,
#endif
  ITEM_MONITOR,
  ITEM_LISTEN,
  ITEM_HEALTH,
  ITEM_BACK,
  ITEM_COUNT          // at most 9: the position is shown as one digit
};
static uint8_t  g_screen = SCR_HOME;
static uint8_t  g_item   = 0;
static uint32_t g_uiTouchedAt = 0;
// Bus monitor: page 0 is the summary, page n the n-th ID of the sniffer table.
// Payload and interval are kept for the ID on show only, not for all of them.
static uint8_t  g_monPage  = 0;
static uint8_t  g_monDlc   = 0xFF;   // 0xFF: nothing seen since the page was opened
static uint8_t  g_monData[8];
static uint16_t g_monDelta = 0;
// Learning view. For the first LEARN_BASELINE_MS on a page, bytes that change
// are noted as changing by themselves (counters, checksums) and left alone;
// after that a byte that changes blinks for a while.
static uint32_t g_monBaseUntil = 0;
static uint8_t  g_monNoise = 0;      // one bit per byte: changes by itself
static uint8_t  g_monHot[8];         // display refreshes left to blink
static bool     g_blink = false;
#define MON_FIRST_ID_PAGE 2          // page 0 summary, page 1 who is awake
// The display has eight user characters. The home screen uses seven: six for
// the three icons and one for the radio wave, whose picture is redrawn in
// place for each stage of its animation.
#define ICON_BATTERY 1               // car battery, codes 1 and 2
#define ICON_DASH    3               // instrument cluster, codes 3 and 4
#define ICON_OBD     5               // diagnostic socket, codes 5 and 6
// User character 0, addressed as 8: the HD44780 repeats its user characters
// at codes 8..15, and 0 cannot sit in the middle of a C string.
#define ICON_WAVE    8
static uint16_t g_fps      = 0;
static uint16_t g_fpsCount = 0;
static uint32_t g_nextFpsAt = 0;

static LiquidCrystal g_lcd(LCD_RS_PIN, LCD_E_PIN, LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN);
static char     g_lcdShown[2][16];   // what is on the glass, so only changes get written
static uint32_t g_nextLcdAt = 0;
#endif

// --- state that survives a reset ---------------------------------------------
// Ignition, Klemme X and listen-only are kept in EEPROM, so a reset -- a PC
// opening the serial port is one -- or a power cut does not drop the ignition
// under a tester that is in the middle of flashing a control unit. Three
// bytes: a marker, the flags, and the flags inverted as a check. A byte is
// only written when it differs, so the wear is one write per change.
#define STATE_MARKER    0xC5
#define STATE_KL15      0x01
#define STATE_KLX       0x02
#define STATE_LISTEN    0x04

static void saveState() {
#if REMEMBER_STATE
  uint8_t flags = (g_state.kl_15 ? STATE_KL15 : 0) | (g_state.kl_x ? STATE_KLX : 0) |
                  (g_listenOnly ? STATE_LISTEN : 0);
  EEPROM.update(STATE_EEPROM_ADDR,     STATE_MARKER);
  EEPROM.update(STATE_EEPROM_ADDR + 1, flags);
  EEPROM.update(STATE_EEPROM_ADDR + 2, (uint8_t)~flags);
#endif
}

// False when nothing valid is stored: a new board, or a torn write.
static bool loadState() {
#if REMEMBER_STATE
  uint8_t flags = EEPROM.read(STATE_EEPROM_ADDR + 1);
  if (EEPROM.read(STATE_EEPROM_ADDR) != STATE_MARKER) return false;
  if (EEPROM.read(STATE_EEPROM_ADDR + 2) != (uint8_t)~flags) return false;
  g_state.kl_15 = g_state.kl_s = flags & STATE_KL15;
  g_state.kl_x  = flags & STATE_KLX;
  g_listenOnly  = flags & STATE_LISTEN;
  return true;
#else
  return false;
#endif
}

// --- MCP2515, the parts the library is not trusted with ---------------------
// mcp_can 1.5.1 copies as many data bytes as the DLC register says, up to 15,
// into an 8-byte member that sits right in front of its SPI pointer and its
// chip-select pin. One frame, or one misread register, with a DLC above 8
// therefore leaves the driver talking to nothing: it reports every transmit as
// sent and never receives again until reset. Seen on the bench twice. So frames
// are read here, with the length clamped, and the library only initialises the
// chip and transmits.
static const SPISettings kMcpSpi(10000000, MSBFIRST, SPI_MODE0);

static uint8_t mcpRead(uint8_t instruction, bool isRegister) {
  SPI.beginTransaction(kMcpSpi);
  digitalWrite(MCP_CS_PIN, LOW);
  if (isRegister) SPI.transfer(0x03);        // READ, then the address
  SPI.transfer(instruction);
  uint8_t v = SPI.transfer(0x00);
  digitalWrite(MCP_CS_PIN, HIGH);
  SPI.endTransaction();
  return v;
}
static uint8_t mcpReadRegister(uint8_t address) { return mcpRead(address, true); }
static uint8_t mcpReadStatus()                  { return mcpRead(0xA0, false); }

#define MCP_REG_CANSTAT 0x0E
#define MCP_REG_TEC     0x1C
#define MCP_REG_REC     0x1D
#define MCP_REG_EFLG    0x2D

// Takes one frame out of the controller, if there is one. READ RX BUFFER
// clears the buffer's interrupt flag by itself when chip select rises.
static bool mcpReadFrame(uint32_t &id, bool &extended, uint8_t &dlc, uint8_t *data) {
  uint8_t status = mcpReadStatus();
  uint8_t instruction;
  if (status & 0x01)      instruction = 0x90;   // RXB0, from SIDH
  else if (status & 0x02) instruction = 0x94;   // RXB1, from SIDH
  else return false;

  SPI.beginTransaction(kMcpSpi);
  digitalWrite(MCP_CS_PIN, LOW);
  SPI.transfer(instruction);
  uint8_t sidh = SPI.transfer(0x00);
  uint8_t sidl = SPI.transfer(0x00);
  uint8_t eid8 = SPI.transfer(0x00);
  uint8_t eid0 = SPI.transfer(0x00);
  dlc = SPI.transfer(0x00) & 0x0F;
  if (dlc > 8) dlc = 8;                         // 9..15 are legal on the wire and mean 8
  for (uint8_t i = 0; i < dlc; i++) data[i] = SPI.transfer(0x00);
  digitalWrite(MCP_CS_PIN, HIGH);
  SPI.endTransaction();

  id = ((uint16_t)sidh << 3) | (sidl >> 5);
  extended = sidl & 0x08;
  if (extended) id = (id << 18) | ((uint32_t)(sidl & 0x03) << 16) | ((uint16_t)eid8 << 8) | eid0;
  return true;
}

static void initBus() {
  g_busOk = (CAN.begin(MCP_ANY, MCP_BITRATE, MCP_CRYSTAL) == CAN_OK);
  // Without setMode the controller stays in configuration mode and silently
  // transmits nothing. Listen-only is the deliberate version of that.
  if (g_busOk) CAN.setMode(g_listenOnly ? MCP_LISTENONLY : MCP_NORMAL);
}

// Once a second: is the chip still there and in the mode it should be in? A
// module that lost power comes back in configuration mode, and one that was
// never found at boot is tried again, so the 5 V wire can be plugged in late.
static void serviceHealth() {
  if (!due(g_nextHealthAt, g_busOk ? HEALTH_CHECK_MS : BUS_RETRY_MS)) return;
  if (g_busOk) {
    uint8_t expected = g_listenOnly ? MCP_LISTENONLY : MCP_NORMAL;
    if ((mcpReadRegister(MCP_REG_CANSTAT) & 0xE0) == expected) return;
    if (g_recoveries < 255) g_recoveries++;
  }
  initBus();
#if ENABLE_SERIAL_CONTROL
  Serial.print(F("MCP2515 re-init: "));
  Serial.println(g_busOk ? F("ok") : F("FAILED"));
#endif
}

// ---------------------------------------------------------------------------

static bool sendFrame(uint16_t id, uint8_t dlc, uint8_t *data) {
  // Listen-only: nothing goes out and the counters stay as they are, so the
  // Klemmen counter does not advance either. The same without a controller.
  if (g_listenOnly || !g_busOk) return false;
  byte rc = CAN.sendMsgBuf(id, 0, dlc, data);
  if (rc == CAN_OK) g_txOk++; else g_txErr++;

#if ENABLE_SERIAL_CONTROL
  if (g_trace) {
    Serial.print(id, HEX);
    Serial.print(rc == CAN_OK ? F(" [ok ] ") : F(" [ERR] "));
    for (uint8_t i = 0; i < dlc; i++) {
      if (data[i] < 0x10) Serial.print('0');
      Serial.print(data[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
  }
#endif
  return rc == CAN_OK;
}

// Listen-only also withholds the acknowledge bit, so the controller leaves no
// trace on the bus at all -- the mode for a real, running car.
static void setListenOnly(bool on) {
  g_listenOnly = on;
  saveState();
  if (g_busOk) CAN.setMode(on ? MCP_LISTENONLY : MCP_NORMAL);
#if ENABLE_SERIAL_CONTROL
  Serial.print(F("listen-only: "));
  Serial.println(on ? F("ON, transmitting nothing") : F("OFF"));
#endif
}

static void setIgnition(bool on) {
  g_state.kl_s  = on;   // the key is "inserted" whenever ignition is on
  g_state.kl_15 = on;
  if (!on) {
    g_state.kl_50 = false;
    g_kl50EndsAt  = 0;
  }
  saveState();
#if ENABLE_STATUS_LED
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
#endif
#if ENABLE_SERIAL_CONTROL
  Serial.print(F("Klemme 15: "));
  Serial.print(on ? F("ON") : F("OFF"));
  Serial.println(g_listenOnly ? F(" (listen-only: not sent)") : F(""));
#endif
}

static void pulseKl50() {
  if (!g_state.kl_15) {
#if ENABLE_SERIAL_CONTROL
    Serial.println(F("Kl 50 ignored: ignition is off"));
#endif
    return;
  }
  g_state.kl_50 = true;
  g_kl50EndsAt  = millis() + KL50_PULSE_MS;
}

// For the "when last seen" stamps: seen, and no longer than ms ago.
static bool recent(uint32_t at, uint16_t ms) {
  return at != 0 && millis() - at < ms;
}

static void noteNmNode(uint8_t node, uint32_t now) {
  uint8_t slot = NM_MAX_NODES;
  for (uint8_t i = 0; i < NM_MAX_NODES; i++) {
    bool live = recent(g_nmSeenAt[i], NM_AWAKE_MS);
    if (live && g_nmNode[i] == node) { slot = i; break; }
    if (!live && slot == NM_MAX_NODES) slot = i;
  }
  if (slot == NM_MAX_NODES) return;      // table full of live units
  g_nmNode[slot]   = node;
  g_nmSeenAt[slot] = now | 1;
}

static void sendKlemmenStatus() {
  uint8_t frame[MQB_DLC_KLEMMEN_STATUS_01];
  buildKlemmenStatus01(frame, g_state, g_counter);
  // The counter advances only on a frame that actually went out, matching a
  // real ECU -- receivers treat a gap in the sequence as a fault.
  if (sendFrame(MQB_ID_KLEMMEN_STATUS_01, MQB_DLC_KLEMMEN_STATUS_01, frame)) {
    g_counter = (uint8_t)((g_counter + 1) & 0x0F);
  }
}

// ---------------------------------------------------------------------------

// A short press is reported on release, so that it can be told from a long
// one. A long press is reported while still held, and again every
// BUTTON_REPEAT_MS for as long as it stays held.
static uint8_t pollButton() {
  uint32_t now = millis();
  bool raw = digitalRead(BUTTON_PIN);
  if (raw != g_btnLast) {
    g_btnLast      = raw;
    g_btnChangedAt = now;
    return BTN_NONE;
  }
  if (raw != g_btnStable) {
    if (now - g_btnChangedAt < BUTTON_DEBOUNCE_MS) return BTN_NONE;
    g_btnStable = raw;
    if (raw == LOW) { g_btnDownAt = now; g_btnHeld = false; return BTN_NONE; }
    return g_btnHeld ? BTN_NONE : BTN_SHORT;
  }
  if (raw == HIGH) return BTN_NONE;
  if (!g_btnHeld) {
    if (now - g_btnDownAt < BUTTON_LONG_MS) return BTN_NONE;
    g_btnHeld = true;
    g_btnRepeatAt = now + BUTTON_REPEAT_MS;
    return BTN_LONG;
  }
  if ((int32_t)(now - g_btnRepeatAt) < 0) return BTN_NONE;
  g_btnRepeatAt = now + BUTTON_REPEAT_MS;
  return BTN_REPEAT;
}

#if ENABLE_LCD
// Holding the button on a menu item. Values step on for as long as it is held;
// switches and screen changes act once per press.
static void menuAction(bool repeat) {
  switch (g_item) {
    case ITEM_KL_X:   if (!repeat) { g_state.kl_x = !g_state.kl_x; saveState(); } break;
    case ITEM_KL_50:  pulseKl50(); break;          // held: keeps cranking
#if ENABLE_MIB_MESSAGES
    case ITEM_DIMMING:
      g_dimmung.kl58xd = g_dimmung.kl58xd >= 250 ? 10 : g_dimmung.kl58xd + 10;
      break;
    case ITEM_HOUR:   g_clock.hour = (uint8_t)((g_clock.hour + 1) % 24); break;
    case ITEM_MINUTE: g_clock.minute = (uint8_t)((g_clock.minute + 1) % 60);
                      g_clock.second = 0; break;
#endif
    case ITEM_MONITOR:
      if (!repeat) { g_screen = SCR_MONITOR; g_monPage = 0; g_monDlc = 0xFF; }
      break;
    case ITEM_LISTEN: if (!repeat) setListenOnly(!g_listenOnly); break;
    case ITEM_BACK:   if (!repeat) g_screen = SCR_HOME; break;
    default: break;
  }
}
#endif

// Home: short press toggles ignition, long press opens the menu.
// Menu: short press is the next item, holding acts on it.
// Bus monitor: short press is the next ID, long press goes back to the menu.
static void buttonEvent(uint8_t ev) {
  if (ev == BTN_NONE) return;
#if ENABLE_LCD
  g_uiTouchedAt = millis();
  switch (g_screen) {
    case SCR_HOME:
      if (ev == BTN_SHORT) setIgnition(!g_state.kl_15);
      else if (ev == BTN_LONG) { g_screen = SCR_MENU; g_item = 0; }
      break;
    case SCR_MONITOR:
      if (ev == BTN_SHORT) { g_monPage++; g_monDlc = 0xFF; }
      else if (ev == BTN_LONG) g_screen = SCR_MENU;
      break;
    default:
      if (ev == BTN_SHORT) { if (++g_item >= ITEM_COUNT) g_item = 0; }
      else menuAction(ev == BTN_REPEAT);
      break;
  }
#else
  if (ev == BTN_SHORT) setIgnition(!g_state.kl_15);
#endif
}

static void serviceSniffer() {
  if (!g_busOk) return;

  uint32_t id = 0;
  bool extended = false;
  uint8_t dlc = 0;
  uint8_t buf[8];
  if (!mcpReadFrame(id, extended, dlc, buf)) return;
  g_rxCount++;
#if ENABLE_LCD
  if (g_fpsCount < 0xFFFF) g_fpsCount++;
#endif

#if ENABLE_SERIAL_CONTROL
  if (g_sniff) {
    Serial.print(extended ? F("RX x") : F("RX "));
    Serial.print(id, HEX);
    Serial.print(' ');
    for (uint8_t i = 0; i < dlc; i++) {
      if (buf[i] < 0x10) Serial.print('0');
      Serial.print(buf[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
  }
#endif
  // The table holds 11-bit IDs; a 29-bit one folded into it would pose as
  // some other message.
  uint32_t now = millis();
  if (extended) {
    g_rxExtended++;
    if ((id & 0x1FFFFF00UL) == NM_ID_BASE) noteNmNode((uint8_t)id, now);
    return;
  }

  uint16_t sid = (uint16_t)id;
  // The controller does not receive its own frames, so these are someone else's.
  if (sid == MQB_ID_DIMMUNG_01)  g_dimmungHeardAt  = now | 1;
  if (sid == MQB_ID_DIAGNOSE_01) g_diagnoseHeardAt = now | 1;
#if ENABLE_VIN
  if (sid == MQB_ID_VIN_01)      g_vinHeardAt      = now | 1;
#endif
  if (sid >= 0x700)              g_testerAt        = now | 1;
#if ENABLE_VOLTAGE
  if (sid == VOLTAGE_CAN_ID && dlc > VOLTAGE_BYTE) {
    uint32_t mv = (uint32_t)buf[VOLTAGE_BYTE] * VOLTAGE_MV_PER_BIT + VOLTAGE_OFFSET_MV;
    g_voltDv = mv > 25500 ? 255 : (uint8_t)(mv / 100);
    g_voltAt = now | 1;
  }
#endif

  IdStat *st = NULL;
  for (uint8_t i = 0; i < g_statCount; i++) {
    if (g_stats[i].id == sid) { st = &g_stats[i]; break; }
  }
  if (st == NULL && g_statCount < SNIFFER_MAX_IDS) {
    st = &g_stats[g_statCount++];
    st->id = sid; st->count = 0; st->lastMs = now;
    st->minDelta = 0xFFFF; st->maxDelta = 0;
  } else if (st == NULL) {
    g_statsFull = true;
  }

  if (st != NULL) {
    uint32_t d = now - st->lastMs;
    if (d > 0xFFFF) d = 0xFFFF;
    if (st->count > 0) {
      if (d < st->minDelta) st->minDelta = (uint16_t)d;
      if (d > st->maxDelta) st->maxDelta = (uint16_t)d;
    }
#if ENABLE_LCD
    if (g_monPage >= MON_FIRST_ID_PAGE && st == &g_stats[g_monPage - MON_FIRST_ID_PAGE]) {
      if (g_monDlc == 0xFF) {            // first frame on this page
        g_monBaseUntil = now + LEARN_BASELINE_MS;
        g_monNoise = 0;
        memset(g_monHot, 0, sizeof g_monHot);
      } else {
        bool baseline = (int32_t)(now - g_monBaseUntil) < 0;
        for (uint8_t i = 0; i < dlc && i < g_monDlc; i++) {
          if (buf[i] == g_monData[i]) continue;
          if (baseline) g_monNoise |= (uint8_t)(1 << i);
          else if (!(g_monNoise & (1 << i))) g_monHot[i] = LEARN_BLINK_REFRESHES;
        }
      }
      g_monDelta = st->count > 0 ? (uint16_t)d : 0;
      g_monDlc   = dlc;
      memcpy(g_monData, buf, dlc);
    }
#endif
    st->lastMs = now;
    if (st->count < 0xFFFF) st->count++;
  }
}

#if ENABLE_SERIAL_CONTROL
static void printStats() {
  Serial.println(F("  ID  count   min   max  (ms between frames)"));
  for (uint8_t i = 0; i < g_statCount; i++) {
    IdStat &s = g_stats[i];
    Serial.print(F(" "));
    Serial.print(s.id, HEX);
    Serial.print('\t');
    Serial.print(s.count);
    Serial.print('\t');
    if (s.count > 1) {
      Serial.print(s.minDelta); Serial.print('\t'); Serial.println(s.maxDelta);
    } else {
      Serial.println(F("-\t-"));
    }
  }
  if (g_statCount == 0) Serial.println(F(" (nothing received)"));
  if (g_statsFull) Serial.println(F(" WARNING: more IDs seen than SNIFFER_MAX_IDS"));
}

#if ENABLE_VIN
static void printVin() {
  Serial.print(F("  VIN (6B4): "));
  if (!g_vinOn) { Serial.println(F("off")); return; }
  Serial.write((const uint8_t *)g_vin, MQB_VIN_LENGTH);
  Serial.println(recent(g_vinHeardAt, MIB_YIELD_MS) ? F(", bus sends it, ours off") : F(", ours"));
}

// After 'V': the 17 characters start VIN_01. Anything a VIN cannot hold, a
// '-' for one, ends the typing and leaves it off.
static void vinKey(char key) {
  char c = vinChar(key);
  if (c) {
    g_vin[g_vinTyped++] = c;
    g_vinTypedAt = millis();
    if (g_vinTyped < MQB_VIN_LENGTH) return;
    g_vinOn  = true;
    g_vinMux = 0;
  }
  g_vinTyped = 0xFF;
  printVin();
}
#endif

static void printStatus() {
  Serial.println(F("--- status ---"));
  Serial.print(F("  bus init : ")); Serial.println(g_busOk ? F("ok") : F("FAILED"));
  Serial.print(F("  mode     : ")); Serial.println(g_listenOnly ? F("listen-only, tx off") : F("normal"));
  Serial.print(F("  Kl S     : ")); Serial.println(g_state.kl_s);
  Serial.print(F("  Kl 15    : ")); Serial.println(g_state.kl_15);
  Serial.print(F("  Kl X     : ")); Serial.println(g_state.kl_x);
  Serial.print(F("  Kl 50    : ")); Serial.println(g_state.kl_50);
#if ENABLE_MIB_MESSAGES
  Serial.print(F("  clock    : "));
  Serial.print(g_clock.hour); Serial.print(':');
  Serial.print(g_clock.minute); Serial.print(':');
  Serial.println(g_clock.second);
  Serial.print(F("  dimming  : ")); Serial.println(g_dimmung.kl58xd);
  Serial.print(F("  5F0 / 6B2: "));
  Serial.print(recent(g_dimmungHeardAt, MIB_YIELD_MS) ? F("bus sends it, ours off") : F("ours"));
  Serial.print(F(" / "));
  Serial.println(recent(g_diagnoseHeardAt, MIB_YIELD_MS) ? F("bus sends it, ours off") : F("ours"));
#endif
#if ENABLE_VIN
  printVin();
#endif
#if ENABLE_VOLTAGE
  Serial.print(F("  voltage  : "));
  if (recent(g_voltAt, VOLTAGE_STALE_MS)) {
    Serial.print(g_voltDv / 10); Serial.print('.'); Serial.print(g_voltDv % 10);
    Serial.println(VOLTAGE_VERIFIED ? F(" V") : F(" V (source not verified)"));
  } else {
    Serial.println(F("no frame"));
  }
#endif
  Serial.print(F("  tester   : ")); Serial.println(recent(g_testerAt, TESTER_HOLD_MS) ? F("present") : F("-"));
  Serial.print(F("  awake    :"));
  for (uint8_t i = 0; i < NM_MAX_NODES; i++) {
    if (!recent(g_nmSeenAt[i], NM_AWAKE_MS)) continue;
    Serial.print(' '); Serial.print(g_nmNode[i], HEX);
  }
  Serial.println();
  Serial.print(F("  tx ok    : ")); Serial.println(g_txOk);
  Serial.print(F("  tx err   : ")); Serial.println(g_txErr);
  Serial.print(F("  rx frames: ")); Serial.println(g_rxCount);
  Serial.print(F("  rx 29-bit: ")); Serial.println(g_rxExtended);
  if (g_busOk) {
    // Straight from the chip: error flags and the two CAN error counters.
    Serial.print(F("  chip     : eflg 0x")); Serial.print(mcpReadRegister(MCP_REG_EFLG), HEX);
    Serial.print(F("  tec ")); Serial.print(mcpReadRegister(MCP_REG_TEC));
    Serial.print(F("  rec ")); Serial.println(mcpReadRegister(MCP_REG_REC));
  }
  Serial.print(F("  re-inits : ")); Serial.println(g_recoveries);
  Serial.println(F("--------------"));
}

static void printHelp() {
  Serial.println(F("1/0 ign on/off   t toggle ign   x toggle Kl X   5 pulse Kl 50"));
  Serial.println(F("s status   d toggle tx trace   r toggle rx dump   p rx stats"));
  Serial.println(F("c clear rx stats   +/- dimming   l listen-only   h help"));
  Serial.println(F("b short press   B long press   v what the LCD shows"));
#if ENABLE_VIN
  Serial.println(F("V and 17 characters: send that VIN   V- stop"));
#endif
}

static void serviceSerial() {
#if ENABLE_VIN
  // A VIN left half typed is given up, so that a stray 'V' does not swallow
  // the commands that follow it.
  if (g_vinTyped != 0xFF && millis() - g_vinTypedAt > VIN_ENTRY_MS) vinKey(0);
#endif
  if (!Serial.available()) return;
  char key = (char)Serial.read();
#if ENABLE_VIN
  if (g_vinTyped != 0xFF) { vinKey(key); return; }
#endif
  switch (key) {
    case '1': setIgnition(true);  break;
    case '0': setIgnition(false); break;
    case 't': setIgnition(!g_state.kl_15); break;
    case 'x': g_state.kl_x = !g_state.kl_x; saveState();
              Serial.print(F("Klemme X: ")); Serial.println(g_state.kl_x); break;
    case '5': pulseKl50(); break;
    case 's': printStatus(); break;
    case 'd': g_trace = !g_trace; break;
    case 'r': g_sniff = !g_sniff;
              Serial.print(F("rx dump: ")); Serial.println(g_sniff); break;
    case 'p': printStats(); break;
    case 'c': g_statCount = 0; g_statsFull = false; g_rxCount = 0; g_rxExtended = 0;
              Serial.println(F("rx stats cleared")); break;
    case 'l': setListenOnly(!g_listenOnly); break;
    case 'b': buttonEvent(BTN_SHORT); break;
    case 'B': buttonEvent(BTN_LONG);  break;
#if ENABLE_LCD
    case 'v':
      for (uint8_t row = 0; row < 2; row++) {
        Serial.print('|');
        for (uint8_t i = 0; i < 16; i++) {         // the four icons as letters
          char c = g_lcdShown[row][i];
          // battery Bb, cluster Dd, socket Oo, the wave w
          Serial.print(c >= ICON_BATTERY && c <= ICON_WAVE ? "BbDdOo?w"[c - ICON_BATTERY] : c);
        }
        Serial.println('|');
      }
      break;
#endif
#if ENABLE_MIB_MESSAGES
    case '+': if (g_dimmung.kl58xd < 250) g_dimmung.kl58xd += 10;
              Serial.println(g_dimmung.kl58xd); break;
    case '-': if (g_dimmung.kl58xd > 10) g_dimmung.kl58xd -= 10;
              Serial.println(g_dimmung.kl58xd); break;
#endif
#if ENABLE_VIN
    // Off at once: the characters that follow overwrite the VIN being sent.
    case 'V': g_vinOn = false; g_vinTyped = 0; g_vinTypedAt = millis(); break;
#endif
    case 'h': printHelp(); break;
    default: break;
  }
}
#endif

// ---------------------------------------------------------------------------

// Fixed-period scheduler: advancing the deadline by the period rather than
// from "now" keeps the cycle from drifting, with a catch-up guard after a stall.
static bool due(uint32_t &deadline, uint16_t period) {
  if ((int32_t)(millis() - deadline) < 0) return false;
  deadline += period;
  if ((int32_t)(millis() - deadline) >= 0) deadline = millis() + period;
  return true;
}

// --- 16x2 LCD ----------------------------------------------------------------
#if ENABLE_LCD
// Fills the 16 characters of a line from a PROGMEM string, space padded.
static void lcdText(char *line, const char *text) {
  for (uint8_t i = 0; i < 16; i++) {
    char c = (char)pgm_read_byte(text);
    if (c) { line[i] = c; text++; } else { line[i] = ' '; }
  }
}

// Right-aligns v in width characters; a value too wide for them shows as nines.
static void lcdNumber(char *dst, uint8_t width, uint32_t v) {
  for (uint8_t i = width; i > 0; i--) {
    dst[i - 1] = (v || i == width) ? (char)('0' + v % 10) : ' ';
    v /= 10;
  }
  if (v) memset(dst, '9', width);
}

// Writes only the span of a line that differs from what is on the glass: a
// character costs about 0.3 ms of blocking I/O, and lcd.clear() 2 ms.
static void lcdLine(uint8_t row, const char *line) {
  char *shown = g_lcdShown[row];
  uint8_t first = 16, last = 0;
  for (uint8_t i = 0; i < 16; i++) {
    if (line[i] != shown[i]) { if (first == 16) first = i; last = i; }
  }
  if (first == 16) return;
  g_lcd.setCursor(first, row);
  for (uint8_t i = first; i <= last; i++) {
    g_lcd.write((uint8_t)line[i]);
    shown[i] = line[i];
  }
}

static char hexDigit(uint8_t v) {
  v &= 0x0F;
  return (char)(v < 10 ? '0' + v : 'A' + v - 10);
}

static void lcdTwoDigits(char *dst, uint8_t v) {
  dst[0] = (char)('0' + v / 10 % 10);
  dst[1] = (char)('0' + v % 10);
}

// The supply voltage as six characters: " 12.3V", or "?12.3V" while the frame
// it is read from has not been confirmed for this car (VOLTAGE_VERIFIED).
static void lcdVoltage(char *dst) {
#if ENABLE_VOLTAGE
  if (!recent(g_voltAt, VOLTAGE_STALE_MS)) {
    memcpy_P(dst, PSTR(" --.-V"), 6);
    return;
  }
  // Not '~': the display's own character set has an arrow at that code.
  dst[0] = VOLTAGE_VERIFIED ? ' ' : '?';
  lcdNumber(dst + 1, 2, g_voltDv / 10);
  dst[3] = '.';
  dst[4] = (char)('0' + g_voltDv % 10);
  dst[5] = 'V';
#else
  (void)dst;
#endif
}

// The radio wave: a dot in the corner and up to three arcs spreading from it.
// Each stage is drawn into the same user character, so the cell on the glass
// changes without being written again.
static void lcdWaveStage(uint8_t stage) {
  static const uint8_t pictures[4][8] PROGMEM = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 },   // the source alone
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x04, 0x14 },   // one arc
    { 0x00, 0x00, 0x00, 0x1C, 0x02, 0x19, 0x05, 0x15 },   // two arcs
    { 0x00, 0x1C, 0x02, 0x1D, 0x02, 0x19, 0x05, 0x15 },   // three arcs
  };
  uint8_t rows[8];
  memcpy_P(rows, pictures[stage & 3], 8);
  g_lcd.createChar(ICON_WAVE & 7, rows);
}

//   Ign on  X ?12.3V    the ignition we send, X while Klemme X is set, voltage
//   BbDdOo  err 12 w    three icons, failed frames if any, the wave
// In place of "Ign on" / "Ign off" the first line reads "Cranking" while
// Klemme 50 is held and "Listen" in listen-only mode, where we send no
// ignition at all; a failed boot check takes the whole line.
// The icons, each two dashes while it does not apply:
//   battery             Klemme 30: a supply voltage is being read; blinks when low
//   instrument cluster  Klemme 58: the dimming message is on the bus
//   diagnostic socket   a diagnostic tester is talking
// The wave in the corner keeps waving for as long as the traffic is healthy:
// our frames go out and are acknowledged or, in listen-only, frames keep
// coming in. It is '!' while frames fail to go out and a dash when nothing
// moves.
static void lcdIcon(char *dst, uint8_t icon) {
  dst[0] = (char)icon;
  dst[1] = (char)(icon + 1);
}

static void lcdHome(char *top, char *bottom) {
  if (!g_selfTestOk)     lcdText(top, PSTR("CHECKSUM FAIL"));
  else if (!g_busOk)     lcdText(top, PSTR("MCP2515 FAILED"));
  else {
    lcdText(top, g_listenOnly ? PSTR("Listen")
               : g_state.kl_50 ? PSTR("Cranking")
               : g_state.kl_15 ? PSTR("Ign on") : PSTR("Ign off"));
    if (g_state.kl_x) top[8] = 'X';
    lcdVoltage(top + 10);
  }

  bool lamp = recent(g_dimmungHeardAt, MIB_YIELD_MS);
#if ENABLE_MIB_MESSAGES
  if (g_state.kl_15 && g_busOk && !g_listenOnly) lamp = true;   // ours is going out
#endif
  lcdText(bottom, PSTR("------"));
#if ENABLE_VOLTAGE
  if (recent(g_voltAt, VOLTAGE_STALE_MS)) {
    if (g_voltDv < VOLTAGE_LOW_DV && g_blink) bottom[0] = bottom[1] = ' ';
    else lcdIcon(bottom + 0, ICON_BATTERY);
  }
#endif
  if (lamp) lcdIcon(bottom + 2, ICON_DASH);
  if (recent(g_testerAt, TESTER_HOLD_MS)) lcdIcon(bottom + 4, ICON_OBD);

  if (g_txErr) {
    bottom[8] = 'e'; bottom[9] = 'r'; bottom[10] = 'r';
    lcdNumber(bottom + 11, 3, g_txErr);     // 999 stands for "999 or more"
  }

  // What moved since the last refresh decides what the corner shows.
  static uint32_t seenTxOk = 0, seenTxErr = 0, seenRx = 0;
  static uint8_t stage = 0;
  bool moving = g_listenOnly ? g_rxCount != seenRx : g_txOk != seenTxOk;
  if (g_txErr != seenTxErr) bottom[15] = '!';
  else if (!moving)         bottom[15] = '-';
  else {
    bottom[15] = (char)ICON_WAVE;
    lcdWaveStage(stage);
    stage = (uint8_t)((stage + 1) & 3);
  }
  seenTxOk = g_txOk; seenTxErr = g_txErr; seenRx = g_rxCount;
}

//   Dimming      3/9    the item and where it is in the menu
//   100    hold=+10     its value, and what holding the button does
static void lcdMenu(char *top, char *bottom) {
  static const char onHint[]  PROGMEM = "on   hold=toggle";
  static const char offHint[] PROGMEM = "off  hold=toggle";
  switch (g_item) {
    case ITEM_KL_X:
      lcdText(top, PSTR("Kl X"));
      lcdText(bottom, g_state.kl_x ? onHint : offHint);
      break;
    case ITEM_KL_50:
      lcdText(top, PSTR("Kl 50 crank"));
      lcdText(bottom, g_state.kl_50 ? PSTR("cranking")
                    : g_state.kl_15 ? PSTR("hold = crank") : PSTR("needs Kl 15 on"));
      break;
#if ENABLE_MIB_MESSAGES
    case ITEM_DIMMING:
      lcdText(top, PSTR("Dimming"));
      lcdText(bottom, PSTR("       hold=+10"));
      lcdNumber(bottom, 3, g_dimmung.kl58xd);
      break;
    case ITEM_HOUR:
    case ITEM_MINUTE:
      lcdText(top, g_item == ITEM_HOUR ? PSTR("Clock hour") : PSTR("Clock minute"));
      lcdText(bottom, g_item == ITEM_HOUR ? PSTR("  :    hold=+1h") : PSTR("  :    hold=+1m"));
      lcdTwoDigits(bottom, g_clock.hour);
      lcdTwoDigits(bottom + 3, g_clock.minute);
      break;
#endif
    case ITEM_MONITOR:
      lcdText(top, PSTR("Bus monitor"));
      lcdText(bottom, PSTR("hold = open"));
      break;
    case ITEM_LISTEN:
      lcdText(top, PSTR("Listen-only"));
      lcdText(bottom, g_listenOnly ? onHint : offHint);
      break;
    case ITEM_HEALTH:
      //   t  0 r  0 e40 i0    CAN error counters, error flags, re-inits
      lcdText(top, PSTR("Health"));
      lcdText(bottom, PSTR("t    r    e   i"));
      if (g_busOk) {
        lcdNumber(bottom + 1, 3, mcpReadRegister(MCP_REG_TEC));
        lcdNumber(bottom + 6, 3, mcpReadRegister(MCP_REG_REC));
        uint8_t eflg = mcpReadRegister(MCP_REG_EFLG);
        bottom[11] = hexDigit(eflg >> 4);
        bottom[12] = hexDigit(eflg);
      } else {
        lcdText(bottom, PSTR("MCP2515 FAILED"));
      }
      lcdNumber(bottom + 15, 1, g_recoveries);
      break;
    default:
      lcdText(top, PSTR("Back"));
      lcdText(bottom, PSTR("hold = home"));
      break;
  }
  top[13] = (char)('1' + g_item);
  top[14] = '/';
  top[15] = (char)('0' + ITEM_COUNT);
}

//   BUS    87 fps       frames per second on the bus
//   ids 16 ext    40    11-bit IDs seen, and 29-bit frames
// then who is awake, from network management:
//   awake 2             units sending 0x1B0000xx
//   10 5F               their node addresses
// then one page per ID, which is also the learning view:
//   3DC   50ms  2/16    the ID, time since its previous frame, position
//   0011223344556677    its latest payload; a byte that just changed blinks
static void lcdMonitor(char *top, char *bottom) {
  if (g_monPage >= MON_FIRST_ID_PAGE + g_statCount) { g_monPage = 0; g_monDlc = 0xFF; }
  if (g_monPage == 0) {
    lcdText(top, PSTR("BUS       fps"));
    lcdNumber(top + 4, 5, g_fps);
    lcdText(bottom, PSTR("ids    ext"));
    lcdNumber(bottom + 4, 2, g_statCount);
    lcdNumber(bottom + 11, 5, g_rxExtended);
    return;
  }
  if (g_monPage == 1) {
    uint8_t n = 0;
    lcdText(top, PSTR("awake"));
    lcdText(bottom, PSTR(""));
    // In ascending order of address, so that the list does not reshuffle
    // with whichever unit happened to be heard first.
    int16_t last = -1;
    for (;;) {
      int16_t next = 256;
      for (uint8_t i = 0; i < NM_MAX_NODES; i++) {
        if (!recent(g_nmSeenAt[i], NM_AWAKE_MS)) continue;
        if (g_nmNode[i] > last && g_nmNode[i] < next) next = g_nmNode[i];
      }
      if (next == 256) break;
      if (n < 5) {
        bottom[3 * n]     = hexDigit((uint8_t)(next >> 4));
        bottom[3 * n + 1] = hexDigit((uint8_t)next);
      } else {
        bottom[15] = '+';
      }
      n++;
      last = next;
    }
    lcdNumber(top + 6, 2, n);
    if (n == 0) lcdText(bottom, PSTR("nobody"));
    return;
  }
  uint8_t index = g_monPage - MON_FIRST_ID_PAGE;
  uint16_t id = g_stats[index].id;
  lcdText(top, PSTR("        ms"));
  top[0] = hexDigit((uint8_t)(id >> 8));
  top[1] = hexDigit((uint8_t)(id >> 4));
  top[2] = hexDigit((uint8_t)id);
  top[13] = '/';
  lcdNumber(top + 11, 2, index + 1);
  if (g_statCount < 10) top[14] = (char)('0' + g_statCount);   // "2/9", not "2/ 9"
  else lcdNumber(top + 14, 2, g_statCount);
  if (g_monDlc == 0xFF) {
    lcdText(bottom, PSTR("waiting"));
    return;
  }
  if ((int32_t)(millis() - g_monBaseUntil) < 0) memcpy_P(top + 3, PSTR("  learn"), 7);
  else lcdNumber(top + 3, 5, g_monDelta);
  lcdText(bottom, PSTR(""));
  for (uint8_t i = 0; i < g_monDlc; i++) {
    if (g_monHot[i]) {
      g_monHot[i]--;
      if (g_blink) continue;                 // blank this time round
    }
    bottom[2 * i]     = hexDigit(g_monData[i] >> 4);
    bottom[2 * i + 1] = hexDigit(g_monData[i]);
  }
}

static void serviceLcd() {
  if (due(g_nextFpsAt, 1000)) { g_fps = g_fpsCount; g_fpsCount = 0; }
  if (!due(g_nextLcdAt, LCD_REFRESH_MS)) return;
  g_blink = !g_blink;

  // A menu left alone goes back to the home screen. Not the health item,
  // which is there to be watched, and not the bus monitor.
  if (g_screen == SCR_MENU && g_item != ITEM_HEALTH &&
      millis() - g_uiTouchedAt > MENU_TIMEOUT_MS) {
    g_screen = SCR_HOME;
  }

  char top[16], bottom[16];
  if (g_screen == SCR_MENU)         lcdMenu(top, bottom);
  else if (g_screen == SCR_MONITOR) lcdMonitor(top, bottom);
  else                              lcdHome(top, bottom);
  lcdLine(0, top);
  lcdLine(1, bottom);
}
#endif

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
#if ENABLE_STATUS_LED
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
#endif
  g_selfTestOk = mqbChecksumSelfTest();
  bool restored = loadState();
  (void)restored;
#if ENABLE_STATUS_LED
  digitalWrite(STATUS_LED_PIN, g_state.kl_15 ? HIGH : LOW);
#endif

#if ENABLE_SERIAL_CONTROL
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { /* brief wait on native-USB boards */ }
  Serial.println();
  Serial.println(F("VW MQB Klemme 15 emulator"));
  Serial.print(F("checksum self-test: "));
  Serial.println(g_selfTestOk ? F("PASS") : F("FAIL"));
#endif

  initBus();

#if ENABLE_SERIAL_CONTROL
  Serial.print(F("MCP2515 init: "));
  Serial.println(g_busOk ? F("ok") : F("FAILED -- check wiring and crystal"));
  if (restored) {
    Serial.print(F("restored: Klemme 15 "));
    Serial.print(g_state.kl_15 ? F("ON") : F("OFF"));
    Serial.println(g_state.kl_x ? F(", Klemme X on") : F(""));
  }
  if (g_listenOnly) Serial.println(F("listen-only: ON, transmitting nothing"));
  printHelp();
#endif

#if ENABLE_LCD
  g_lcd.begin(16, 2);                          // ends with a clear: all spaces
  // On the glass there is a gap of one dot between two cells.
  static const uint8_t icons[6][8] PROGMEM = {
    { 0x0C, 0x1F, 0x10, 0x12, 0x17, 0x12, 0x1F, 0x00 },   // battery: post, and the plus
    { 0x06, 0x1F, 0x01, 0x01, 0x1D, 0x01, 0x1F, 0x00 },   //          post, and the minus
    { 0x00, 0x07, 0x08, 0x10, 0x10, 0x11, 0x1F, 0x00 },   // cluster: a dial ...
    { 0x00, 0x1C, 0x02, 0x09, 0x11, 0x01, 0x1F, 0x00 },   //          ... and its needle
    // Diagnostic socket: a rounded outline with two rows of pins. The gap
    // between the two cells falls between pins.
    //   ..###  ###..
    //   .#...  ...#.
    //   #.#.#  #.#.#
    //   #....  ....#
    //   #.#.#  #.#.#
    //   .#...  ...#.
    //   ..###  ###..
    { 0x07, 0x08, 0x15, 0x10, 0x15, 0x08, 0x07, 0x00 },
    { 0x1C, 0x02, 0x15, 0x01, 0x15, 0x02, 0x1C, 0x00 },
  };
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t rows[8];
    memcpy_P(rows, icons[i], 8);
    g_lcd.createChar(ICON_BATTERY + i, rows);
  }
  lcdWaveStage(0);
  memset(g_lcdShown, ' ', sizeof g_lcdShown);
#endif

  uint32_t now = millis();
  g_nextKlemmenAt = now;
  g_nextHealthAt  = now + HEALTH_CHECK_MS;
#if ENABLE_MIB_MESSAGES
  g_nextDimmungAt  = now;
  g_nextDiagnoseAt = now;
  g_nextClockAt    = now;
#endif
#if ENABLE_VIN
  g_nextVinAt = now;
#endif
#if ENABLE_LCD
  g_nextLcdAt = now;
  g_nextFpsAt = now;
#endif
}

void loop() {
  buttonEvent(pollButton());
  serviceSniffer();
  serviceHealth();
#if ENABLE_SERIAL_CONTROL
  serviceSerial();
#endif

  if (g_state.kl_50 && (int32_t)(millis() - g_kl50EndsAt) >= 0) {
    g_state.kl_50 = false;
  }

  // Klemmen_Status_01 goes out continuously regardless of terminal state --
  // that is what a real gateway does, and it is what keeps the bus awake.
  if (due(g_nextKlemmenAt, KLEMMEN_CYCLE_MS)) sendKlemmenStatus();

#if ENABLE_MIB_MESSAGES
  if (due(g_nextClockAt, 1000)) tickClock(g_clock);

  // Dimming and clock are terminal-15 dependent on a real car.
  if (due(g_nextDimmungAt, DIMMUNG_CYCLE_MS) && g_state.kl_15 && !recent(g_dimmungHeardAt, MIB_YIELD_MS)) {
    uint8_t frame[MQB_DLC_DIMMUNG_01];
    buildDimmung01(frame, g_dimmung);
    sendFrame(MQB_ID_DIMMUNG_01, MQB_DLC_DIMMUNG_01, frame);
  }
  if (due(g_nextDiagnoseAt, DIAGNOSE_CYCLE_MS) && g_state.kl_15 && !recent(g_diagnoseHeardAt, MIB_YIELD_MS)) {
    uint8_t frame[MQB_DLC_DIAGNOSE_01];
    buildDiagnose01(frame, g_clock);
    sendFrame(MQB_ID_DIAGNOSE_01, MQB_DLC_DIAGNOSE_01, frame);
  }
#endif

#if ENABLE_VIN
  // The three frames of VIN_01 in turn, whatever the ignition: a car's gateway
  // sends them for as long as the bus is awake. The next one follows only a
  // frame that went out, so the three stay in order.
  if (due(g_nextVinAt, VIN_CYCLE_MS) && g_vinOn && !recent(g_vinHeardAt, MIB_YIELD_MS)) {
    uint8_t frame[MQB_DLC_VIN_01];
    buildVin01(frame, g_vinMux, g_vin);
    if (sendFrame(MQB_ID_VIN_01, MQB_DLC_VIN_01, frame)) {
      g_vinMux = (uint8_t)((g_vinMux + 1) % MQB_VIN_FRAMES);
    }
  }
#endif

#if ENABLE_LCD
  serviceLcd();   // last, so a frame that is due goes out first
#endif
}
