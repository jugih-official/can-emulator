#include <cstdio>
#include <cstring>
#include "mqb_klemmen.h"
#include "mqb_mib.h"
#include "mqb_vin.h"

static int g_fail = 0;
static void check(bool ok, const char *what) {
  if (!ok) { printf("  FAIL: %s\n", what); g_fail++; }
}

// Mirror of packLE, used to read signals back out of a built frame.
static uint32_t unpackLE(const uint8_t *buf, uint8_t startBit, uint8_t len) {
  uint32_t v = 0;
  for (uint8_t i = 0; i < len; i++) {
    uint8_t bit = startBit + i;
    if (buf[bit >> 3] & (1 << (bit & 7))) v |= (uint32_t)1 << i;
  }
  return v;
}

static KlemmenState mk(bool s, bool k15, bool x, bool k50) {
  KlemmenState st; st.kl_s=s; st.kl_15=k15; st.kl_x=x; st.kl_50=k50; return st;
}

static void testKlemmen() {
  printf("Klemmen_Status_01 (0x3C0)\n");
  check(mqbChecksumSelfTest(), "checksum matches 16 published reference values");

  uint8_t f[4];
  buildKlemmenStatus01(f, mk(true, true, false, false), 5);
  check(f[1] == 0x05, "counter in byte 1");
  check(f[2] == 0x03, "Kl_S|Kl_15 == 0x03");
  check(f[3] == 0x00, "byte 3 reserved zero");

  buildKlemmenStatus01(f, mk(false, false, true, true), 0);
  check(f[2] == 0x0C, "Kl_X|Kl_50 == 0x0C");

  // Counter must only use the low nibble.
  buildKlemmenStatus01(f, mk(true, true, false, false), 0x15);
  check(f[1] == 0x05, "counter masked to low nibble");

  const struct { const char *name; KlemmenState s; } cases[] = {
    {"ign OFF (Kl30 only)", mk(false,false,false,false)},
    {"ign ON  (S+15)",      mk(true, true, false,false)},
    {"cranking (S+15+50)",  mk(true, true, false,true )},
  };
  for (auto &c : cases) {
    printf("  %-22s:", c.name);
    for (int ctr = 0; ctr < 4; ctr++) {
      uint8_t b[4]; buildKlemmenStatus01(b, c.s, ctr);
      printf("  %02X %02X %02X %02X", b[0], b[1], b[2], b[3]);
    }
    printf("  ...\n");
  }
}

static void testDimmung() {
  printf("Dimmung_01 (0x5F0)\n");
  DimmungState d;
  d.kl58xd = 200; d.kl58xs = 77; d.kl58xt = 61;
  d.nachtdesign = true; d.fotosensor = 0xBEEF;

  uint8_t f[8];
  buildDimmung01(f, d);
  check(unpackLE(f,  0,  8) == 200,    "DI_KL_58xd round-trips");
  check(unpackLE(f,  8,  7) == 77,     "DI_KL_58xs round-trips");
  check(unpackLE(f, 15,  1) == 1,      "DI_Display_Nachtdesign round-trips");
  check(unpackLE(f, 16,  7) == 61,     "DI_KL_58xt round-trips");
  check(unpackLE(f, 24, 16) == 0xBEEF, "DI_Fotosensor round-trips");
  printf("  frame: ");
  for (int i = 0; i < 8; i++) printf("%02X ", f[i]);
  printf("\n");
}

