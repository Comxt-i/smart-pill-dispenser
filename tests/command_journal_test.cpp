#include <cassert>
#include <cstring>
#include "../ESP32_Main/flash_store.cpp"
#include "../ESP32_Main/command_journal.cpp"
SerialClass Serial;

static bool is(const char *expected) { return strcmp(commandJournalLastError(), expected) == 0; }

int main() {
  constexpr uint32_t now = 1800000000;

  // ---- บอร์ดที่ไม่มีพาร์ทิชันไฟล์: ใช้ NVS แบบเดิม ----
  assert(!flashStoreReady());
  commandJournalBegin();
  assert(!commandJournalReserve("bad-time", 0) && is("clock not set"));
  assert(commandJournalReserve("dose-1", now) && is(""));
  assert(commandJournalContains("dose-1"));
  assert(!commandJournalReserve("dose-1", now) && is("already started"));
  commandJournalBegin(); // Simulated reboot reloads the same NVS blob.
  assert(commandJournalContains("dose-1"));
  assert(!commandJournalReserve("dose-1", now + 10));
  commandStorage.writeOk = false;
  assert(!commandJournalReserve("failed-write", now) && is("write failed"));
  assert(!commandJournalContains("failed-write"));
  commandStorage.writeOk = true;
  for (int i = 1; i < 64; ++i) { char id[40]; snprintf(id, sizeof(id), "dose-%d", i + 1); assert(commandJournalReserve(id, now)); }
  assert(!commandJournalReserve("overflow", now) && is("full"));
  assert(!commandJournalReserve("clock-backwards", now - 10));
  assert(commandJournalReserve("next-day", now + KEEP_SECONDS + 1));

  // ถูกปฏิเสธก่อนกลไกขยับ: ปลดการจองแล้วกดรับมื้อนั้นใหม่ได้ และยังจำหลังรีบูต
  assert(commandJournalContains("dose-5"));
  assert(commandJournalRelease("dose-5") && !commandJournalContains("dose-5"));
  commandJournalBegin();
  assert(!commandJournalContains("dose-5"));
  assert(commandJournalReserve("dose-5", now + 20));
  assert(!commandJournalRelease("never-reserved"));
  // ปลดแล้วเขียนไม่ผ่าน = ต้องยังล็อกอยู่ (ปลอดภัยกว่าปลดแค่ในหน่วยความจำแล้วรีบูตกลับมาล็อกเอง)
  commandStorage.writeOk = false;
  assert(!commandJournalRelease("dose-5") && commandJournalContains("dose-5"));
  commandStorage.writeOk = true;

  // บันทึกเสีย = ไม่รู้ว่าวันนี้จ่ายอะไรไปแล้ว ห้ามจ่ายต่อ
  const auto goodNvs = commandStorage.bytes;
  commandStorage.bytes[0] ^= 0xff;
  commandJournalBegin();
  assert(!commandJournalReserve("corrupt-journal", now) && is("corrupted"));
  commandStorage.bytes = goodNvs;

  // ---- มีพาร์ทิชันไฟล์: ย้ายบันทึกเดิมจาก NVS มาไว้ในไฟล์ แล้วคืนที่ใน NVS ----
  assert(flashStoreBegin());
  commandJournalBegin();
  assert(commandJournalContains("dose-3") && commandJournalContains("next-day"));  // ของเดิมตามมาครบ
  assert(commandStorage.bytes.empty());                                            // ลบออกจาก NVS แล้ว
  assert(flashStoreSize("/journal.bin") == sizeof(Journal));
  assert(!commandJournalReserve("dose-3", now + 30) && is("already started"));     // ยังกันจ่ายซ้ำได้

  // NVS เต็มจนเขียนไม่ได้ ไม่กระทบอีกแล้ว (นี่คือเหตุที่เคยขึ้น "dose locked" ทั้งที่ไม่เคยจ่าย)
  commandStorage.writeOk = false;
  assert(commandJournalReserve("dose-after-move", now + 2 * KEEP_SECONDS));
  commandJournalBegin();  // รีบูต: อ่านจากไฟล์
  assert(commandJournalContains("dose-after-move"));
  assert(commandJournalRelease("dose-after-move"));
  commandJournalBegin();
  assert(!commandJournalContains("dose-after-move"));
  commandStorage.writeOk = true;

  // แฟลชเขียนไม่ผ่าน: ไม่จองในหน่วยความจำอย่างเดียว บอกเหตุตรงๆ
  fakeFlash().writeOk = false;
  assert(!commandJournalReserve("flash-full", now + 2 * KEEP_SECONDS) && is("write failed"));
  assert(!commandJournalContains("flash-full"));
  fakeFlash().writeOk = true;

  // ไฟล์เสีย: ห้ามจ่าย
  fakeFlash().files["/journal.bin"][0] ^= 0xff;
  commandJournalBegin();
  assert(!commandJournalReserve("corrupt-file", now + 2 * KEEP_SECONDS) && is("corrupted"));
}
