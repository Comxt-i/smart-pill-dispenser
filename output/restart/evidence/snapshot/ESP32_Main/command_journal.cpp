#include "command_journal.h"
#include "software_clock.h"
#include <Preferences.h>
namespace {
constexpr uint32_t MAGIC = 0x434D4431;
constexpr uint32_t KEEP_SECONDS = 86400; // Server command TTL is much shorter.
struct Entry { char id[72]; uint32_t epoch; };
struct Journal { uint32_t magic; Entry entries[64]; };
Journal journal = {};
Preferences commandStorage;
bool journalReady = false;
}
void commandJournalDoseKey(char *out, size_t size, const char *scheduleId, uint32_t day, bool dryRun) {
  snprintf(out, size, "dose:%s:%lu:%c", scheduleId, static_cast<unsigned long>(day), dryRun ? 'T' : 'R');
}
void commandJournalBegin() {
  journal = {}; journal.magic = MAGIC;
  journalReady = commandStorage.begin("pillcmd", false);
  if (!journalReady) return;
  const size_t size = commandStorage.getBytesLength("journal");
  if (!size) return;
  if (size != sizeof(journal) || commandStorage.getBytes("journal", &journal, sizeof(journal)) != sizeof(journal)
      || journal.magic != MAGIC) { journalReady = false; return; }
  for (const auto &e : journal.entries)
    if (!memchr(e.id, 0, sizeof(e.id))) { journalReady = false; return; }
}
bool commandJournalContains(const char *id) {
  if (!id || !*id) return false;
  for (const auto &e : journal.entries) if (strcmp(e.id, id) == 0) return true;
  return false;
}
bool commandJournalReserve(const char *id, uint32_t epoch) {
  if (!journalReady || !id || !*id || strlen(id) >= sizeof(Entry::id) ||
      !SoftwareClock::validEpoch(epoch) || commandJournalContains(id)) return false;
  for (auto &e : journal.entries) {
    if (!e.id[0] || (epoch > e.epoch && epoch - e.epoch > KEEP_SECONDS)) {
      const Entry previous = e;
      e = {}; strcpy(e.id, id); e.epoch = epoch;
      if (commandStorage.putBytes("journal", &journal, sizeof(journal)) == sizeof(journal)) return true;
      e = previous;
      return false;
    }
  }
  return false;
}
