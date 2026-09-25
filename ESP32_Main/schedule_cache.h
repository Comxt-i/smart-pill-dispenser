#pragma once

#include <Arduino.h>

// ตารางยาในเครื่อง: ใช้เตือนได้ทันทีหลังเปิดเครื่อง และตลอดช่วงที่เน็ตหรือ server ล่ม
//
// เก็บสองอย่างใน NVS
//   1. คำตอบ sync ล่าสุด (ตารางทั้งสัปดาห์ schema 2) เขียนเฉพาะตอน state_version เปลี่ยน
//   2. มื้อที่ "จบแล้ววันนี้" เขียนทันทีที่มื้อนั้นจบในเครื่อง
//
// ข้อ 2 คือสิ่งที่กันจ่ายยาซ้ำ: ถ้ากินไปแล้วแต่ยังส่งผลขึ้น server ไม่ทัน แล้วไฟดับ
// ตอนเปิดใหม่โดยไม่มีเน็ต ตารางที่เก็บไว้ยังบอกว่ามื้อนั้น "ยังไม่กิน" เครื่องจะเตือนซ้ำ
// แล้วผู้ใช้กดรับยาอีกรอบ รายการนี้คือหลักฐานในเครื่องที่ไม่ต้องรอ server

void scheduleCacheBegin();

/**
 * เก็บคำตอบ sync ถ้า state_version ต่างจากที่เก็บไว้ (ไม่เขียน NVS ซ้ำๆ ทุกรอบ)
 * `dayKey` คือวันที่ของเครื่องตอนได้ข้อมูลนี้ ใช้ตัดสินว่าสถานะ "กินแล้ว" ของ server ยังใช้ได้ไหม
 * คืน true เมื่อเขียนจริง
 */
bool scheduleCacheStore(const String &body, const char *stateVersion, uint32_t dayKey);

/** คำตอบ sync ที่เก็บไว้ล่าสุด คืน false ถ้าไม่มีหรือเสียหาย */
bool scheduleCacheLoad(String &body, uint32_t &fetchedDayKey);

/** state_version ของข้อมูลที่เก็บอยู่ ("" ถ้าไม่มี) */
const char *scheduleCacheStateVersion();

/** มื้อนี้จบแล้ววันนี้ (จ่ายแล้ว ข้าม พลาด ล้มเหลว หรือกำลังจ่ายตอนไฟดับ) ห้ามเตือนซ้ำ */
void scheduleCacheMarkClosed(const char *scheduleId, uint32_t dayKey);
bool scheduleCacheIsClosed(const char *scheduleId, uint32_t dayKey);
