#include "rtc_lcd.h"
#include "config.h"
#include "marquee.h"
#include "software_clock.h"

#include <DS1307RTC.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>
#include <TimeLib.h>
#include <Wire.h>

namespace {
LiquidCrystal_I2C lcdTime(LCD_TIME_ADDRESS, LCD_TIME_COLS, LCD_TIME_ROWS);
LiquidCrystal_I2C lcdMedicine(LCD_MEDICINE_ADDRESS, LCD_MEDICINE_COLS, LCD_MEDICINE_ROWS);

tmElements_t currentTime;
bool clockValid = false;
SoftwareClock softwareClock;

// เวลาล่าสุดที่รู้แน่ว่าถูก (จาก server หรือ RTC ที่ผ่านการตรวจแล้ว) เก็บข้ามการรีบูต
// ใช้จับ RTC ที่หยุดเดินตอนไม่มีไฟ: เปิดเครื่องมาแล้วบอกเวลาก่อนค่านี้ = ผิดแน่นอน
Preferences clockStore;
bool clockStoreOpened = false;
bool clockStoreReady = false;
uint32_t trustedFloor = 0;
uint32_t floorSaved = 0;
bool rtcLostTime = false;  // เจอ RTC ย้อนหลังในรอบบูตนี้
// อ่าน RTC ครั้งล่าสุดได้เวลาที่ใช้ได้ไหม (ชิปอยู่และเดิน แต่เวลาเป็นค่าขยะ/ก่อนปี 2024 = false)
bool rtcHadTime = false;
// Wi-Fi ต่ออยู่ไหม (pill_app บอกมา) ใช้เลือกคำแนะนำตอนยังไม่มีเวลา
bool networkOnline = false;
// server ยืนยันเวลาแล้วในรอบบูตนี้หรือยัง
// DS1307 ที่หยุดเดินตอนไม่มีไฟจะค้างที่เวลาตอนถอดปลั๊ก เครื่องตรวจเองไม่ได้ (ไม่มีอะไรนับเวลาต่อ
// ตอนไม่มีไฟ และ DS1307 ไม่มีธงบอกว่าออสซิลเลเตอร์เคยหยุด) จึงต้องบอกผู้ใช้ว่ายังไม่ได้ยืนยัน
bool timeVerified = false;
// ตอนเปิดเครื่องครั้งล่าสุด RTC คลาดจาก server เกินหนึ่งนาทีไหม (เก็บข้ามรีบูต)
// ดูจากครั้งล่าสุดเท่านั้น เปลี่ยนถ่านแล้วเปิดเครื่องที่เวลาถูก ข้อความเตือนหายเอง
uint32_t rtcWasOff = 0;

void ensureClockStore()
{
  if (clockStoreOpened)
    return;
  clockStoreOpened = true;
  clockStoreReady = clockStore.begin("pillclock", false);
  if (clockStoreReady)
  {
    trustedFloor = floorSaved = clockStore.getULong("floor", 0);
    rtcWasOff = clockStore.getULong("rtcoff", 0);
  }
}

/**
 * จดว่าเวลานี้ถูกแน่นอน
 * authoritative (มาจาก server) = เชื่อแม้ถอยหลัง เช่นเปลี่ยน timezone บนเว็บ หรือ RTC เคยเดินเร็วไป
 */
void noteTrustedTime(uint32_t epoch, bool authoritative)
{
  if (!SoftwareClock::validEpoch(epoch))
    return;
  if (!authoritative && epoch <= trustedFloor)
    return;
  const bool movedDown = epoch < trustedFloor;
  trustedFloor = epoch;
  const uint32_t sinceSave = epoch > floorSaved ? epoch - floorSaved : floorSaved - epoch;
  if (clockStoreReady && (movedDown || sinceSave >= CLOCK_FLOOR_SAVE_INTERVAL_S))
  {
    clockStore.putULong("floor", epoch);
    floorSaved = epoch;
  }
}
const char *clockSource = "WAITING";
bool timeLcdPresent = false, medicineLcdPresent = false, rtcPresent = false;
unsigned long lastRefreshMs = 0;
// ครั้งแรกต้องอ่านเสมอ: หน้ารายงานตอนบูตเรียกก่อน millis() ถึง 1000 ถ้าโดนกันถี่ไว้
// จะไม่ได้อ่าน RTC เลย แล้วรายงาน "RTC RUNNING" ทั้งที่ RTC ไม่มีเวลาให้
bool refreshedOnce = false;

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
    else if (address == DS1307_ADDRESS)
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
  if (refreshedOnce && millis() - lastRefreshMs < 1000)
    return;

