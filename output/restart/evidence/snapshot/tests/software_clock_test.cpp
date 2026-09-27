#include <cassert>
#include "../ESP32_Main/rtc_lcd.cpp"
#include "../ESP32_Main/marquee.cpp"
uint32_t fakeMs = 0;
unsigned long millis() { return fakeMs; }
SerialClass Serial;
DS1307RTC RTC;
TwoWire Wire;
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, int) {}
int digitalRead(uint8_t) { return HIGH; }
void delayMicroseconds(unsigned int) {}
void refresh(uint32_t delta = 1000) { fakeMs += delta; rtcLcdUpdate(); }
int main() {
  // No RTC and no network time: never invent a usable schedule clock.
  refresh(); assert(!rtcIsValid() && rtcMinutesOfDay() == -1 && rtcDayKey() == 0);
  rtcSyncFromEpoch(0); rtcSyncFromEpoch(123); assert(!rtcIsValid());
  tmElements_t t = {}; t.Year=56; t.Month=9; t.Day=25; t.Hour=23; t.Minute=59; t.Second=59;
  const uint32_t midnightBefore = makeTime(t);
  rtcSyncFromEpoch(midnightBefore);
  assert(rtcIsValid() && rtcMinutesOfDay() == 1439 && RTC.writes == 0);
  assert(strcmp(rtcClockSource(), "SERVER") == 0);
  // Time and day roll forward without any further network calls.
  refresh(); assert(rtcDayKey() == 20260926 && rtcMinutesOfDay() == 0);
  refresh(3600000); assert(rtcMinutesOfDay() == 60);
  // Stale RTC and failed writes must not replace trustworthy server time.
  rtcPresent=true; RTC.readOk=true; RTC.value=t;
  rtcSyncFromEpoch(midnightBefore + 7201); refresh();
  assert(rtcMinutesOfDay() == 120 && strcmp(rtcClockSource(), "SERVER") == 0);
  RTC.readOk=false; refresh(); assert(rtcIsValid());
  // RTC later accepts a sync, then can provide the clock after reboot.
  RTC.writeOk=RTC.readOk=true; rtcSyncFromEpoch(midnightBefore+10801);
  assert(strcmp(rtcClockSource(), "RTC") == 0);
  softwareClock = {}; clockValid=false; refresh();
  assert(rtcIsValid() && strcmp(rtcClockSource(), "RTC") == 0);
  const uint32_t rebootRtcEpoch = makeTime(RTC.value);
  assert(rtcLocalEpoch() == rebootRtcEpoch);
  fakeMs += 2300; // No I2C refresh during a multi-channel motion loop.
  assert(rtcLocalEpoch() == rebootRtcEpoch + 2);
  // Fresh reboot with no RTC requires a new server sync.
  rtcPresent=false; softwareClock={}; clockValid=false; refresh();
  assert(!rtcIsValid() && rtcLocalEpoch() == 0);
  // Preserve fractional milliseconds and handle the 49-day millis wrap.
  SoftwareClock counter;
  counter.sync(midnightBefore, UINT32_MAX-499);
  assert(counter.localEpoch(0) == midnightBefore);
  assert(counter.localEpoch(500) == midnightBefore+1);
  assert(counter.localEpoch(750) == midnightBefore+1);
  assert(counter.localEpoch(1500) == midnightBefore+2);
}
