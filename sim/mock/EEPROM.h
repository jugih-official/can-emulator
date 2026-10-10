// EEPROM backed by a file, so that a simulated reset finds what was stored.
#pragma once
#include <Arduino.h>

class EEPROMClass {
 public:
  uint8_t read(int addr);
  void update(int addr, uint8_t value);
};
extern EEPROMClass EEPROM;