static void testDiagnose() {
  printf("Diagnose_01 (0x6B2)\n");
  ClockState c;
  c.year = 2026; c.month = 8; c.day = 22;
  c.hour = 16; c.minute = 45; c.second = 33;
  c.odometer_km = 123456;

  uint8_t f[8];
  buildDiagnose01(f, c);
  check(unpackLE(f,  8, 20) == 123456,  "KBI_Kilometerstand round-trips");
  check(unpackLE(f, 28,  7) == 26,      "UH_Jahr encodes with +2000 offset");
  check(unpackLE(f, 35,  4) == 8,       "UH_Monat round-trips");
  check(unpackLE(f, 39,  5) == 22,      "UH_Tag round-trips");
  check(unpackLE(f, 44,  5) == 16,      "UH_Stunde round-trips");
  check(unpackLE(f, 49,  6) == 45,      "UH_Minute round-trips");
  check(unpackLE(f, 55,  6) == 33,      "UH_Sekunde round-trips");
  printf("  frame: ");
  for (int i = 0; i < 8; i++) printf("%02X ", f[i]);
  printf("\n");

  // Signals must not bleed into each other: max out every field at once.
  ClockState m;
  m.year = 2127; m.month = 12; m.day = 31;
  m.hour = 23; m.minute = 59; m.second = 59; m.odometer_km = 1048573;
  buildDiagnose01(f, m);
  check(unpackLE(f,  8, 20) == 1048573, "max odometer isolated");
  check(unpackLE(f, 28,  7) == 127,     "max year isolated");
  check(unpackLE(f, 35,  4) == 12,      "max month isolated");
  check(unpackLE(f, 39,  5) == 31,      "max day isolated");
  check(unpackLE(f, 44,  5) == 23,      "max hour isolated");
  check(unpackLE(f, 49,  6) == 59,      "max minute isolated");
  check(unpackLE(f, 55,  6) == 59,      "max second isolated");
}

static void testClockTick() {
  printf("clock rollover\n");
  ClockState c;
  c.hour = 23; c.minute = 59; c.second = 58; c.day = 5;
  tickClock(c);
  check(c.second == 59 && c.hour == 23, "plain second tick");
  tickClock(c);
  check(c.second == 0 && c.minute == 0 && c.hour == 0 && c.day == 6,
        "cascades second -> minute -> hour -> day");
}

static void testVin() {
  printf("VIN_01 (0x6B4)\n");
  // Not a real car's: the bench's own, with a character at every place told apart.
  const char vin[] = "BENCH0123456789AZ";
  uint8_t f[8];

  buildVin01(f, 0, vin);
  check(f[0] == 0, "frame 0: multiplexer");
  check(f[1] == 0 && f[2] == 0 && f[3] == 0 && f[4] == 0, "frame 0: component-protection bytes are zero");
  check(memcmp(f + 5, "BEN", 3) == 0, "frame 0: characters 1..3 in bytes 5..7");
  printf("  frames:");
  for (int i = 0; i < 8; i++) printf(" %02X", f[i]);

  buildVin01(f, 1, vin);
  check(f[0] == 1, "frame 1: multiplexer");
  check(memcmp(f + 1, "CH01234", 7) == 0, "frame 1: characters 4..10 in bytes 1..7");
  printf("  |");
  for (int i = 0; i < 8; i++) printf(" %02X", f[i]);

  buildVin01(f, 2, vin);
  check(f[0] == 2, "frame 2: multiplexer");
  check(memcmp(f + 1, "56789AZ", 7) == 0, "frame 2: characters 11..17 in bytes 1..7");
  printf("  |");
  for (int i = 0; i < 8; i++) printf(" %02X", f[i]);
  printf("\n");

  check(vinChar('7') == '7' && vinChar('W') == 'W', "digits and capitals pass");
  check(vinChar('w') == 'W', "lower case is taken as upper case");
  check(vinChar('I') == 0 && vinChar('o') == 0 && vinChar('Q') == 0, "I, O and Q are not VIN characters");
  check(vinChar('-') == 0 && vinChar('\n') == 0 && vinChar(' ') == 0 && vinChar(0) == 0, "nor is anything else");
}

int main() {
  testKlemmen();
  testDimmung();
  testDiagnose();
  testClockTick();
  testVin();
  printf("\n%s\n", g_fail ? "TESTS FAILED" : "all tests passed");
  return g_fail ? 1 : 0;
}
