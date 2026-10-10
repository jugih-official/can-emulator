// SPI, with a small MCP2515 on the other end: read status, read register and
// read RX buffer, which is all the sketch does over SPI itself.
#pragma once
#include <Arduino.h>

#define MSBFIRST 1
#define SPI_MODE0 0

struct SPISettings {
  SPISettings(uint32_t, int, int) {}
};

class SPIClass {
 public:
  void begin() {}
  void beginTransaction(const SPISettings &) {}
  void endTransaction() {}
  uint8_t transfer(uint8_t out);
};
extern SPIClass SPI;
