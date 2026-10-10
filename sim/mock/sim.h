// What the mocked Arduino, MCP2515, LCD and EEPROM share with the simulator.
#pragma once
#include <stdint.h>
#include <deque>
#include <map>
#include <string>

struct SimFrame {
  uint32_t id;
  bool     ext;
  uint8_t  dlc;
  uint8_t  data[8];
};

struct SimTx {            // what the sketch has put on the wire, per ID
  uint32_t count = 0;
  uint32_t at    = 0;
  uint8_t  dlc   = 0;
  uint8_t  data[8] = {0};
};

struct Sim {
  uint64_t us  = 0;                 // virtual time in microseconds
  uint32_t now = 0;                 // ... and in milliseconds, what millis() returns
  bool     buttonDown = false;

  // MCP2515
  bool     power = true;            // the module has its 5 V
  uint8_t  mode  = 0x80;            // CANSTAT mode bits; 0x80 is configuration
  uint8_t  eflg  = 0;
  bool     ack   = true;            // some other node acknowledges
  std::deque<SimFrame> rx;          // at most 2: RXB0 and RXB1
  std::map<uint32_t, SimTx> tx;

  // LCD
  uint8_t  ddram[2][16];
  uint8_t  cgram[8][8];
  uint8_t  row = 0, col = 0;

  // Serial
  std::string serialOut;
  std::deque<char> serialIn;

  // EEPROM
  uint8_t  eeprom[1024];
  std::string eepromPath;
  void saveEeprom();
};

extern Sim sim;
