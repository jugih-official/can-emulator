// ---------------------------------------------------------------------------
// VW MQB Klemme 15 emulator -- Arduino Uno + MCP2515
//
// Broadcasts Klemmen_Status_01 (0x3C0) at 100 ms onto the Komfort-/
// Infotainment-CAN so that modules on a bench harness see "ignition on"
// without a body control module, steering column module (J527) or immobiliser
// being present. Optionally also sends the dimming and clock messages a MIB
// head unit expects.
//
// Push button on BUTTON_PIN toggles ignition. Serial console at 115200 gives
// the same control plus a sniffer. See README.md for wiring.
// ---------------------------------------------------------------------------
#include <SPI.h>
#include <mcp_can.h>

#include "config.h"
#include "mqb_klemmen.h"
#include "mqb_mib.h"

static MCP_CAN CAN(MCP_CS_PIN);

static KlemmenState g_state;
static uint8_t  g_counter = 0;
static bool     g_trace   = TRACE_TX_DEFAULT;
static bool     g_busOk   = false;
static uint32_t g_txOk    = 0;
static uint32_t g_txErr   = 0;

#if ENABLE_MIB_MESSAGES
static DimmungState g_dimmung;
static ClockState   g_clock;
static uint32_t g_nextDimmungAt  = 0;
static uint32_t g_nextDiagnoseAt = 0;
static uint32_t g_nextClockAt    = 0;
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

// ---------------------------------------------------------------------------

static bool sendFrame(uint16_t id, uint8_t dlc, uint8_t *data) {
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

static void setIgnition(bool on) {
  g_state.kl_s  = on;   // the key is "inserted" whenever ignition is on
  g_state.kl_15 = on;
  if (!on) {
    g_state.kl_50 = false;
    g_kl50EndsAt  = 0;
  }
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
#if ENABLE_SERIAL_CONTROL
  Serial.print(F("Klemme 15: "));
  Serial.println(on ? F("ON") : F("OFF"));
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
  if (CAN.checkReceive() != CAN_MSGAVAIL) return;

  long unsigned int id = 0;
  unsigned char dlc = 0;
  unsigned char buf[8];
  if (CAN.readMsgBuf(&id, &dlc, buf) != CAN_OK) return;

  uint32_t now = millis();
  uint16_t sid = (uint16_t)(id & 0x7FF);

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

#if ENABLE_SERIAL_CONTROL
  if (g_sniff) {
    Serial.print(F("RX "));
    Serial.print(sid, HEX);
    Serial.print(' ');
    for (uint8_t i = 0; i < dlc; i++) {
      if (buf[i] < 0x10) Serial.print('0');
      Serial.print(buf[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
  }
#endif
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
#endif
  Serial.print(F("  tx ok    : ")); Serial.println(g_txOk);
  Serial.print(F("  tx err   : ")); Serial.println(g_txErr);
  Serial.println(F("--------------"));
}

static void printHelp() {
  Serial.println(F("1/0 ign on/off   t toggle ign   x toggle Kl X   5 pulse Kl 50"));
  Serial.println(F("s status   d toggle tx trace   r toggle rx dump   p rx stats"));
  Serial.println(F("c clear rx stats   +/- dimming   h help"));
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
    case 'c': g_statCount = 0; g_statsFull = false;
              Serial.println(F("rx stats cleared")); break;
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

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

#if ENABLE_SERIAL_CONTROL
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { /* brief wait on native-USB boards */ }
  Serial.println();
  Serial.println(F("VW MQB Klemme 15 emulator"));
  Serial.print(F("checksum self-test: "));
  Serial.println(mqbChecksumSelfTest() ? F("PASS") : F("FAIL"));
#endif

  g_busOk = (CAN.begin(MCP_ANY, MCP_BITRATE, MCP_CRYSTAL) == CAN_OK);
  if (g_busOk) {
    // Without this the controller stays in configuration/listen-only mode and
    // silently transmits nothing.
    CAN.setMode(MCP_NORMAL);
  }

#if ENABLE_SERIAL_CONTROL
  Serial.print(F("MCP2515 init: "));
  Serial.println(g_busOk ? F("ok") : F("FAILED -- check wiring and crystal"));
  printHelp();
#endif

  uint32_t now = millis();
  g_nextKlemmenAt = now;
#if ENABLE_MIB_MESSAGES
  g_nextDimmungAt  = now;
  g_nextDiagnoseAt = now;
  g_nextClockAt    = now;
#endif
}

void loop() {
  serviceButton();
  serviceSniffer();
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
  if (due(g_nextDimmungAt, DIMMUNG_CYCLE_MS) && g_state.kl_15) {
    uint8_t frame[MQB_DLC_DIMMUNG_01];
    buildDimmung01(frame, g_dimmung);
    sendFrame(MQB_ID_DIMMUNG_01, MQB_DLC_DIMMUNG_01, frame);
  }
  if (due(g_nextDiagnoseAt, DIAGNOSE_CYCLE_MS) && g_state.kl_15) {
    uint8_t frame[MQB_DLC_DIAGNOSE_01];
    buildDiagnose01(frame, g_clock);
    sendFrame(MQB_ID_DIAGNOSE_01, MQB_DLC_DIAGNOSE_01, frame);
  }
#endif
}
