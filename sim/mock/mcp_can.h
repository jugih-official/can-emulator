// The part of coryjfowler's mcp_can that the sketch uses: set-up and transmit.
#pragma once
#include <Arduino.h>

#define MCP_ANY 0
#define CAN_500KBPS 12
#define MCP_8MHZ 2
#define MCP_16MHZ 1
#define CAN_OK 0
#define CAN_FAILINIT 1
#define CAN_SENDMSGTIMEOUT 7
#define MCP_NORMAL 0x00
#define MCP_LISTENONLY 0x60

class MCP_CAN {
 public:
  explicit MCP_CAN(uint8_t) {}
  byte begin(byte idmode, byte speed, byte clock);
  byte setMode(byte mode);
  byte sendMsgBuf(unsigned long id, byte ext, byte len, byte *buf);
};
