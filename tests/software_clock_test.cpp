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
// Power cycle: RAM state is lost, the NVS store (clockStore.scalars) and the RTC chip survive.
void reboot() {
  softwareClock = {}; clockValid = false; clockSource = "WAITING"; rtcLostTime = false;
  clockStoreOpened = false; trustedFloor = floorSaved = 0; timeVerified = false; rtcWasOff = 0;
  rtcHadTime = false; refreshedOnce = false;
  refresh();
}
void setRtc(uint32_t epoch) { breakTime(epoch, RTC.value); }
bool lcdShows(const char *text) { return strstr(shownTime[0], text) || strstr(shownTime[1], text); }
int main() {
  // The boot report reads the clock before millis() reaches 1000; that first read must happen.
  rtcLcdUpdate(); assert(lcdShows("WAIT FOR TIME") && lcdShows("CONNECT WI-FI") && !rtcHasTime());
  rtcSetNetworkOnline(true); fakeMs += 1000; rtcLcdUpdate();
  assert(lcdShows("GETTING TIME") && !lcdShows("CONNECT WI-FI"));  // Wi-Fi is fine, do not blame it
  rtcSetNetworkOnline(false);
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

  // --- RTC that stopped while unplugged ---
  rtcPresent = true; RTC.readOk = RTC.writeOk = true;
  const uint32_t t0 = midnightBefore + 100000;
  rtcSyncFromEpoch(t0);
  assert(clockStore.scalars["floor"] == t0 && rtcTimeVerified());
  // Dead backup battery: the DS1307 froze at t0 while real time moved on an hour.
  reboot();
  assert(strcmp(rtcClockSource(), "RTC") == 0 && !rtcLostTimeDetected());
  assert(!rtcTimeVerified() && lcdShows("UNSYNC"));  // cannot be detected, so it is flagged
  assert(!rtcWasOffAtLastBoot());
  rtcSyncFromEpoch(t0 + 3600); refresh();
  assert(rtcTimeVerified() && !lcdShows("UNSYNC") && rtcWasOffAtLastBoot());
  assert(clockStore.scalars["rtcoff"] == 1 && rtcLocalEpoch() >= t0 + 3600);  // stub RTC does not tick
  // Later syncs in the same boot do not clear the warning; only the next boot's first sync decides.
  rtcSyncFromEpoch(rtcLocalEpoch()); assert(rtcWasOffAtLastBoot());
  reboot(); assert(rtcWasOffAtLastBoot());  // survives the power cycle
  // Battery replaced: RTC kept time, first sync agrees, the warning clears and persists cleared.
  rtcSyncFromEpoch(rtcLocalEpoch() + 1);
  assert(!rtcWasOffAtLastBoot() && clockStore.scalars["rtcoff"] == 0);
  reboot(); assert(!rtcWasOffAtLastBoot() && lcdShows("UNSYNC"));  // every boot waits for the server
  // Drift within a minute on the first sync is not a battery problem.
  rtcSyncFromEpoch(rtcLocalEpoch() + 30); assert(!rtcWasOffAtLastBoot());

  // --- RTC chip running but holding garbage (e.g. reset to 2000-01-01): not "lost", just no time ---
  { tmElements_t junk = {}; junk.Year = 30; junk.Month = 1; junk.Day = 1; RTC.value = junk; }
  reboot();
  assert(!rtcIsValid() && !rtcHasTime() && !rtcLostTimeDetected() && lcdShows("WAIT FOR TIME"));
  rtcSyncFromEpoch(clockStore.scalars["floor"] + 5);
  reboot(); assert(rtcHasTime() && strcmp(rtcClockSource(), "RTC") == 0);

  // --- RTC reading earlier than time already seen (e.g. reset to compile time) ---
  const uint32_t floorNow = clockStore.scalars["floor"];
  setRtc(floorNow - 3600); reboot();
  assert(!rtcIsValid() && strcmp(rtcClockSource(), "WAITING") == 0);
  assert(rtcLostTimeDetected() && lcdShows("RTC LOST TIME") && lcdShows("CONNECT WI-FI"));
  refresh(); assert(!rtcIsValid());  // stays unused until the server answers
  rtcSyncFromEpoch(floorNow + 60);
  assert(rtcIsValid() && strcmp(rtcClockSource(), "RTC") == 0);
  reboot();
  assert(rtcIsValid() && strcmp(rtcClockSource(), "RTC") == 0 && !rtcLostTimeDetected());
  // Slightly behind the saved floor (floor saved between refreshes) is still trusted.
  const uint32_t floorAfterFix = clockStore.scalars["floor"];
  setRtc(floorAfterFix - RTC_BACKWARD_TOLERANCE_S); reboot();
  assert(strcmp(rtcClockSource(), "RTC") == 0 && !rtcLostTimeDetected());
  setRtc(floorAfterFix - RTC_BACKWARD_TOLERANCE_S - 1); reboot();
  assert(strcmp(rtcClockSource(), "WAITING") == 0 && rtcLostTimeDetected());
  // The server moving time back (timezone change) lowers the floor; no false alarm afterwards.
  const uint32_t earlier = floorAfterFix - 7200;
  rtcSyncFromEpoch(earlier);
  assert(clockStore.scalars["floor"] == earlier);
  reboot();
  assert(strcmp(rtcClockSource(), "RTC") == 0 && !rtcLostTimeDetected());
  // Running on the RTC advances the floor, saved only every CLOCK_FLOOR_SAVE_INTERVAL_S.
  setRtc(earlier + 10); reboot();
  assert(trustedFloor == earlier + 10 && clockStore.scalars["floor"] == earlier);
  setRtc(earlier + CLOCK_FLOOR_SAVE_INTERVAL_S); reboot();
  assert(clockStore.scalars["floor"] == earlier + CLOCK_FLOOR_SAVE_INTERVAL_S);
  // Even a small backward correction is saved at once, or the next boot would reject a correct RTC.
  rtcSyncFromEpoch(earlier + CLOCK_FLOOR_SAVE_INTERVAL_S - 100);
  assert(clockStore.scalars["floor"] == earlier + CLOCK_FLOOR_SAVE_INTERVAL_S - 100);
}
