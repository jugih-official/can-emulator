// ---------------------------------------------------------------------------
// Simulator for can-emulator.ino.
//
// The sketch itself is compiled for the host, unchanged, against the mocks in
// mock/. This file is the other side of those mocks -- time, the button, the
// MCP2515, the LCD's memory, the EEPROM -- plus a small model of the bench:
// a gateway, a radio, a tester and a car's own ignition frame, each of which
// can be switched on and off.
//
// It is driven by lines on stdin and answers each with one line of JSON
// (or, with --text, with the two LCD lines as text):
//   run <ms>            let that much time pass
//   btn 0|1             release / hold the button
//   press short|long    a whole press
//   ser <text>          type into the serial console
//   set <name> <value>  gateway, radio, tester, car, power: 0|1
//                       volt: tenths of a volt   knob: 0..255
//   state               just report
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "Arduino.h"
#include "EEPROM.h"
#include "LiquidCrystal.h"
#include "SPI.h"
#include "mcp_can.h"
#include "sim.h"
#include "../config.h"

void setup();
void loop();

Sim sim;
SerialMock Serial;
SPIClass SPI;
EEPROMClass EEPROM;

// --- Arduino core ------------------------------------------------------------
uint32_t millis() { return sim.now; }
void pinMode(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) {
  if (pin == BUTTON_PIN) return sim.buttonDown ? LOW : HIGH;
  return HIGH;
}

int SerialMock::available() { return (int)sim.serialIn.size(); }
int SerialMock::read() {
  if (sim.serialIn.empty()) return -1;
  char c = sim.serialIn.front();
  sim.serialIn.pop_front();
  return (unsigned char)c;
}
size_t SerialMock::write(uint8_t c) { sim.serialOut += (char)c; return 1; }
size_t SerialMock::write(const uint8_t *buf, size_t n) { sim.serialOut.append((const char *)buf, n); return n; }
size_t SerialMock::print(const char *s) { sim.serialOut += s; return strlen(s); }
size_t SerialMock::print(char c) { sim.serialOut += c; return 1; }
size_t SerialMock::print(int v, int base) { return print((long)v, base); }
size_t SerialMock::print(long v, int base) {
  if (base == DEC && v < 0) { print('-'); return 1 + number((unsigned long)-v, base); }
  return number((unsigned long)v, base);
}
size_t SerialMock::number(unsigned long v, int base) {
  char buf[40];
  int i = sizeof buf;
  buf[--i] = 0;
  do { buf[--i] = "0123456789ABCDEF"[v % base]; v /= base; } while (v);
  return print(buf + i);
}

// --- EEPROM ------------------------------------------------------------------
void Sim::saveEeprom() {
  if (eepromPath.empty()) return;
  if (FILE *f = fopen(eepromPath.c_str(), "wb")) { fwrite(eeprom, 1, sizeof eeprom, f); fclose(f); }
}
uint8_t EEPROMClass::read(int addr) { return sim.eeprom[addr & 1023]; }
void EEPROMClass::update(int addr, uint8_t value) {
  if (sim.eeprom[addr & 1023] == value) return;
  sim.eeprom[addr & 1023] = value;
  sim.saveEeprom();
}

// --- LCD ---------------------------------------------------------------------
// Talking to the display takes time, during which the sketch does nothing
// else: about 0.3 ms per character or command in 4-bit mode.
static void lcdBusy() { sim.us += 300; sim.now = (uint32_t)(sim.us / 1000); }
void LiquidCrystal::begin(uint8_t, uint8_t) { clear(); }
void LiquidCrystal::clear() { memset(sim.ddram, ' ', sizeof sim.ddram); sim.row = sim.col = 0; }
void LiquidCrystal::setCursor(uint8_t col, uint8_t row) { sim.col = col; sim.row = row & 1; lcdBusy(); }
size_t LiquidCrystal::write(uint8_t c) {
  if (sim.col < 16) sim.ddram[sim.row][sim.col] = c;
  sim.col++;
  lcdBusy();
  return 1;
}
void LiquidCrystal::createChar(uint8_t n, uint8_t rows[]) { memcpy(sim.cgram[n & 7], rows, 8); }

// --- MCP2515 -----------------------------------------------------------------
// The library side: set-up and transmit.
byte MCP_CAN::begin(byte, byte, byte) {
  if (!sim.power) return CAN_FAILINIT;
  sim.mode = 0x80;
  sim.eflg = 0;
  sim.rx.clear();
  return CAN_OK;
}
byte MCP_CAN::setMode(byte mode) {
  if (!sim.power) return 1;
  sim.mode = mode;
  return CAN_OK;
}
byte MCP_CAN::sendMsgBuf(unsigned long id, byte, byte len, byte *buf) {
  if (!sim.power || sim.mode != MCP_NORMAL || !sim.ack) return CAN_SENDMSGTIMEOUT;
  SimTx &t = sim.tx[id];
  t.count++;
  t.at  = sim.now;
  t.dlc = len;
  memcpy(t.data, buf, len);
  return CAN_OK;
}

