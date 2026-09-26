#pragma once

// เปิดเครื่องแล้วจะใช้ตารางยาที่เก็บไว้ในเครื่องเมื่อไร
//
// ถาม server ก่อนเสมอ เพราะตารางในเครื่องอาจเก่า: ญาติลบยาหรือแก้เวลาบนเว็บตอนกล่องปิดอยู่
// ถ้าใช้ของเก่าทันที ระหว่างรอ sync กล่องอาจเตือนยาที่ถูกลบไปแล้ว และผู้ใช้กดรับทัน = จ่ายยาที่ไม่ควรจ่าย
//
// ใช้ของในเครื่องเมื่อ server ไม่ได้ให้คำตอบ:
//   - sync ครั้งแรกจบแล้วแต่ไม่สำเร็จ (server ล่ม เน็ตมีปัญหา) = ใช้ทันที
//   - รอเกิน serverWaitMs โดยยังไม่ได้ถามด้วยซ้ำ (Wi-Fi ต่อไม่ติด) = ใช้ตอนหมดเวลา
// sync สำเร็จ = มีตารางจาก server แล้ว ไม่ต้องใช้ของในเครื่อง
inline bool shouldUseCachedSchedule(bool clockValid,
                                    bool haveSchedule,
                                    bool alreadyTried,
                                    bool firstSyncFinished,
                                    unsigned long msSinceBoot,
                                    unsigned long serverWaitMs)
{
  if (!clockValid || haveSchedule || alreadyTried)
    return false;  // ไม่รู้วันที่ก็เลือกมื้อของวันนี้ไม่ได้ / มีตารางแล้ว / ลองไปแล้ว
  return firstSyncFinished || msSinceBoot >= serverWaitMs;
}
