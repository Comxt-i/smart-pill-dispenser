#pragma once

#include <Arduino.h>

void nanoControlBegin();
bool dispenseMedicine(uint8_t dispenser, uint8_t amount);
bool stopDispenser();
