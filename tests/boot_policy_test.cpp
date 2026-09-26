// เปิดเครื่อง: ถาม server ก่อน ไม่ได้ค่อยใช้ตารางในเครื่อง
#include <cassert>
#include "../ESP32_Main/boot_policy.h"

int main()
{
  const unsigned long WAIT = 20000;
  // เพิ่งเปิดเครื่อง ยังรอคำตอบจาก server: ห้ามใช้ของเก่า (อาจมียาที่ถูกลบไปแล้วบนเว็บ)
  assert(!shouldUseCachedSchedule(true, false, false, false, 3000, WAIT));
  // server ตอบแล้วว่าล้มเหลว: ใช้ของในเครื่องทันที ไม่ต้องรอครบเวลา
  assert(shouldUseCachedSchedule(true, false, false, true, 3000, WAIT));
  // Wi-Fi ต่อไม่ติดเลย ไม่ได้ถามด้วยซ้ำ: รอครบเวลาแล้วใช้ของในเครื่อง
  assert(!shouldUseCachedSchedule(true, false, false, false, WAIT - 1, WAIT));
  assert(shouldUseCachedSchedule(true, false, false, false, WAIT, WAIT));
  // server ให้ตารางมาแล้ว: ไม่ใช้ของเก่าทับ
  assert(!shouldUseCachedSchedule(true, true, false, true, 60000, WAIT));
  // ยังไม่รู้วันที่: เลือกมื้อของวันนี้ไม่ได้ รอก่อน
  assert(!shouldUseCachedSchedule(false, false, false, true, 60000, WAIT));
  // ลองไปแล้วครั้งหนึ่ง: ไม่อ่านแฟลชซ้ำทุกรอบ loop
  assert(!shouldUseCachedSchedule(true, false, true, true, 60000, WAIT));
  return 0;
}
