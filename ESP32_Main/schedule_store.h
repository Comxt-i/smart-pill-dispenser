#pragma once

#include <Arduino.h>

#include "config.h"

/** สถานะของมื้อยาหนึ่งมื้อภายในวันนี้ */
enum class DoseState : uint8_t {
  Pending,     // ยังไม่ถึงเวลา
  Alerting,    // ถึงเวลาแล้ว กำลังเตือนให้ผู้ใช้กดปุ่ม
  Dispensing,  // ผู้ใช้กดปุ่มแล้ว จานกำลังหมุน
  Done,        // จ่ายสำเร็จ (หรือ server บอกว่ามื้อนี้จบไปแล้ว)
  Missed,      // เลยเวลาผ่อนผันโดยไม่มีใครกดปุ่ม
  Skipped,     // ผู้ใช้กด Cancel เพื่อข้ามมื้อนี้
};

struct Dose {
  char scheduleId[40];
  char label[20];
  int minutes;  // นาทีนับจากเที่ยงคืน
  DoseState state;
  bool failureReported;  // กันส่ง FAILED ซ้ำเมื่อผู้ใช้กดจ่ายแล้วพลาดหลายครั้ง
};

struct Slot {
  uint8_t number;  // 1..DISPENSER_COUNT
  bool active;
  char medicationId[40];
  char name[20];  // ชื่อย่อสำหรับ LCD (server ตัดมาให้ไม่เกิน 16 ตัวอักษร)
  float amountPerDose;
  Dose doses[MAX_DOSES_PER_SLOT];
  uint8_t doseCount;
};

/** ตัวชี้ไปยังมื้อยาหนึ่งมื้อ ใช้ส่งต่อระหว่างโมดูลโดยไม่ต้องคัดลอกข้อมูล */
struct DoseRef {
  uint8_t slotIndex;
  uint8_t doseIndex;
  bool valid;
};

/** เรียกทุกครั้งที่มื้อยาเปลี่ยนสถานะ ใช้ให้ loop หลักส่งผลขึ้น server */
typedef void (*DoseStateChangedFn)(const DoseRef &ref, DoseState previous, DoseState next);

void scheduleBegin(DoseStateChangedFn onStateChanged);

// ---- การรับข้อมูลจาก server ----
// ใช้ buffer พักเพื่อไม่ให้ตารางที่ใช้งานอยู่เสียหายถ้า sync ล้มเหลวกลางทาง

void scheduleBeginSync();
int scheduleStageSlot(uint8_t number, bool active, const char *medicationId, const char *name, float amountPerDose);
void scheduleStageDose(int slotIndex, const char *scheduleId, const char *label, int minutes, bool doneOnServer);
/** ย้ายข้อมูลจาก buffer พักมาใช้จริง โดยคงสถานะของมื้อยาที่ยังเป็น id เดิมไว้ */
void scheduleCommitSync();

// ---- การใช้งาน ----

uint8_t scheduleSlotCount();
const Slot &scheduleSlot(uint8_t index);
/** -1 เมื่อไม่พบช่องนั้น */
int scheduleFindSlot(uint8_t slotNumber);

/**
 * หามื้อยาจาก schedule_id
 * ใช้อ้างอิงข้ามช่วงเวลาที่อาจมี sync คั่น เพราะ index ของมื้อยาเปลี่ยนได้เมื่อตารางเปลี่ยน
 */
DoseRef scheduleFindByScheduleId(const char *scheduleId);
Dose *scheduleDoseAt(const DoseRef &ref);
const Slot *scheduleSlotOf(const DoseRef &ref);

/**
 * เดินสถานะของทุกมื้อยาตามเวลาปัจจุบัน และคืนมื้อที่ต้องเตือนอยู่ตอนนี้
 *
 * justBooted = true ในรอบแรกหลังได้เวลาจาก RTC ครั้งแรก ใช้ตัดสินว่ามื้อที่เลยมานาน
 * ควรถูกข้ามเงียบๆ แทนการปลุกย้อนหลัง
 */
DoseRef scheduleTick(int nowMinutes, uint32_t dayKey, bool justBooted);

/** มื้อถัดไปของวันนี้ที่ยังไม่ถึงเวลา (ใช้แสดงบน LCD) */
DoseRef scheduleNextUpcoming(int nowMinutes);

/** เปลี่ยนสถานะพร้อมบันทึกลง Serial เพื่อให้ไล่ปัญหาได้ */
void scheduleSetState(const DoseRef &ref, DoseState state);

const char *doseStateName(DoseState state);

/** true หนึ่งครั้งเมื่อข้ามไปวันใหม่ ใช้สั่ง sync ตารางของวันใหม่ทันที */
bool scheduleConsumeDayRollover();

/** true เมื่อได้รับตารางจาก server อย่างน้อยหนึ่งครั้งแล้ว */
bool scheduleHasData();
