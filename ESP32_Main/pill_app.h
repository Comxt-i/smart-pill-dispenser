#pragma once

#include <Arduino.h>

/** ตรรกะหลักของกล่องยา: รวมตารางยา ปุ่มกด เสียงเตือน จอ และการคุยกับ server */
void appBegin();
void appLoop();

/**
 * สั่งจ่ายยาจากหน้าเว็บในเครื่อง (สำหรับทดสอบหน้างาน)
 * ใช้เส้นทางเดียวกับการกดปุ่มจึงถูกบันทึกขึ้น server เหมือนกัน
 * คืนข้อความอธิบายผลเพื่อนำไปแสดงบนหน้าเว็บ และตั้ง httpStatus ให้ผู้เรียก
 */
const char *appManualDispense(uint8_t slotNumber, float amount, int &httpStatus);

/** สรุปสถานะปัจจุบันเป็น JSON สำหรับหน้าเว็บในเครื่องและการไล่ปัญหา */
void appStatusJson(String &out);
