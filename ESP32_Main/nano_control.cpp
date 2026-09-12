#include "nano_control.h"
#include "config.h"

#include <Wire.h>

namespace {
bool sendCommand(uint8_t command, uint8_t dispenser, uint8_t amount)
{
  Wire.beginTransmission(NANO_DISPENSER_ADDRESS);
  Wire.write(command);
  Wire.write(dispenser);
  Wire.write(amount);
  return Wire.endTransmission() == 0;
}
}

void nanoControlBegin()
{
  // Wire.begin() is called once in ESP32_Main.ino.
}

bool dispenseMedicine(uint8_t dispenser, uint8_t amount)
{
  if (dispenser < 1 || dispenser > 3 || amount < 1 || amount > 9)
    return false;

  return sendCommand(CMD_DISPENSE, dispenser, amount);
}

bool stopDispenser()
{
  return sendCommand(CMD_STOP, 0, 0);
}
