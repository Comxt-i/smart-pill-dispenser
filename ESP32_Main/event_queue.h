#pragma once

#include <Arduino.h>

#include "config.h"

/**
 * ผลการจ่ายยาหนึ่งรายการที่รอส่งขึ้น server
 *
 * eventId เป็นกุญแจกันบันทึกซ้ำ: ถ้าส่งไปแล้วแต่ไม่ได้รับคำตอบ (เน็ตหลุดตอนขากลับ)
 * การส่งซ้ำด้วย eventId เดิมจะไม่ทำให้เกิด log ซ้ำในฐานข้อมูล
 */
struct PendingEvent {
  char eventId[24];
  char status[14];      // DISPENSED, MISSED, SKIPPED, FAILED
  char scheduleId[40];
  char medicationId[40];
  char commandId[40];
  char note[32];
  uint8_t slot;
  float amount;
  uint32_t localEpoch;  // เวลาท้องถิ่นที่เกิดเหตุ (0 = นาฬิกายังไม่พร้อม)
};

/**
 * โหลดผลการจ่ายยาที่ยังไม่ได้ส่งกลับมาหลังไฟดับหรือรีบูต (เรียกหลัง flashStoreBegin())
 * เก็บในไฟล์ถ้ามีพาร์ทิชันไฟล์ ไม่อย่างนั้นเก็บใน NVS แบบจำกัดจำนวน
 */
void eventQueueBegin();

/** ผลที่เก็บรอส่งได้สูงสุดกี่รายการ (ขึ้นกับว่ามีพาร์ทิชันไฟล์ไหม) */
uint8_t eventQueueCapacity();

/** สร้าง eventId ที่ไม่ซ้ำสำหรับเหตุการณ์ใหม่ (ใช้ MAC + ตัวนับที่เก็บใน NVS) */
void eventQueueMakeId(char *out, size_t size);

/**
 * ใส่รายการเข้าคิว คิวเต็มจะทิ้งรายการเก่าสุดทิ้ง
 * คืน false เมื่อต้องทิ้งของเก่า เพื่อให้ผู้เรียกเตือนใน Serial ได้
 */
bool eventQueuePush(const PendingEvent &event);

uint8_t eventQueueSize();
const PendingEvent &eventQueueAt(uint8_t index);

/** ลบรายการที่ server ตอบรับแล้ว (รวมถึงรายการที่ถูกปฏิเสธถาวร) */
void eventQueueRemove(const char *eventId);

/** เขียนคิวปัจจุบันลง NVS เรียกหลังแก้ไขคิวเสร็จ */
void eventQueuePersist();
