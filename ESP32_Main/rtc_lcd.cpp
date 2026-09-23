#include "rtc_lcd.h"
#include "config.h"
#include "marquee.h"

#include <DS1307RTC.h>
#include <LiquidCrystal_I2C.h>
#include <TimeLib.h>
#include <Wire.h>

namespace {
LiquidCrystal_I2C lcdTime(LCD_TIME_ADDRESS, LCD_TIME_COLS, LCD_TIME_ROWS);
LiquidCrystal_I2C lcdMedicine(LCD_MEDICINE_ADDRESS, LCD_MEDICINE_COLS, LCD_MEDICINE_ROWS);

tmElements_t currentTime;
bool clockValid = false;
bool timeLcdPresent = false, medicineLcdPresent = false, rtcPresent = false;
unsigned long lastRefreshMs = 0;

// จำข้อความที่แสดงอยู่จริงของแต่ละบรรทัด เพื่อเขียนเฉพาะบรรทัดที่เปลี่ยน
// การเขียนทับทุกบรรทัดทุกรอบทำให้จอกะพริบและกินเวลาบัส I2C โดยเปล่าประโยชน์
char shownMedicine[LCD_MEDICINE_ROWS][LCD_MAX_COLS + 1] = {};
char shownTime[LCD_TIME_ROWS][LCD_MAX_COLS + 1] = {};

// แต่ละบรรทัดของจอยามีตัวเลื่อนข้อความเป็นของตัวเอง เลื่อนอิสระจากกัน
Marquee lineMarquee[LCD_MEDICINE_ROWS] = {};
uint8_t lineCount = 0;

char hintLines[LCD_MAX_HINTS][LCD_MAX_COLS + 1] = {};
uint8_t hintCount = 0;
uint8_t hintIndex = 0;
unsigned long lastHintSwapMs = 0;
bool hintTimerStarted = false;

// เก็บผลสแกน I2C ไว้ให้หน้าเว็บในเครื่องอ่าน จะได้ไล่ปัญหาโดยไม่ต้องเสียบ USB
constexpr uint8_t I2C_MAX_FOUND = 8;
uint8_t foundAddresses[I2C_MAX_FOUND] = {};
uint8_t foundCount = 0;

/** เติมช่องว่างให้เต็มความกว้างจอ เพื่อลบข้อความเดิมที่ยาวกว่าโดยไม่ต้อง clear ทั้งจอ */
void printPadded(LiquidCrystal_I2C &lcd, uint8_t row, uint8_t cols, const char *text)
{
  if ((&lcd == &lcdTime && !timeLcdPresent) ||
      (&lcd == &lcdMedicine && !medicineLcdPresent)) return;
  char padded[LCD_MAX_COLS + 1];
  snprintf(padded, sizeof(padded), "%-*.*s", cols, cols, text ? text : "");
  lcd.setCursor(0, row);
  lcd.print(padded);
}

/** เขียนบรรทัดเดียวเฉพาะเมื่อข้อความเปลี่ยนจริง */
void writeLineIfChanged(LiquidCrystal_I2C &lcd,
                        uint8_t row,
                        uint8_t cols,
                        char *shown,
                        const char *text)
{
  if (strncmp(shown, text, LCD_MAX_COLS + 1) == 0)
    return;

  printPadded(lcd, row, cols, text);
  strncpy(shown, text, LCD_MAX_COLS);
  shown[LCD_MAX_COLS] = '\0';
}

}