// The SPI side: the three instructions the sketch sends itself. A transaction
// runs from chip select going low to it going high again.
static bool    g_selected = false;
static uint8_t g_spiBytes[16];
static uint8_t g_spiCount = 0;
static uint8_t g_spiReply[16];

void digitalWrite(uint8_t pin, uint8_t value) {
  if (pin != MCP_CS_PIN) return;
  if (value == LOW) { g_selected = true; g_spiCount = 0; return; }
  // Deselecting after READ RX BUFFER is what frees the buffer.
  if (g_selected && g_spiCount > 0 && g_spiBytes[0] == 0x90 && sim.power && !sim.rx.empty()) sim.rx.pop_front();
  g_selected = false;
}

static uint8_t mcpRegister(uint8_t addr) {
  switch (addr) {
    case 0x0E: return sim.mode;                 // CANSTAT
    case 0x1C: return sim.ack ? 0 : 128;        // TEC: error passive when nobody acknowledges
    case 0x1D: return 0;                        // REC
    case 0x2D: return sim.eflg;                 // EFLG
    default:   return 0;
  }
}

uint8_t SPIClass::transfer(uint8_t out) {
  if (!g_selected || g_spiCount >= sizeof g_spiBytes) return 0xFF;
  uint8_t n = g_spiCount;
  g_spiBytes[g_spiCount++] = out;
  if (!sim.power) return 0xFF;                  // nothing drives MISO
  uint8_t instruction = g_spiBytes[0];
  if (n == 0) {
    if (instruction == 0x90 && !sim.rx.empty()) {
      const SimFrame &f = sim.rx.front();
      memset(g_spiReply, 0, sizeof g_spiReply);
      if (f.ext) {
        g_spiReply[0] = (uint8_t)(f.id >> 21);
        g_spiReply[1] = (uint8_t)(((f.id >> 18) & 0x07) << 5) | 0x08 | (uint8_t)((f.id >> 16) & 0x03);
        g_spiReply[2] = (uint8_t)(f.id >> 8);
        g_spiReply[3] = (uint8_t)f.id;
      } else {
        g_spiReply[0] = (uint8_t)(f.id >> 3);
        g_spiReply[1] = (uint8_t)((f.id & 0x07) << 5);
      }
      g_spiReply[4] = f.dlc;
      memcpy(g_spiReply + 5, f.data, 8);
    }
    return 0;
  }
  if (instruction == 0xA0) return sim.rx.empty() ? 0x00 : 0x01;      // READ STATUS: RX0IF
  if (instruction == 0x03) return n == 1 ? 0 : mcpRegister((uint8_t)(g_spiBytes[1] + n - 2));
  if (instruction == 0x90) return g_spiReply[n - 1];
  return 0;
}

// --- the bench ---------------------------------------------------------------
enum Node { GATEWAY, RADIO, TESTER, CAR, NODE_COUNT };
static bool    g_node[NODE_COUNT] = { true, false, false, false };
static uint8_t g_voltDv = 120;
static uint8_t g_knob   = 0;

struct Periodic {
  Node     node;
  uint32_t id;
  bool     ext;
  uint16_t period;
  uint8_t  data[8];
  uint32_t next;
  uint8_t  counter;
  uint32_t sent;          // frames put on the wire, and the latest of them
  uint8_t  last[8];
  uint8_t  dlc;
};

