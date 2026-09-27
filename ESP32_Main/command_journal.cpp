#include "command_journal.h"
#include "flash_store.h"
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
bool onFlash = false;
const char *reserveError = "";

// บันทึกนี้ใหญ่ราว 4.9 KB เดิมอยู่ใน NVS ซึ่งใช้ได้จริงแค่ราว 16 KB และต้องแบ่งกับ Wi-Fi
// การเขียนทับ blob ต้องมีที่ว่างเท่าขนาดใหม่ก่อนลบของเก่า พอ NVS แน่น การจองมื้อยาเขียนไม่ผ่าน
// ทุกการจ่ายจึงถูกปฏิเสธว่า "dose locked" ทั้งที่มื้อนั้นไม่เคยจ่าย จึงย้ายมาไว้บนพาร์ทิชันไฟล์
// (เหมือนคิวผลการจ่ายยาและตารางยาในเครื่อง) NVS ใช้เฉพาะบอร์ดที่ไม่มีพาร์ทิชันไฟล์
constexpr const char *JOURNAL_FILE = "/journal.bin";

bool validJournal(const Journal &candidate)
{
  if (candidate.magic != MAGIC) return false;
  for (const auto &e : candidate.entries)
    if (!memchr(e.id, 0, sizeof(e.id))) return false;
  return true;
}

bool persist()
{
  if (onFlash) return flashStoreWrite(JOURNAL_FILE, &journal, sizeof(journal));
  return commandStorage.putBytes("journal", &journal, sizeof(journal)) == sizeof(journal);
}

/**
 * โหลดบันทึกเดิมจาก NVS (ใช้ทั้งตอนย้ายและตอนไม่มีพาร์ทิชันไฟล์) คืน false ถ้ามีแต่เสีย
 *
 * อ่านเข้าตัวแปรหลักตรงๆ ห้ามทำสำเนาบน stack: บันทึกใหญ่ 4.9 KB แต่ stack ของ setup() มีแค่ 8 KB
 * สำเนาสองชุดทำ stack ล้น เครื่องรีบูตวนตั้งแต่บูต
 */
bool loadFromNvs(bool &found)
{
  found = false;
  const size_t size = commandStorage.getBytesLength("journal");
  if (!size) return true;
  found = true;
  if (size != sizeof(journal) || commandStorage.getBytes("journal", &journal, sizeof(journal)) != sizeof(journal)
      || !validJournal(journal)) return false;
  return true;
}
}
void commandJournalDoseKey(char *out, size_t size, const char *scheduleId, uint32_t day, bool dryRun) {
  snprintf(out, size, "dose:%s:%lu:%c", scheduleId, static_cast<unsigned long>(day), dryRun ? 'T' : 'R');
}
void commandJournalBegin() {
  journal = {}; journal.magic = MAGIC;
  journalReady = false;
  reserveError = "";
  onFlash = flashStoreReady();
  const bool nvsReady = commandStorage.begin("pillcmd", false);

  if (onFlash) {
    const size_t size = flashStoreSize(JOURNAL_FILE);
    if (size) {
      // บันทึกเสีย = ไม่รู้ว่าวันนี้จ่ายอะไรไปแล้ว ห้ามจ่ายต่อ (ปลอดภัยไว้ก่อน กันให้ยาซ้ำ)
      if (size != sizeof(journal) || !flashStoreRead(JOURNAL_FILE, &journal, sizeof(journal)) || !validJournal(journal)) {
        reserveError = "corrupted";
        return;
      }
      journalReady = true;
      return;
    }
    // ย้ายของเดิมจาก NVS มาไว้ในไฟล์ แล้วลบออกจาก NVS เพื่อคืนที่ให้ Wi-Fi และค่าอื่น
    bool found = false;
    if (nvsReady && !loadFromNvs(found)) { reserveError = "corrupted"; return; }
    if (found && !flashStoreWrite(JOURNAL_FILE, &journal, sizeof(journal))) { reserveError = "write failed"; return; }
    if (found) commandStorage.remove("journal");
    journalReady = true;
    return;
  }

  if (!nvsReady) { reserveError = "unavailable"; return; }
  bool found = false;
  if (!loadFromNvs(found)) { reserveError = "corrupted"; return; }
  journalReady = true;
}
bool commandJournalContains(const char *id) {
  if (!id || !*id) return false;
  for (const auto &e : journal.entries) if (strcmp(e.id, id) == 0) return true;
  return false;
}
bool commandJournalReserve(const char *id, uint32_t epoch) {
  if (!journalReady) { if (!*reserveError) reserveError = "unavailable"; return false; }
  if (!id || !*id || strlen(id) >= sizeof(Entry::id)) { reserveError = "bad id"; return false; }
  if (!SoftwareClock::validEpoch(epoch)) { reserveError = "clock not set"; return false; }
  if (commandJournalContains(id)) { reserveError = "already started"; return false; }
  for (auto &e : journal.entries) {
    if (!e.id[0] || (epoch > e.epoch && epoch - e.epoch > KEEP_SECONDS)) {
      const Entry previous = e;
      e = {}; strcpy(e.id, id); e.epoch = epoch;
      if (persist()) { reserveError = ""; return true; }
      e = previous;
      reserveError = "write failed";
      return false;
    }
  }
  reserveError = "full";
  return false;
}
bool commandJournalRelease(const char *id) {
  if (!journalReady || !id || !*id) return false;
  for (auto &e : journal.entries) {
    if (strcmp(e.id, id) != 0) continue;
    const Entry previous = e;
    e = {};
    if (persist()) return true;
    e = previous;  // เขียนไม่ผ่าน = ยังล็อกไว้ ปลอดภัยกว่าปลดแค่ในหน่วยความจำ
    return false;
  }
  return false;
}
const char *commandJournalLastError() { return reserveError; }
