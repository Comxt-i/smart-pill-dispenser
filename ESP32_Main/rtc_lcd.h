#pragma once

#include <Arduino.h>

void rtcLcdBegin();

/**
 * สแกนบัส I2C แล้วรายงานว่าพบอุปกรณ์ที่คาดหวังครบไหม
 *
 * ถ้า address ของ LCD หรือ RTC ไม่ตรงกับที่ตั้งไว้ จอจะดับเงียบๆ โดยไม่มี error
 * การสแกนตอนบูตทำให้รู้ทันทีว่าสายหลุดหรือ address ผิด แทนที่จะไปนั่งเดา
 *
 * คืน false เมื่อมีอุปกรณ์ที่คาดหวังหายไปอย่างน้อยหนึ่งตัว
 */
bool i2cScanAndReport();

/** จำนวน address ที่เจอตอนสแกนล่าสุด */
uint8_t i2cFoundCount();

/** address ลำดับที่ index ที่เจอตอนสแกนล่าสุด */
uint8_t i2cFoundAddress(uint8_t index);

/**
 * สภาพของเส้น SDA/SCL ตอนว่าง ใช้แทนการวัดด้วยมัลติมิเตอร์
 *
 * I2C ที่ปกติต้องถูก pull-up ขึ้นเป็น HIGH ทั้งสองเส้นตอนไม่มีใครใช้บัส
 * ถ้าอ่านได้ LOW แปลว่าไม่มี pull-up หรือสายลัดลง GND ซึ่งทำให้สแกนไม่เจออะไรเลย
 */
struct I2cLineState {
  bool sdaHighWithoutPullup;  // มี pull-up ภายนอกไหม
  bool sclHighWithoutPullup;
  bool sdaHighWithPullup;     // ขาใช้งานได้ไหม (ไม่ได้ลัดลง GND)
  bool sclHighWithPullup;
};

/** ตรวจสภาพเส้นสัญญาณ ต้องเรียกก่อน Wire.begin() เท่านั้น */
I2cLineState i2cCheckLines();

/** ผลตรวจครั้งล่าสุด ใช้แสดงบนหน้าเว็บ */
I2cLineState i2cLastLineState();

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
//
// บรรทัดบนคือชื่อยา ถ้ายาวเกิน 16 ตัวอักษรจะเลื่อนวนให้อ่านครบ
// บรรทัดล่างสลับไปมาระหว่างข้อมูลมื้อยากับคำแนะนำว่าปุ่มไหนทำอะไร
// ทั้งสองอย่างเดินด้วย lcdMedicineTick() ซึ่งต้องเรียกทุกรอบ loop

/**
 * ตั้งสิ่งที่จะแสดงบนจอยา
 *
 * lines  ข้อความของแต่ละบรรทัดไล่จากบนลงล่าง แต่ละบรรทัดเลื่อนวนเองถ้ายาวเกินจอ
 * hints  ข้อความที่สลับกันแสดงบน **บรรทัดสุดท้าย** ของจอ
 *        ส่ง 0 ถ้าไม่ต้องการให้บรรทัดสุดท้ายสลับ
 *
 * เรียกซ้ำด้วยค่าเดิมได้ทุกรอบ loop โดยภาพจะไม่กระตุก
 */
void lcdSetMedicineScreen(const char *const *lines,
                          uint8_t lineCount,
                          const char *const *hints,
                          uint8_t hintCount);

/** รูปแบบย่อสำหรับข้อความสองบรรทัด (บรรทัดที่เหลือจะถูกล้าง) */
void lcdShowMessage(const char *line1, const char *line2);

/** ขยับข้อความเลื่อนและสลับบรรทัดล่าง ต้องเรียกทุกรอบ loop */
void lcdMedicineTick();
