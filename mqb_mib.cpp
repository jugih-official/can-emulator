#include "mqb_mib.h"

void packLE(uint8_t *buf, uint8_t startBit, uint8_t len, uint32_t value) {
  for (uint8_t i = 0; i < len; i++) {
    if (value & ((uint32_t)1 << i)) {
      uint8_t bit = (uint8_t)(startBit + i);
      buf[bit >> 3] |= (uint8_t)(1 << (bit & 7));
    }
  }
}

void buildDimmung01(uint8_t *buf, const DimmungState &s) {
  for (uint8_t i = 0; i < MQB_DLC_DIMMUNG_01; i++) buf[i] = 0;
  packLE(buf,  0,  8, s.kl58xd);
  packLE(buf,  8,  7, s.kl58xs);
  packLE(buf, 15,  1, s.nachtdesign ? 1 : 0);
  packLE(buf, 16,  7, s.kl58xt);
  packLE(buf, 24, 16, s.fotosensor);
}

void buildDiagnose01(uint8_t *buf, const ClockState &s) {
  for (uint8_t i = 0; i < MQB_DLC_DIAGNOSE_01; i++) buf[i] = 0;
  packLE(buf,  0,  8, 0);                      // DGN_Verlernzaehler
  packLE(buf,  8, 20, s.odometer_km);
  packLE(buf, 28,  7, (uint32_t)(s.year - 2000));  // offset 2000 per DBC
  packLE(buf, 35,  4, s.month);
  packLE(buf, 39,  5, s.day);
  packLE(buf, 44,  5, s.hour);
  packLE(buf, 49,  6, s.minute);
  packLE(buf, 55,  6, s.second);
}

void tickClock(ClockState &s) {
  if (++s.second < 60) return;
  s.second = 0;
  if (++s.minute < 60) return;
  s.minute = 0;
  if (++s.hour < 24) return;
  s.hour = 0;
  s.day++;                 // deliberately no month rollover, see header
}