  refreshedOnce = true;
  lastRefreshMs = millis();
  ensureClockStore();

  const uint32_t fallback = softwareClock.localEpoch(millis());
  tmElements_t rtcTime = {};
  const bool rtcRead = rtcPresent && RTC.read(rtcTime);
  const bool rtcFieldsValid = rtcRead && rtcTime.Year >= 54 && rtcTime.Year < 130 &&
      rtcTime.Month >= 1 && rtcTime.Month <= 12 && rtcTime.Day >= 1 && rtcTime.Day <= 31 &&
      rtcTime.Hour < 24 && rtcTime.Minute < 60 && rtcTime.Second < 60;
  const uint32_t rtcEpoch = rtcFieldsValid ? static_cast<uint32_t>(makeTime(rtcTime)) : 0;
  const uint32_t drift = rtcEpoch > fallback ? rtcEpoch - fallback : fallback - rtcEpoch;
  // เปิดเครื่องมาแล้ว RTC บอกเวลาย้อนหลัง = นาฬิกาหยุดเดินตอนไม่มีไฟ ห้ามใช้
  // ถ้าใช้ไป เครื่องจะจ่ายยาตามเวลาที่ผิด และมื้อที่ถูกตัดสินผิดว่า "พลาด" จะไม่เตือนอีกทั้งวัน
  const bool rtcEpochValid = rtcRead && SoftwareClock::validEpoch(rtcEpoch);
  rtcHadTime = rtcEpochValid;
  const bool rtcPlausible =
      rtcEpochValid && rtcTimeIsPlausible(rtcEpoch, trustedFloor, RTC_BACKWARD_TOLERANCE_S);
  if (rtcEpochValid && !rtcPlausible && !rtcLostTime)
  {
    rtcLostTime = true;
    Serial.printf("[Clock] RTC บอกเวลาย้อนหลังไป %lu วินาที: นาฬิกาหยุดเดินตอนไม่มีไฟ "
                  "(ถ่านสำรองหมด?) ไม่ใช้เวลานี้ รอเวลาจาก server\n",
                  static_cast<unsigned long>(trustedFloor - rtcEpoch));
  }

  // A stale RTC that failed to accept the server time must not override it.
  if (rtcPlausible && (!fallback || drift <= 5)) {
    currentTime = rtcTime;
    softwareClock.sync(rtcEpoch, millis());
    clockValid = true;
    clockSource = "RTC";
    noteTrustedTime(rtcEpoch, false);
  } else if (SoftwareClock::validEpoch(fallback)) {
    breakTime(fallback, currentTime);
    clockValid = true;
    clockSource = "SERVER";
  } else {
    clockValid = false;
    clockSource = "WAITING";
    writeLineIfChanged(lcdTime, 0, LCD_TIME_COLS, shownTime[0], rtcLostTime ? "RTC LOST TIME" : "WAIT FOR TIME");
    // ต่อ Wi-Fi แล้วอย่าบอกให้ต่อ Wi-Fi อีก ผู้ใช้จะเข้าใจผิดว่าเน็ตมีปัญหา
    writeLineIfChanged(lcdTime, 1, LCD_TIME_COLS, shownTime[1], networkOnline ? "GETTING TIME" : "CONNECT WI-FI");
    return;
  }

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
           strcmp(clockSource, "RTC") != 0 ? "  NO BAT"
           : timeVerified                  ? ""
                                           : " UNSYNC");  // เวลาจาก RTC ที่ server ยังไม่ยืนยัน

  writeLineIfChanged(lcdTime, 0, LCD_TIME_COLS, shownTime[0], clockValid ? line1 : "SET CLOCK FIRST");
  writeLineIfChanged(lcdTime, 1, LCD_TIME_COLS, shownTime[1], line2);
}

