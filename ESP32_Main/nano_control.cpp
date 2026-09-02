#include "nano_control.h"
#include "config.h"

#include <Wire.h>

namespace {
uint8_t addressForLane(uint8_t lane)
{
  switch (lane)
  {
    case 1: return NANO1_ADDRESS;
    case 2: return NANO2_ADDRESS;
    case 3: return NANO3_ADDRESS;
    default: return 0;
  }
}

bool sendCommand(uint8_t lane, uint8_t command, uint8_t value)
{
  uint8_t address = addressForLane(lane);
  if (address == 0)
    return false;

  Wire.beginTransmission(address);
  Wire.write(command);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}
}

void nanoControlBegin()
{
  // Wire.begin() is called once in ESP32_Main.ino.
}

bool dispenseMedicine(uint8_t lane, uint8_t amount)
{
  return sendCommand(lane, CMD_DISPENSE, amount);
}

bool stopLane(uint8_t lane)
{
  return sendCommand(lane, CMD_STOP, 0);
}

