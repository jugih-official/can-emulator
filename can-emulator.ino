// ---------------------------------------------------------------------------
// VW MQB Klemme 15 emulator -- Arduino Uno + MCP2515
//
// Broadcasts Klemmen_Status_01 (0x3C0) at 100 ms onto the Komfort-/
// Infotainment-CAN so that modules on a bench harness see "ignition on"
// without a body control module, steering column module (J527) or immobiliser
// being present. Optionally also sends the dimming and clock messages a MIB
// head unit expects.
//
// Push button on BUTTON_PIN toggles ignition and a 16x2 LCD shows the state
// and the counters. Serial console at 115200 gives the same control plus a
// sniffer, and 'l' makes the controller listen-only for sniffing a complete
// car without disturbing it. See README.md for wiring.
// ---------------------------------------------------------------------------
#include <SPI.h>
#include <mcp_can.h>

#include "config.h"
#include "mqb_klemmen.h"
#include "mqb_mib.h"
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
// When something else on the bus last sent these IDs (0 = never). A gateway
// sends both itself; two senders of one ID with different payloads collide,
// so ours stand back for as long as the other one is heard.
static uint32_t g_dimmungHeardAt  = 0;
static uint32_t g_diagnoseHeardAt = 0;
#endif

static uint32_t g_nextKlemmenAt = 0;
static uint32_t g_kl50EndsAt    = 0;

// Button edge detection with debounce.
static bool     g_btnStable    = HIGH;   // pull-up: HIGH == released
static bool     g_btnLast      = HIGH;
static uint32_t g_btnChangedAt = 0;

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
static LiquidCrystal g_lcd(LCD_RS_PIN, LCD_E_PIN, LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN);
static char     g_lcdShown[2][16];   // what is on the glass, so only changes get written
static uint32_t g_nextLcdAt = 0;
#endif

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

#if ENABLE_SERIAL_CONTROL
// Listen-only also withholds the acknowledge bit, so the controller leaves no
// trace on the bus at all -- the mode for a real, running car.
static void setListenOnly(bool on) {
  g_listenOnly = on;
  if (g_busOk) CAN.setMode(on ? MCP_LISTENONLY : MCP_NORMAL);
  Serial.print(F("listen-only: "));
  Serial.println(on ? F("ON, transmitting nothing") : F("OFF"));
}
#endif

