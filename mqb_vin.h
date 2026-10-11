#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// VIN_01: the vehicle identification number as it goes round an MQB car.
//
// On a car the gateway is the sender. It holds the VIN and repeats it for the
// other control units, together with four bytes of the component-protection
// secret. Nothing tells a gateway its VIN this way: that is a tester's job.
// Sending it from here stands in for a gateway towards the units on the same
// bus, no more.
//
// Layout from vw_mqb.dbc (commaai/opendbc). Three frames, told apart by the
// multiplexer in the low two bits of byte 0:
//   0:  KS_Geheimnis_1..4 in bytes 1..4, VIN characters 1..3 in bytes 5..7
//   1:  VIN characters  4..10 in bytes 1..7
//   2:  VIN characters 11..17 in bytes 1..7
// No checksum and no counter.
// ---------------------------------------------------------------------------
#define MQB_ID_VIN_01     0x6B4
#define MQB_DLC_VIN_01    8
#define MQB_VIN_LENGTH    17
#define MQB_VIN_FRAMES    3

// One of the three frames. `vin` is MQB_VIN_LENGTH characters and needs no
// terminator. The component-protection bytes go out as zero: they are a secret
// between a real gateway and its car, and are not known here.
void buildVin01(uint8_t *buf, uint8_t mux, const char *vin);

// The character as a VIN may hold it, or 0 for one it may not: digits and
// capital letters without I, O and Q. Lower case is taken as upper case.
char vinChar(char c);