void rtcSyncFromEpoch(uint32_t localEpoch)
{
  if (!SoftwareClock::validEpoch(localEpoch)) return;
  const uint32_t previous = rtcLocalEpoch();
  const uint32_t drift = previous > localEpoch ? previous - localEpoch : localEpoch - previous;
  const bool wasUsingRtc = strcmp(clockSource, "RTC") == 0;
  ensureClockStore();
  noteTrustedTime(localEpoch, true);
  // ครั้งแรกที่ server ยืนยันเวลาในรอบบูตนี้: RTC ที่ใช้มาตั้งแต่เปิดเครื่องถูกไหม
  if (!timeVerified && wasUsingRtc)
  {
    const uint32_t wasOff = drift > 60 ? 1 : 0;
    if (wasOff)
      Serial.printf("[Clock] เปิดเครื่องมาเวลาใน RTC คลาดจาก server %lu วินาที แก้ให้แล้ว "
                    "นาฬิกาน่าจะหยุดเดินตอนไม่มีไฟ ตรวจถ่านสำรอง\n",
                    static_cast<unsigned long>(drift));
    if (wasOff != rtcWasOff && clockStoreReady)
      clockStore.putULong("rtcoff", wasOff);  // เขียนเฉพาะตอนผลเปลี่ยน
    rtcWasOff = wasOff;
  }
  else if (wasUsingRtc && drift > 60)
  {
    Serial.printf("[Clock] เวลาใน RTC คลาดจาก server %lu วินาที แก้ให้แล้ว\n",
                  static_cast<unsigned long>(drift));
  }
  timeVerified = true;
  softwareClock.sync(localEpoch, millis());
  breakTime(localEpoch, currentTime);
  clockValid = true;
  clockSource = "SERVER";
  if (rtcPresent) {
    if ((wasUsingRtc && drift <= 2) || RTC.write(currentTime)) {
      clockSource = "RTC";
    } else {
      Serial.println("[Clock] RTC write failed; continuing with server time");
    }
  }
  if (!previous)
    Serial.printf("[Clock] Time ready via %s\n", clockSource);
}

bool rtcIsPresent() { return rtcPresent; }

bool rtcLostTimeDetected() { return rtcLostTime; }

bool rtcTimeVerified() { return timeVerified; }

bool rtcHasTime() { return rtcHadTime; }

void rtcSetNetworkOnline(bool online) { networkOnline = online; }

bool rtcWasOffAtLastBoot()
{
  ensureClockStore();
  return rtcWasOff != 0;
}

bool rtcOscillatorHalted()
{
  // บิต 7 ของรีจิสเตอร์ 0x00 คือ CH (Clock Halt)
  // ตั้งอยู่ = ออสซิลเลเตอร์ไม่เดิน ซึ่งเป็นสถานะตั้งต้นเมื่อไฟหมดโดยไม่มีถ่านสำรอง
  //
  // DS1307RTC.read() คืน false เมื่อบิตนี้ตั้งอยู่ ทำให้แยกไม่ออกระหว่าง
  // "ชิปหาย" กับ "ชิปอยู่แต่นาฬิกาหยุด" ซึ่งสองอย่างนี้แก้คนละวิธีกันคนละเรื่อง
  if (!rtcPresent) return false;

  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(static_cast<uint8_t>(0x00));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(static_cast<int>(DS1307_ADDRESS), 1) != 1) return false;

  return (Wire.read() & 0x80) != 0;
}

const char *rtcClockSource() { return clockSource; }

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
  return softwareClock.localEpoch(millis());
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

