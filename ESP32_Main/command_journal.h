#pragma once
#include <Arduino.h>
/** เรียกหลัง flashStoreBegin() เพื่อให้บันทึกอยู่บนพาร์ทิชันไฟล์ (ย้ายของเดิมจาก NVS ให้เอง) */
void commandJournalBegin();
void commandJournalDoseKey(char *out, size_t size, const char *scheduleId, uint32_t day, bool dryRun);
bool commandJournalContains(const char *id);
// Persists before motion; false means do NOT execute (storage full/unavailable).
bool commandJournalReserve(const char *id, uint32_t epoch);
/**
 * ปลดการจองที่ยังไม่ได้ขยับกลไกเลย (ถูกปฏิเสธก่อนหมุน เช่นเซ็นเซอร์ใช้ไม่ได้ servo ไม่ตอบ)
 * ห้ามเรียกถ้ากลไกเริ่มขยับแล้ว เพราะยาอาจออกมาแล้ว การจองต้องค้างไว้กันจ่ายซ้ำ
 */
bool commandJournalRelease(const char *id);
/** เหตุที่ commandJournalReserve ครั้งล่าสุดไม่ผ่าน ภาษาอังกฤษสั้นๆ (ไม่เกิน 15 ตัว ให้พอดีช่องบันทึก 32 ตัว) "" = ผ่าน */
const char *commandJournalLastError();
