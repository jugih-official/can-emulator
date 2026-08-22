#include "mqb_klemmen.h"

uint8_t mqbChecksum(const uint8_t *data, uint8_t len, uint8_t magic) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x2F) : (uint8_t)(crc << 1);
    }
  }
  crc ^= magic;
  for (uint8_t bit = 0; bit < 8; bit++) {
    crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x2F) : (uint8_t)(crc << 1);
  }
  return (uint8_t)(crc ^ 0xFF);
}

void buildKlemmenStatus01(uint8_t *buf, const KlemmenState &state, uint8_t counter) {
  buf[1] = (uint8_t)(counter & 0x0F);
  buf[2] = (uint8_t)((state.kl_s  ? (1 << KL_S_BIT)  : 0) |
                     (state.kl_15 ? (1 << KL_15_BIT) : 0) |
                     (state.kl_x  ? (1 << KL_X_BIT)  : 0) |
                     (state.kl_50 ? (1 << KL_50_BIT) : 0));
  buf[3] = 0x00;
  buf[0] = mqbChecksum(&buf[1], 3, MQB_CRC_MAGIC_KLEMMEN_STATUS_01);
}

// Reference checksums for byte2 == 0x03 (Kl_S + Kl_15 set), counter 0..15,
// as captured from a real MQB gateway.
static const uint8_t kReferenceCrc[16] PROGMEM = {
  0x74, 0xC1, 0x31, 0x84, 0xFE, 0x4B, 0xBB, 0x0E,
  0x4F, 0xFA, 0x0A, 0xBF, 0xC5, 0x70, 0x80, 0x35
};

bool mqbChecksumSelfTest() {
  KlemmenState ignitionOn;
  ignitionOn.kl_s  = true;
  ignitionOn.kl_15 = true;

  uint8_t frame[MQB_DLC_KLEMMEN_STATUS_01];
  for (uint8_t counter = 0; counter < 16; counter++) {
    buildKlemmenStatus01(frame, ignitionOn, counter);
    if (frame[0] != pgm_read_byte(&kReferenceCrc[counter])) return false;
  }
  return true;
}
