// ตารางยาในเครื่องและรายการ "มื้อที่จบแล้ววันนี้" ต้องรอดการรีบูต
// เรียก scheduleCacheBegin() ซ้ำ = จำลองเปิดเครื่องใหม่ (อ่านกลับจาก NVS ปลอม)
#include <cassert>
#include <cstring>
#include "../ESP32_Main/flash_store.cpp"
#include "../ESP32_Main/schedule_cache.cpp"

SerialClass Serial;

int main()
{
  scheduleCacheBegin();
  String body;
  uint32_t day = 0;
  assert(!scheduleCacheLoad(body, day));  // เครื่องใหม่ยังไม่มีอะไร
  assert(scheduleCacheStateVersion()[0] == '\0');

  // ---- ตารางยา ----
  const String weekly("{\"slots\":[{\"slot\":1}],\"state_version\":\"aaaa1111\"}");
  assert(scheduleCacheStore(weekly, "aaaa1111", 20260926));
  assert(!scheduleCacheStore(weekly, "aaaa1111", 20260926));  // เหมือนเดิม: ไม่เขียน NVS ซ้ำ
  assert(!scheduleCacheStore(weekly, "", 20260926));          // server รุ่นเก่าไม่มีรุ่น: ไม่เก็บ

  scheduleCacheBegin();  // รีบูต
  assert(strcmp(scheduleCacheStateVersion(), "aaaa1111") == 0);
  assert(scheduleCacheLoad(body, day));
  assert(strcmp(body.c_str(), weekly.c_str()) == 0 && day == 20260926);

  // วันใหม่รุ่นเดิมก็ต้องเขียน เพราะสถานะ "กินแล้ว" ของ server ผูกกับวันที่ได้ข้อมูลมา
  assert(scheduleCacheStore(weekly, "aaaa1111", 20260927));
  assert(scheduleCacheLoad(body, day) && day == 20260927);

  // ใหญ่เกินที่ NVS รับได้: ไม่เก็บ และของเดิมยังอยู่ครบ
  std::string huge(7000, 'x');
  assert(!scheduleCacheStore(String(huge.c_str()), "bbbb2222", 20260927));
  assert(scheduleCacheLoad(body, day) && strcmp(body.c_str(), weekly.c_str()) == 0);

  // ข้อมูลในแฟลชเสียหาย: ต้องถือว่าไม่มี ห้ามเอาไปเตือนผิดๆ
  bodyStore.bytes[0] ^= 0xFF;
  scheduleCacheBegin();
  assert(!scheduleCacheLoad(body, day) && scheduleCacheStateVersion()[0] == '\0');
  bodyStore.bytes.resize(bodyStore.bytes.size() - 3);
  assert(!scheduleCacheLoad(body, day));

  // ---- มื้อที่จบแล้ววันนี้ (กันจ่ายซ้ำ) ----
  assert(!scheduleCacheIsClosed("dose-1", 20260926));
  scheduleCacheMarkClosed("dose-1", 20260926);
  scheduleCacheMarkClosed("dose-1", 20260926);  // ซ้ำ ไม่เพิ่มรายการ
  scheduleCacheMarkClosed("dose-2", 20260926);
  assert(closed.count == 2);

  scheduleCacheBegin();  // ไฟดับแล้วเปิดใหม่
  assert(scheduleCacheIsClosed("dose-1", 20260926) && scheduleCacheIsClosed("dose-2", 20260926));
  assert(!scheduleCacheIsClosed("dose-3", 20260926));

  // วันใหม่: มื้อเดียวกันของพรุ่งนี้ยังไม่ได้กิน ห้ามถูกนับว่าจบแล้ว
  assert(!scheduleCacheIsClosed("dose-1", 20260927));
  scheduleCacheMarkClosed("dose-3", 20260927);
  assert(scheduleCacheIsClosed("dose-3", 20260927) && !scheduleCacheIsClosed("dose-1", 20260927));
  scheduleCacheBegin();
  assert(!scheduleCacheIsClosed("dose-1", 20260926));  // รายการของเมื่อวานถูกล้างถาวรแล้ว

  // ค่าไม่ถูกต้องต้องไม่ทำอะไร
  scheduleCacheMarkClosed("", 20260927);
  scheduleCacheMarkClosed(nullptr, 20260927);
  scheduleCacheMarkClosed("dose-9", 0);
  assert(closed.count == 1);

  // ---- มีพาร์ทิชันไฟล์: ตารางอยู่ในไฟล์ ไม่กินที่ NVS ----
  bodyStore.bytes.assign(40, 0xAB);  // ของเก่าที่ firmware รุ่นก่อนเก็บไว้ใน NVS
  flashStoreBegin();
  scheduleCacheBegin();
  assert(bodyStore.bytes.empty());  // คืนที่ให้ NVS แล้ว
  assert(scheduleCacheStore(weekly, "cccc3333", 20260928));
  assert(bodyStore.bytes.empty());  // เขียนลงไฟล์ ไม่ใช่ NVS
  assert(fakeFlash().files.count("/schedule.bin") == 1);
  scheduleCacheBegin();  // รีบูต
  assert(strcmp(scheduleCacheStateVersion(), "cccc3333") == 0);
  assert(scheduleCacheLoad(body, day) && strcmp(body.c_str(), weekly.c_str()) == 0 && day == 20260928);
  // ไฟดับกลางการเขียนรุ่นใหม่: ยังได้รุ่นเดิมครบ
  fakeFlash().renameOk = false;
  assert(!scheduleCacheStore(String("{\"slots\":[]}"), "dddd4444", 20260928));
  fakeFlash().renameOk = true;
  scheduleCacheBegin();
  assert(strcmp(scheduleCacheStateVersion(), "cccc3333") == 0);
  // ไฟล์เสีย: ถือว่าไม่มี
  fakeFlash().files["/schedule.bin"][0] ^= 0xFF;
  scheduleCacheBegin();
  assert(!scheduleCacheLoad(body, day));
  return 0;
}