// The gateway's frames and rates are the ones measured on the bench; the
// payloads are the captured ones where one was captured, zeros otherwise.
static std::vector<Periodic> g_frames = {
  { GATEWAY, 0x3DC,       false,   50, { 0xFF, 0x01, 0, 0, 0, 0x01, 0, 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x5AC,       false,  100, { 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x663,       false,  100, { 0x78, 0x28, 0x20, 0x0E, 0x1F, 0x9B, 0x2C, 0x00 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x48B,       false,  100, { 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x3DA,       false,  100, { 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x3EB,       false,  100, { 0xFF, 0xFE, 0, 0, 0, 0, 0, 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x569,       false,  500, { 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x585,       false, 1000, { 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x6B2,       false, 1000, { 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x386,       false, 1000, { 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x5F5,       false, 1000, { 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x5F7,       false, 1000, { 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x1B000010,  true,   200, { 0x10, 0x04, 0, 0, 0, 0, 0, 0 }, 0, 0, 0, { 0 }, 0 },
  { GATEWAY, 0x17332310,  true,  1000, { 0 }, 0, 0, 0, { 0 }, 0 },
  // An invented radio: network management as node 5F, and one frame whose
  // byte 2 follows the "knob", for trying the learning view.
  { RADIO,   0x1B00005F,  true,   200, { 0x5F, 0x04, 0, 0, 0, 0, 0, 0 }, 0, 0, 0, { 0 }, 0 },
  { RADIO,   0x3E5,       false,  100, { 0 }, 0, 0, 0, { 0 }, 0 },
  { TESTER,  0x700,       false, 2000, { 0x02, 0x3E, 0x80, 0x55, 0x55, 0x55, 0x55, 0x55 }, 0, 0, 0, { 0 }, 0 },
  // A complete car's own terminal status and dimming, for listen-only mode.
  { CAR,     0x3C0,       false,  100, { 0x00, 0x00, 0x03, 0x00, 0, 0, 0, 0 }, 0, 0, 0, { 0 }, 0 },
  { CAR,     0x5F0,       false,  500, { 0 }, 0, 0, 0, { 0 }, 0 },
};

// Frames that are due go onto the wire one after the other, about a quarter
// of a millisecond each at 500 kbit/s, and from there into the controller's
// two receive buffers. A third frame arriving before the sketch has fetched
// one is lost, as on the real chip.
static std::deque<SimFrame> g_wire;
static uint64_t g_wireFreeAt = 0;
#define FRAME_US 250

static void busTick() {
  sim.ack = g_node[GATEWAY] || g_node[RADIO] || g_node[TESTER] || g_node[CAR];
  while (!g_wire.empty() && g_wireFreeAt <= sim.us) {
    if (g_wireFreeAt < sim.us) g_wireFreeAt = sim.us;           // the bus was idle
    g_wireFreeAt += FRAME_US;
    if (sim.power && sim.mode != 0x80) {
      if (sim.rx.size() < 2) sim.rx.push_back(g_wire.front());
      else sim.eflg |= 0x40;                                    // both buffers full: lost
    }
    g_wire.pop_front();
  }
  static uint32_t lastMs = 0;
  if (sim.now == lastMs) return;                                // schedules run per millisecond
  lastMs = sim.now;
  for (Periodic &p : g_frames) {
    if (!g_node[p.node]) { p.next = sim.now + p.period; continue; }
    if ((int32_t)(sim.now - p.next) < 0) continue;
    p.next = sim.now + p.period;
    p.counter++;

    SimFrame f;
    f.id  = p.id;
    f.ext = p.ext;
    f.dlc = p.id == 0x3C0 ? 4 : 8;
    memcpy(f.data, p.data, 8);
    if (p.id == 0x663) f.data[0] = g_voltDv;
    if (p.id == 0x5AC) f.data[1] = p.counter;            // changes by itself
    if (p.id == 0x3C0) f.data[1] = p.counter & 0x0F;
    if (p.id == 0x3E5) { f.data[0] = p.counter & 0x0F; f.data[2] = g_knob; }

    g_wire.push_back(f);
    p.sent++;
    p.dlc = f.dlc;
    memcpy(p.last, f.data, 8);
  }
}

// One pass of loop() is taken to cost 50 microseconds, plus whatever the LCD
// adds. That is in the range of the real board, where a pass is a handful of
// SPI transfers.
#define LOOP_US 50

static void run(uint32_t ms) {
  uint64_t until = sim.us + (uint64_t)ms * 1000;
  while (sim.us < until) {
    sim.us += LOOP_US;
    sim.now = (uint32_t)(sim.us / 1000);
    busTick();
    loop();
  }
}

// --- reporting ---------------------------------------------------------------
static std::string jsonString(const std::string &s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c == '\n') o += "\\n";
    else if (c == '\r') ;
    else if (c < 0x20 || c > 0x7E) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  return o + "\"";
}

static void reportJson() {
  std::string o = "{\"t\":" + std::to_string(sim.now) + ",\"lcd\":[";
  for (int r = 0; r < 2; r++) {
    o += r ? ",[" : "[";
    for (int c = 0; c < 16; c++) o += (c ? "," : "") + std::to_string(sim.ddram[r][c]);
    o += "]";
  }
  o += "],\"cg\":[";
  for (int n = 0; n < 8; n++) {
    o += n ? ",[" : "[";
    for (int r = 0; r < 8; r++) o += (r ? "," : "") + std::to_string(sim.cgram[n][r]);
    o += "]";
  }
  o += "],\"mode\":";
  o += !sim.power ? "\"no power\"" : sim.mode == 0x00 ? "\"normal\"" : sim.mode == 0x60 ? "\"listen-only\"" : "\"configuration\"";
  o += ",\"tx\":[";
  bool first = true;
  for (const auto &kv : sim.tx) {
    char b[160], hex[24] = "";
    for (int i = 0; i < kv.second.dlc; i++) snprintf(hex + 2 * i, 3, "%02X", kv.second.data[i]);
    snprintf(b, sizeof b, "%s{\"id\":\"%lX\",\"n\":%lu,\"age\":%lu,\"data\":\"%s\"}", first ? "" : ",",
             (unsigned long)kv.first, (unsigned long)kv.second.count,
             (unsigned long)(sim.now - kv.second.at), hex);
    o += b;
    first = false;
  }
  static const char *const who[NODE_COUNT] = { "gateway", "radio", "tester", "car" };
  o += "],\"bus\":[";
  first = true;
  for (const Periodic &p : g_frames) {
    if (!g_node[p.node] || !p.sent) continue;
    char row[160], hex[24] = "";
    for (int i = 0; i < p.dlc; i++) snprintf(hex + 2 * i, 3, "%02X", p.last[i]);
    snprintf(row, sizeof row, "%s{\"from\":\"%s\",\"id\":\"%lX\",\"n\":%lu,\"data\":\"%s\"}", first ? "" : ",",
             who[p.node], (unsigned long)p.id, (unsigned long)p.sent, hex);
    o += row;
    first = false;
  }
  char b[200];
  snprintf(b, sizeof b, "],\"bench\":{\"gateway\":%d,\"radio\":%d,\"tester\":%d,\"car\":%d,\"power\":%d,\"volt\":%d,\"knob\":%d,\"btn\":%d}",
           g_node[GATEWAY], g_node[RADIO], g_node[TESTER], g_node[CAR], sim.power, g_voltDv, g_knob, sim.buttonDown);
  o += b;
  o += ",\"ser\":" + jsonString(sim.serialOut) + "}";
  sim.serialOut.clear();
  puts(o.c_str());
  fflush(stdout);
}

static void reportText() {
  // The icons' two halves as Bb Kk Dd Oo. Codes 8..15 are the user characters
  // once more, so 8 is user character 0.
  static const char icons[] = "oBbKkDdO";
  for (int r = 0; r < 2; r++) {
    putchar('|');
    for (int c = 0; c < 16; c++) {
      uint8_t ch = sim.ddram[r][c];
      putchar(ch < 16 ? icons[ch & 7] : ch);
    }
    puts("|");
  }
  if (!sim.serialOut.empty()) {
    for (char c : sim.serialOut) if (c != '\r') putchar(c);
    sim.serialOut.clear();
  }
  fflush(stdout);
}

static void command(const std::string &line) {
  char name[32] = "";
  long value = 0;
  if (sscanf(line.c_str(), "run %ld", &value) == 1) {
    run((uint32_t)value);
  } else if (sscanf(line.c_str(), "btn %ld", &value) == 1) {
    sim.buttonDown = value != 0;
  } else if (sscanf(line.c_str(), "press %31s", name) == 1) {
    sim.buttonDown = true;
    run(!strcmp(name, "long") ? BUTTON_LONG_MS + 200 : 150);
    sim.buttonDown = false;
    run(300);                                   // long enough for the display to follow
  } else if (line.compare(0, 4, "ser ") == 0) {
    for (char c : line.substr(4)) sim.serialIn.push_back(c);
    run(20);
  } else if (sscanf(line.c_str(), "set %31s %ld", name, &value) == 2) {
    if      (!strcmp(name, "gateway")) g_node[GATEWAY] = value;
    else if (!strcmp(name, "radio"))   g_node[RADIO]   = value;
    else if (!strcmp(name, "tester"))  g_node[TESTER]  = value;
    else if (!strcmp(name, "car"))     g_node[CAR]     = value;
    else if (!strcmp(name, "volt"))    g_voltDv        = (uint8_t)value;
    else if (!strcmp(name, "knob"))    g_knob          = (uint8_t)value;
    else if (!strcmp(name, "power")) {
      sim.power = value;
      if (sim.power) { sim.mode = 0x80; sim.rx.clear(); }   // a chip that just got power is in configuration mode
    }
  }
}

int main(int argc, char **argv) {
  bool text = argc > 1 && !strcmp(argv[1], "--text");
  memset(sim.eeprom, 0xFF, sizeof sim.eeprom);
  memset(sim.ddram, ' ', sizeof sim.ddram);
  memset(sim.cgram, 0, sizeof sim.cgram);
  if (const char *path = getenv("SIM_EEPROM")) {
    sim.eepromPath = path;
    if (FILE *f = fopen(path, "rb")) { size_t n = fread(sim.eeprom, 1, sizeof sim.eeprom, f); (void)n; fclose(f); }
  }

  setup();
  char buf[512];
  while (fgets(buf, sizeof buf, stdin)) {
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (text && !line.empty()) printf("> %s\n", line.c_str());
    command(line);
    if (text) reportText(); else reportJson();
  }
  return 0;
}
