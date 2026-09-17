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
bool clockValid = false;
unsigned long lastRefreshMs = 0;

// จำข้อความล่าสุดไว้ เพื่อไม่ต้องเขียนจอซ้ำทุกวินาที (การ clear() ทำให้จอกะพริบ)
char shownLine1[17] = "";
char shownLine2[17] = "";

/** เติมช่องว่างให้เต็ม 16 ตัวอักษร เพื่อลบข้อความเดิมที่ยาวกว่าโดยไม่ต้อง clear ทั้งจอ */
void printPadded(LiquidCrystal_I2C &lcd, uint8_t row, const char *text)
{
  char padded[17];
  snprintf(padded, sizeof(padded), "%-16.16s", text ? text : "");
  lcd.setCursor(0, row);
  lcd.print(padded);
}

void writeMedicineLcd(const char *line1, const char *line2)
{
  if (strncmp(shownLine1, line1, sizeof(shownLine1)) == 0 &&
      strncmp(shownLine2, line2, sizeof(shownLine2)) == 0)
    return;

  printPadded(lcdMedicine, 0, line1);
  printPadded(lcdMedicine, 1, line2);

  strncpy(shownLine1, line1, sizeof(shownLine1) - 1);
  shownLine1[sizeof(shownLine1) - 1] = '\0';
  strncpy(shownLine2, line2, sizeof(shownLine2) - 1);
  shownLine2[sizeof(shownLine2) - 1] = '\0';
}
}

void rtcLcdBegin()
{
  lcdTime.init();
  lcdTime.backlight();
  lcdTime.clear();
  printPadded(lcdTime, 0, "Smart Pill Box");
  printPadded(lcdTime, 1, "Starting...");

  lcdMedicine.init();
  lcdMedicine.backlight();
  lcdMedicine.clear();
  writeMedicineLcd("Connecting", "Please wait...");
}

void rtcLcdUpdate()
{
  if (millis() - lastRefreshMs < 1000)
    return;

  lastRefreshMs = millis();

  if (!RTC.read(currentTime))
  {
    clockValid = false;
    printPadded(lcdTime, 0, "RTC ERROR");
    printPadded(lcdTime, 1, "Check DS1307");
    return;
  }

  // DS1307 ที่ยังไม่เคยตั้งเวลาจะรายงานปี 1970 ซึ่งใช้ตัดสินมื้อยาไม่ได้
  clockValid = tmYearToCalendar(currentTime.Year) >= 2024;

  char line1[17];
  char line2[17];
  snprintf(line1,
           sizeof(line1),
           "%02d/%02d/%04d",
           currentTime.Day,
           currentTime.Month,
           tmYearToCalendar(currentTime.Year));
  snprintf(line2,
           sizeof(line2),
           "%02d:%02d:%02d%s",
           currentTime.Hour,
           currentTime.Minute,
           currentTime.Second,
           clockValid ? "" : "  NOT SET");

  printPadded(lcdTime, 0, clockValid ? line1 : "SET CLOCK FIRST");
  printPadded(lcdTime, 1, line2);
}

void rtcSyncFromEpoch(uint32_t localEpoch)
{
  if (localEpoch == 0)
    return;

  // เขียน RTC เฉพาะตอนที่เพี้ยนจริง การเขียนทุกนาทีทำให้ EEPROM/บัสทำงานโดยไม่จำเป็น
  const uint32_t current = rtcLocalEpoch();
  const uint32_t drift = current > localEpoch ? current - localEpoch : localEpoch - current;
  if (clockValid && drift <= 2)
    return;

  tmElements_t parts;
  breakTime(static_cast<time_t>(localEpoch), parts);

  if (RTC.write(parts))
  {
    currentTime = parts;
    clockValid = true;
    Serial.printf("[RTC] ตั้งเวลาตาม server: %02d:%02d:%02d (คลาดเคลื่อนเดิม %lu วินาที)\n",
                  parts.Hour,
                  parts.Minute,
                  parts.Second,
                  static_cast<unsigned long>(drift));
  }
  else
  {
    Serial.println("[RTC] เขียนเวลาลง DS1307 ไม่สำเร็จ");
  }
}

bool rtcIsValid()
{
  return clockValid;
}

int rtcMinutesOfDay()
{
  if (!clockValid)
    return -1;
  return currentTime.Hour * 60 + currentTime.Minute;
}

uint32_t rtcDayKey()
{
  if (!clockValid)
    return 0;
  return static_cast<uint32_t>(tmYearToCalendar(currentTime.Year)) * 10000UL +
         static_cast<uint32_t>(currentTime.Month) * 100UL + currentTime.Day;
}

uint32_t rtcLocalEpoch()
{
  if (!clockValid)
    return 0;
  return static_cast<uint32_t>(makeTime(currentTime));
}

void lcdShowMessage(const char *line1, const char *line2)
{
  writeMedicineLcd(line1, line2);
}

void lcdShowNextDose(const char *medicineName, int minutes)
{
  char line2[17];
  snprintf(line2, sizeof(line2), "Next %02d:%02d", minutes / 60, minutes % 60);
  writeMedicineLcd(medicineName, line2);
}

void lcdShowAlert(const char *medicineName, float amount, int minutes)
{
  char line2[17];
  // แสดงจำนวนเม็ดแบบไม่มีทศนิยมเมื่อเป็นจำนวนเต็ม
  if (amount == static_cast<int>(amount))
    snprintf(line2, sizeof(line2), "%02d:%02d x%d PRESS", minutes / 60, minutes % 60, static_cast<int>(amount));
  else
    snprintf(line2, sizeof(line2), "%02d:%02d x%.1f PRESS", minutes / 60, minutes % 60, amount);

  writeMedicineLcd(medicineName, line2);
}

void lcdShowIdle(bool online)
{
  writeMedicineLcd("No dose left", online ? "Online" : "Offline");
}