bool i2cScanAndReport()
{
  Serial.println("[I2C] กำลังสแกนบัส...");

  bool foundTimeLcd = false;
  bool foundMedicineLcd = false;
  bool foundRtc = false;
  uint8_t total = 0;
  foundCount = 0;

  for (uint8_t address = 1; address < 127; ++address)
  {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0)
      continue;

    ++total;
    if (foundCount < I2C_MAX_FOUND)
      foundAddresses[foundCount++] = address;
    Serial.printf("[I2C]   พบอุปกรณ์ที่ 0x%02X", address);

    if (address == LCD_TIME_ADDRESS)
    {
      foundTimeLcd = true;
      Serial.print("  <- จอเวลา");
    }
    else if (address == LCD_MEDICINE_ADDRESS)
    {
      foundMedicineLcd = true;
      Serial.print("  <- จอยา");
    }
    else if (address == 0x68)
    {
      foundRtc = true;
      Serial.print("  <- DS1307");
    }
    Serial.println();
  }

  if (total == 0)
    Serial.println("[I2C] ไม่พบอุปกรณ์เลย ตรวจสาย SDA/SCL, ไฟเลี้ยง และ Level Shifter");

  if (!foundTimeLcd)
    Serial.printf("[I2C] เตือน: ไม่พบจอเวลาที่ 0x%02X\n", LCD_TIME_ADDRESS);
  if (!foundMedicineLcd)
    Serial.printf("[I2C] เตือน: ไม่พบจอยาที่ 0x%02X\n", LCD_MEDICINE_ADDRESS);
  if (!foundRtc)
    Serial.println("[I2C] เตือน: ไม่พบ DS1307 ที่ 0x68 นาฬิกาจะไม่เดินต่อเมื่อไฟดับ");

  timeLcdPresent = foundTimeLcd;
  medicineLcdPresent = foundMedicineLcd;
  rtcPresent = foundRtc;
  const bool complete = foundTimeLcd && foundMedicineLcd && foundRtc;
  if (complete)
    Serial.println("[I2C] พบอุปกรณ์ครบตามที่ตั้งค่าไว้");
  else
    Serial.println("[I2C] อุปกรณ์ไม่ครบ ระบบยังทำงานต่อได้แต่บางส่วนจะไม่แสดงผล");

  return complete;
}

I2cLineState lastLineState = {};

namespace {
/**
 * ทดสอบว่าขานี้มี pull-up ภายนอกจริงไหม
 *
 * บังคับให้เป็น LOW เพื่อไล่ประจุตกค้างออกก่อน แล้วปล่อยเป็น input
 * มี pull-up จริง -> เด้งกลับเป็น HIGH ภายในไม่กี่ไมโครวินาที
 * ขาลอย -> ค้างอยู่ LOW เพราะไม่มีอะไรดึงขึ้น
 */
bool hasExternalPullup(uint8_t pin)
{
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delayMicroseconds(200);

  pinMode(pin, INPUT);  // ปล่อยลอย ไม่เปิด pull-up ภายใน
  delayMicroseconds(50);

  return digitalRead(pin) == HIGH;
}

/** ขาใช้งานได้ไหม: เปิด pull-up ภายในแล้วต้องเป็น HIGH ถ้ายัง LOW แปลว่าลัดลง GND */
bool pinCanGoHigh(uint8_t pin)
{
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delayMicroseconds(200);

  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(200);

  return digitalRead(pin) == HIGH;
}
}

I2cLineState i2cCheckLines()
{
  I2cLineState state;

  state.sdaHighWithoutPullup = hasExternalPullup(I2C_SDA_PIN);
  state.sclHighWithoutPullup = hasExternalPullup(I2C_SCL_PIN);
  state.sdaHighWithPullup = pinCanGoHigh(I2C_SDA_PIN);
  state.sclHighWithPullup = pinCanGoHigh(I2C_SCL_PIN);

  lastLineState = state;

  Serial.println("[I2C] ตรวจสภาพเส้นสัญญาณ (ดึงลง GND แล้วดูว่าเด้งกลับไหม)");
  Serial.printf("[I2C]   SDA (GPIO%u): pull-up ภายนอก = %s, ขาใช้งานได้ = %s\n",
                I2C_SDA_PIN,
                state.sdaHighWithoutPullup ? "มี" : "ไม่มี",
                state.sdaHighWithPullup ? "ใช่" : "ไม่ (ลัดลง GND)");
  Serial.printf("[I2C]   SCL (GPIO%u): pull-up ภายนอก = %s, ขาใช้งานได้ = %s\n",
                I2C_SCL_PIN,
                state.sclHighWithoutPullup ? "มี" : "ไม่มี",
                state.sclHighWithPullup ? "ใช่" : "ไม่ (ลัดลง GND)");

  if (!state.sdaHighWithPullup || !state.sclHighWithPullup)
    Serial.println("[I2C]   -> สายลัดลง GND หรือต่อผิดขา บัสใช้งานไม่ได้แน่นอน");
  else if (!state.sdaHighWithoutPullup || !state.sclHighWithoutPullup)
    Serial.println("[I2C]   -> ขาลอย ไม่มี pull-up ภายนอก (สายไม่ถึงโมดูล หรือ shifter ไม่ได้รับไฟ)");
  else
    Serial.println("[I2C]   -> มี pull-up ภายนอกจริง เส้นสัญญาณถึงโมดูลแล้ว");

  return state;
}

I2cLineState i2cLastLineState()
{
  return lastLineState;
}

uint8_t i2cFoundCount()
{
  return foundCount;
}

