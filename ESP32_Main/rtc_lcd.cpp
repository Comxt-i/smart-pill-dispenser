#include "rtc_lcd.h"
#include "config.h"

#include <DS1307RTC.h>
#include <LiquidCrystal_I2C.h>
#include <TimeLib.h>
#include <Wire.h>

namespace {
LiquidCrystal_I2C lcdTime(LCD_TIME_ADDRESS, 16, 2);
LiquidCrystal_I2C lcdMedicine(LCD_MEDICINE_ADDRESS, 16, 2);
tmElements_t currentTime;
unsigned long lastRefreshMs = 0;
}

void rtcLcdBegin()
{
  lcdTime.init();
  lcdTime.backlight();
  lcdTime.clear();

  lcdMedicine.init();
  lcdMedicine.backlight();
  lcdMedicine.clear();
  lcdMedicine.print("No schedule");
}

void rtcLcdUpdate()
{
  if (millis() - lastRefreshMs < 1000)
    return;

  lastRefreshMs = millis();

  if (!RTC.read(currentTime))
  {
    lcdTime.setCursor(0, 0);
    lcdTime.print("RTC ERROR       ");
    return;
  }

  char timeText[17];
  snprintf(timeText,
           sizeof(timeText),
           "%02d:%02d:%02d",
           currentTime.Hour,
           currentTime.Minute,
           currentTime.Second);

  lcdTime.setCursor(0, 0);
  lcdTime.print("Current Time    ");
  lcdTime.setCursor(0, 1);
  lcdTime.print(timeText);
  lcdTime.print("        ");
}

void showNextMedicine(const char *medicineName, uint8_t hour, uint8_t minute)
{
  char nextTime[17];
  snprintf(nextTime, sizeof(nextTime), "Next %02u:%02u", hour, minute);

  lcdMedicine.clear();
  lcdMedicine.setCursor(0, 0);
  lcdMedicine.print(medicineName);
  lcdMedicine.setCursor(0, 1);
  lcdMedicine.print(nextTime);
}