static void setIgnition(bool on) {
  g_state.kl_s  = on;   // the key is "inserted" whenever ignition is on
  g_state.kl_15 = on;
  if (!on) {
    g_state.kl_50 = false;
    g_kl50EndsAt  = 0;
  }
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

#if ENABLE_MIB_MESSAGES
static bool heardRecently(uint32_t at) {
  return at != 0 && millis() - at < MIB_YIELD_MS;
}
#endif

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

static void serviceButton() {
  bool raw = digitalRead(BUTTON_PIN);
  if (raw != g_btnLast) {
    g_btnLast      = raw;
    g_btnChangedAt = millis();
    return;
  }
  if (raw == g_btnStable) return;
  if (millis() - g_btnChangedAt < BUTTON_DEBOUNCE_MS) return;

  g_btnStable = raw;
  if (raw == LOW) setIgnition(!g_state.kl_15);   // pressed
}

static void serviceSniffer() {
  if (!g_busOk) return;

  uint32_t id = 0;
  bool extended = false;
  uint8_t dlc = 0;
  uint8_t buf[8];
  if (!mcpReadFrame(id, extended, dlc, buf)) return;
  g_rxCount++;

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
  if (extended) { g_rxExtended++; return; }

  uint32_t now = millis();
  uint16_t sid = (uint16_t)id;
#if ENABLE_MIB_MESSAGES
  // The controller does not receive its own frames, so these are someone else's.
  if (sid == MQB_ID_DIMMUNG_01)  g_dimmungHeardAt  = now | 1;
  if (sid == MQB_ID_DIAGNOSE_01) g_diagnoseHeardAt = now | 1;
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
    if (st->count > 0) {
      uint32_t d = now - st->lastMs;
      if (d > 0xFFFF) d = 0xFFFF;
      if (d < st->minDelta) st->minDelta = (uint16_t)d;
      if (d > st->maxDelta) st->maxDelta = (uint16_t)d;
    }
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
  Serial.print(heardRecently(g_dimmungHeardAt) ? F("bus sends it, ours off") : F("ours"));
  Serial.print(F(" / "));
  Serial.println(heardRecently(g_diagnoseHeardAt) ? F("bus sends it, ours off") : F("ours"));
#endif
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
}

static void serviceSerial() {
  if (!Serial.available()) return;
  switch (Serial.read()) {
    case '1': setIgnition(true);  break;
    case '0': setIgnition(false); break;
    case 't': setIgnition(!g_state.kl_15); break;
    case 'x': g_state.kl_x = !g_state.kl_x;
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
#if ENABLE_MIB_MESSAGES
    case '+': if (g_dimmung.kl58xd < 250) g_dimmung.kl58xd += 10;
              Serial.println(g_dimmung.kl58xd); break;
    case '-': if (g_dimmung.kl58xd > 10) g_dimmung.kl58xd -= 10;
              Serial.println(g_dimmung.kl58xd); break;
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

//   KL15 ON   X  50     terminal state, X and 50 while those are set
//   tx 123456 err  0    frames sent, and frames the controller could not send
// Listen-only shows what is on the bus instead, and a failed boot check takes
// the first line.
static void serviceLcd() {
  if (!due(g_nextLcdAt, LCD_REFRESH_MS)) return;
  char line[16];

  if (!g_selfTestOk)     lcdText(line, PSTR("CHECKSUM FAIL"));
  else if (!g_busOk)     lcdText(line, PSTR("MCP2515 FAILED"));
  else if (g_listenOnly) lcdText(line, PSTR("LISTEN-ONLY"));
  else {
    lcdText(line, g_state.kl_15 ? PSTR("KL15 ON") : PSTR("KL15 OFF"));
    if (g_state.kl_x)  line[10] = 'X';
    if (g_state.kl_50) { line[13] = '5'; line[14] = '0'; }
  }
  lcdLine(0, line);

  if (g_listenOnly) {
    lcdText(line, PSTR("rx        ids"));
    lcdNumber(line + 3, 6, g_rxCount);
    lcdNumber(line + 14, 2, g_statCount);
  } else {
    lcdText(line, PSTR("tx        err"));
    lcdNumber(line + 3, 6, g_txOk);
    lcdNumber(line + 13, 3, g_txErr);
  }
  lcdLine(1, line);
}
#endif

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
#if ENABLE_STATUS_LED
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
#endif
  g_selfTestOk = mqbChecksumSelfTest();

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
  if (g_listenOnly) Serial.println(F("listen-only: ON, transmitting nothing"));
  printHelp();
#endif

#if ENABLE_LCD
  g_lcd.begin(16, 2);                          // ends with a clear: all spaces
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
#if ENABLE_LCD
  g_nextLcdAt = now;
#endif
}

void loop() {
  serviceButton();
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
  if (due(g_nextDimmungAt, DIMMUNG_CYCLE_MS) && g_state.kl_15 && !heardRecently(g_dimmungHeardAt)) {
    uint8_t frame[MQB_DLC_DIMMUNG_01];
    buildDimmung01(frame, g_dimmung);
    sendFrame(MQB_ID_DIMMUNG_01, MQB_DLC_DIMMUNG_01, frame);
  }
  if (due(g_nextDiagnoseAt, DIAGNOSE_CYCLE_MS) && g_state.kl_15 && !heardRecently(g_diagnoseHeardAt)) {
    uint8_t frame[MQB_DLC_DIAGNOSE_01];
    buildDiagnose01(frame, g_clock);
    sendFrame(MQB_ID_DIAGNOSE_01, MQB_DLC_DIAGNOSE_01, frame);
  }
#endif

#if ENABLE_LCD
  serviceLcd();   // last, so a frame that is due goes out first
#endif
}
