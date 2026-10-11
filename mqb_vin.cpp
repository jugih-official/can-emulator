#include "mqb_vin.h"

void buildVin01(uint8_t *buf, uint8_t mux, const char *vin) {
  for (uint8_t i = 0; i < MQB_DLC_VIN_01; i++) buf[i] = 0;
  buf[0] = mux & 0x03;
  if (mux == 0) {
    for (uint8_t i = 0; i < 3; i++) buf[5 + i] = (uint8_t)vin[i];
  } else if (mux < MQB_VIN_FRAMES) {
    const char *part = vin + 3 + (mux - 1) * 7;
    for (uint8_t i = 0; i < 7; i++) buf[1 + i] = (uint8_t)part[i];
  }
}

char vinChar(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  if (c >= '0' && c <= '9') return c;
  if (c < 'A' || c > 'Z' || c == 'I' || c == 'O' || c == 'Q') return 0;
  return c;
}
