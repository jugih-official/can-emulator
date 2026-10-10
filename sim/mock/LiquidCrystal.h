// An HD44780 16x2 reduced to its memory: what is in each cell, and the eight
// user-defined characters.
#pragma once
#include <Arduino.h>

class LiquidCrystal {
 public:
  LiquidCrystal(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) {}
  void begin(uint8_t cols, uint8_t rows);
  void clear();
  void setCursor(uint8_t col, uint8_t row);
  size_t write(uint8_t c);
  void createChar(uint8_t n, uint8_t rows[]);
};