uint8_t i2cFoundAddress(uint8_t index)
{
  return index < foundCount ? foundAddresses[index] : 0;
}

void rtcLcdBegin()
{
  if (timeLcdPresent) {
    lcdTime.init();
    lcdTime.backlight();
    lcdTime.clear();
  }
  writeLineIfChanged(lcdTime, 0, LCD_TIME_COLS, shownTime[0], "Smart Pill Box");
  writeLineIfChanged(lcdTime, 1, LCD_TIME_COLS, shownTime[1], "Starting...");

  if (medicineLcdPresent) {
    lcdMedicine.init();
    lcdMedicine.backlight();
    lcdMedicine.clear();
  }
  lcdShowMessage("Connecting", "Please wait...");
}

void rtcLcdUpdate()
{
  if (millis() - lastRefreshMs < 1000)
    return;

  lastRefreshMs = millis();

  if (!rtcPresent || !RTC.read(currentTime))
  {
    clockValid = false;
    writeLineIfChanged(lcdTime, 0, LCD_TIME_COLS, shownTime[0], "RTC ERROR");
    writeLineIfChanged(lcdTime, 1, LCD_TIME_COLS, shownTime[1], "Check DS1307");
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

  writeLineIfChanged(lcdTime, 0, LCD_TIME_COLS, shownTime[0], clockValid ? line1 : "SET CLOCK FIRST");
  writeLineIfChanged(lcdTime, 1, LCD_TIME_COLS, shownTime[1], line2);
}

void rtcSyncFromEpoch(uint32_t localEpoch)
{
  if (!rtcPresent || localEpoch == 0)
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

void lcdSetMedicineScreen(const char *const *lines,
                          uint8_t count,
                          const char *const *hints,
                          uint8_t newHintCount)
{
  if (count > LCD_MEDICINE_ROWS)
    count = LCD_MEDICINE_ROWS;
  if (newHintCount > LCD_MAX_HINTS)
    newHintCount = LCD_MAX_HINTS;

  lineCount = count;
  for (uint8_t i = 0; i < LCD_MEDICINE_ROWS; ++i)
    marqueeSet(lineMarquee[i], i < count ? lines[i] : "", LCD_MEDICINE_COLS);

  // ตรวจว่าชุดคำแนะนำเปลี่ยนจริงไหม ถ้าเหมือนเดิมต้องไม่รีเซ็ตจังหวะการสลับ
  bool changed = newHintCount != hintCount;
  for (uint8_t i = 0; i < newHintCount && !changed; ++i)
    changed = strncmp(hintLines[i], hints[i] ? hints[i] : "", LCD_MAX_COLS + 1) != 0;

  if (!changed)
    return;

  hintCount = newHintCount;
  for (uint8_t i = 0; i < newHintCount; ++i)
  {
    strncpy(hintLines[i], hints[i] ? hints[i] : "", LCD_MAX_COLS);
    hintLines[i][LCD_MAX_COLS] = '\0';
  }

  hintIndex = 0;
  hintTimerStarted = false;
}

void lcdShowMessage(const char *line1, const char *line2)
{
  const char *lines[1] = {line1};
  const char *hints[1] = {line2};
  lcdSetMedicineScreen(lines, 1, hints, 1);
}

void lcdMedicineTick()
{
  if (!medicineLcdPresent) return;
  const unsigned long now = millis();

  if (!hintTimerStarted)
  {
    hintTimerStarted = true;
    lastHintSwapMs = now;
  }

  if (hintCount > 1 && now - lastHintSwapMs >= LCD_HINT_INTERVAL_MS)
  {
    lastHintSwapMs = now;
    hintIndex = static_cast<uint8_t>((hintIndex + 1) % hintCount);
  }

  constexpr uint8_t LAST_ROW = LCD_MEDICINE_ROWS - 1;

  for (uint8_t row = 0; row < LCD_MEDICINE_ROWS; ++row)
  {
    char text[LCD_MAX_COLS + 1];

    // บรรทัดสุดท้ายถูกจองไว้ให้คำแนะนำปุ่มสลับไปมา ถ้ามีคำแนะนำอยู่
    if (row == LAST_ROW && hintCount > 0)
      snprintf(text,
               sizeof(text),
               "%-*.*s",
               LCD_MEDICINE_COLS,
               LCD_MEDICINE_COLS,
               hintLines[hintIndex]);
    else
      marqueeRender(lineMarquee[row], now, text);

    writeLineIfChanged(lcdMedicine, row, LCD_MEDICINE_COLS, shownMedicine[row], text);
  }
}

