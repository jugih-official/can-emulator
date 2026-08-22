#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// VW MQB "Klemmen_Status_01" -- terminal (Klemme) status broadcast.
//
//   CAN ID   0x3C0 (960)      Bus  Komfort-/Infotainment-CAN, 500 kbit/s
//   DLC      4                Cycle 100 ms
//   Sender   BCM / Gateway J533
//
//   byte 0   Klemmen_Status_01_CRC   AUTOSAR E2E checksum over bytes 1..3
//   byte 1   Klemmen_Status_01_BZ    message counter, low nibble, 0x0..0xF
//   byte 2   bit 0  ZAS_Kl_S                key inserted / terminal S
//            bit 1  ZAS_Kl_15               ignition on  / terminal 15
//            bit 2  ZAS_Kl_X                load shedding / terminal X
//            bit 3  ZAS_Kl_50_Startanf.     starter request / terminal 50
//            bits 4..7 unused, zero
//   byte 3   reserved, zero
//
// Note there is no Klemme 30 signal. Terminal 30 is permanent battery voltage,
// a physical rail -- it cannot be emulated over CAN, only supplied.
// ---------------------------------------------------------------------------

#define MQB_ID_KLEMMEN_STATUS_01   0x3C0
#define MQB_DLC_KLEMMEN_STATUS_01  4

// Bit positions inside byte 2.
#define KL_S_BIT   0
#define KL_15_BIT  1
#define KL_X_BIT   2
#define KL_50_BIT  3

struct KlemmenState {
  bool kl_s   = false;   // key in barrel / terminal S
  bool kl_15  = false;   // ignition on   / terminal 15
  bool kl_x   = false;   // load shed     / terminal X
  bool kl_50  = false;   // cranking      / terminal 50
};

// AUTOSAR E2E profile-2 style checksum used across MQB messages:
// CRC-8 with polynomial 0x2F, init 0xFF, no reflection, final XOR 0xFF,
// computed over the payload bytes after byte 0, followed by a per-message
// "magic" byte. For 0x3C0 that magic byte is 0xC3.
//
// Derived and cross-checked against the 16 known-good checksums published in
// the Ross-Tech forum thread (all 16 reproduce exactly), which means unlike a
// hardcoded table this also produces valid frames for ignition OFF, for
// terminal X and for terminal 50.
#define MQB_CRC_MAGIC_KLEMMEN_STATUS_01  0xC3

uint8_t mqbChecksum(const uint8_t *data, uint8_t len, uint8_t magic);

// Renders `state` and `counter` into `buf` (must be >= 4 bytes) including a
// valid checksum. `counter` may be any value; only its low nibble is used.
void buildKlemmenStatus01(uint8_t *buf, const KlemmenState &state, uint8_t counter);

// Verifies the checksum implementation against the published reference values.
// Returns true if all 16 counter positions match. Cheap enough to run at boot.
bool mqbChecksumSelfTest();
