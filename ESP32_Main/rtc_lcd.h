#pragma once

#include <Arduino.h>

void rtcLcdBegin();
void rtcLcdUpdate();
void showNextMedicine(const char *medicineName, uint8_t hour, uint8_t minute);

