#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Additional cyclic messages a MIB head unit generally expects on the
// Infotainment-CAN beyond Klemmen_Status_01. Without these the unit typically
// comes up but complains, shows a wrong clock, or sits at full display
// brightness.
//
// Layouts taken from vw_mqb_2010.dbc (commaai/opendbc). Neither message carries
// a checksum or a counter, so they are simple to synthesise.
// ---------------------------------------------------------------------------

// Panel/display dimming. 0x5F0 (1520), DLC 8.
#define MQB_ID_DIMMUNG_01    0x5F0
#define MQB_DLC_DIMMUNG_01   8

// Odometer + date/time, which is where the MIB gets its clock. 0x6B2 (1714),
// DLC 8. Named Diagnose_01 in the DBC; the UH_* signals are the "Uhrzeit".
#define MQB_ID_DIAGNOSE_01   0x6B2
#define MQB_DLC_DIAGNOSE_01  8

// Generic little-endian (Intel) signal packer: writes the low `len` bits of
// `value` into `buf` starting at DBC start bit `startBit`. Bits outside the
// signal are left untouched, so a frame is built by zeroing it then packing
// each signal in turn.
void packLE(uint8_t *buf, uint8_t startBit, uint8_t len, uint32_t value);

struct DimmungState {
  uint8_t  kl58xd      = 100;    // dimming setpoint, 0..253
  uint8_t  kl58xs      = 100;    // %
  uint8_t  kl58xt      = 100;    // %
  bool     nachtdesign = false;  // night design active
  uint16_t fotosensor  = 0;      // ambient light sensor raw
};
void buildDimmung01(uint8_t *buf, const DimmungState &s);

struct ClockState {
  uint16_t year   = 2026;   // 2000..2127
  uint8_t  month  = 1;      // 1..12
  uint8_t  day    = 1;      // 1..31
  uint8_t  hour   = 0;      // 0..23
  uint8_t  minute = 0;      // 0..59
  uint8_t  second = 0;      // 0..59
  uint32_t odometer_km = 0; // 0..1048573
};
void buildDiagnose01(uint8_t *buf, const ClockState &s);

// Advances the clock by one second, handling minute/hour/day rollover.
// Month/year rollover is intentionally not implemented -- a bench clock that
// runs for weeks is not a goal.
void tickClock(ClockState &s);
