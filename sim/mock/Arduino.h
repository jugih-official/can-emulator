// Host stand-in for the Arduino core: just enough for can-emulator.ino.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

typedef uint8_t byte;
typedef bool boolean;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define A0 14
#define A1 15
#define A2 16
#define A3 17
#define A4 18
#define A5 19
#define DEC 10
#define HEX 16

#define PROGMEM
#define PSTR(s) (s)
#define F(s) (s)
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define memcpy_P memcpy

uint32_t millis();
void pinMode(uint8_t pin, uint8_t mode);
int  digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t value);

// The overloads of Arduino's Print: a char prints as a character, every other
// integer type as a number.
class SerialMock {
 public:
  void begin(long) {}
  explicit operator bool() const { return true; }
  int available();
  int read();
  size_t write(uint8_t c);
  size_t write(const uint8_t *buf, size_t n);
  size_t print(const char *s);
  size_t print(char c);
  size_t print(unsigned char v, int base = DEC) { return number(v, base); }
  size_t print(int v, int base = DEC);
  size_t print(unsigned int v, int base = DEC) { return number(v, base); }
  size_t print(long v, int base = DEC);
  size_t print(unsigned long v, int base = DEC) { return number(v, base); }
  size_t println() { return print("\r\n"); }
  template <typename T> size_t println(T v) { size_t n = print(v); return n + println(); }
  template <typename T> size_t println(T v, int base) { size_t n = print(v, base); return n + println(); }
 private:
  size_t number(unsigned long v, int base);
};
extern SerialMock Serial;
