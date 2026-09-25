#include <cassert>
#include "../ESP32_Main/command_journal.cpp"
SerialClass Serial;
int main() {
  constexpr uint32_t now = 1800000000;
  commandJournalBegin();
  assert(!commandJournalReserve("bad-time", 0));
  assert(commandJournalReserve("dose-1", now));
  assert(commandJournalContains("dose-1"));
  assert(!commandJournalReserve("dose-1", now));
  commandJournalBegin(); // Simulated reboot reloads the same NVS blob.
  assert(commandJournalContains("dose-1"));
  assert(!commandJournalReserve("dose-1", now + 10));
  commandStorage.writeOk = false;
  assert(!commandJournalReserve("failed-write", now));
  assert(!commandJournalContains("failed-write"));
  commandStorage.writeOk = true;
  for (int i = 1; i < 64; ++i) { char id[40]; snprintf(id, sizeof(id), "dose-%d", i + 1); assert(commandJournalReserve(id, now)); }
  assert(!commandJournalReserve("overflow", now));
  assert(!commandJournalReserve("clock-backwards", now - 10));
  assert(commandJournalReserve("next-day", now + KEEP_SECONDS + 1));
  commandStorage.bytes[0] ^= 0xff;
  commandJournalBegin();
  assert(!commandJournalReserve("corrupt-journal", now));
}
