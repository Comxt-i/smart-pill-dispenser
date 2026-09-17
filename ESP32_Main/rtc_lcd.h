#pragma once

#include <Arduino.h>

void rtcLcdBegin();

/** รีเฟรชจอเวลาทุก 1 วินาที ต้องเรียกทุกรอบ loop */
void rtcLcdUpdate();

// ---- นาฬิกา ----

/** ตั้ง DS1307 ตามเวลาท้องถิ่นที่ server ส่งมา (เขียนเฉพาะเมื่อเพี้ยนเกิน 2 วินาที) */
void rtcSyncFromEpoch(uint32_t localEpoch);

/** false เมื่ออ่าน DS1307 ไม่ได้หรือยังไม่เคยตั้งเวลา ห้ามใช้ตารางยาตัดสินใจขณะนี้ */
bool rtcIsValid();

/** นาทีนับจากเที่ยงคืน (-1 เมื่อนาฬิกายังไม่พร้อม) */
int rtcMinutesOfDay();

/** รหัสวันแบบ YYYYMMDD ใช้ตรวจว่าข้ามวันแล้วหรือยัง */
uint32_t rtcDayKey();

/** epoch ของเวลาท้องถิ่นปัจจุบัน (0 เมื่อนาฬิกายังไม่พร้อม) */
uint32_t rtcLocalEpoch();

// ---- จอที่สอง: ยาและมื้อถัดไป ----

/** ข้อความสองบรรทัดทั่วไป ใช้กับสถานะระหว่างทำงาน */
void lcdShowMessage(const char *line1, const char *line2);

/** มื้อถัดไปของวันนี้ */
void lcdShowNextDose(const char *medicineName, int minutes);

/** กำลังเตือนให้ผู้ใช้กดปุ่มรับยา */
void lcdShowAlert(const char *medicineName, float amount, int minutes);

/** ไม่มีมื้อยาเหลือในวันนี้ */
void lcdShowIdle(bool online);
