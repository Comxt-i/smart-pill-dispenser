#pragma once

#include <Arduino.h>

#include "config.h"

/** สถานะของมื้อยาหนึ่งมื้อภายในวันนี้ */
enum class DoseState : uint8_t {
  Pending,     // ยังไม่ถึงเวลา
  Alerting,    // ถึงเวลาแล้ว กำลังเตือนให้ผู้ใช้กดปุ่ม
  Queued,      // รับทั้งรอบแล้ว กำลังรอช่องว่าง
  Dispensing,  // ผู้ใช้กดปุ่มแล้ว จานกำลังหมุน
  Done,        // จ่ายสำเร็จ (หรือ server บอกว่ามื้อนี้จบไปแล้ว)
  Missed,      // เลยเวลาผ่อนผันโดยไม่มีใครกดปุ่ม
  Skipped,     // ผู้ใช้กด Cancel เพื่อข้ามมื้อนี้
  Failed,      // ต้องตรวจกลไก/จำนวนยาเองก่อนออกคำสั่งใหม่
  Snoozed,     // ผู้ใช้กดเลื่อน หยุดเตือนชั่วคราวจนถึงเวลาที่เลื่อนไป
};

struct Dose {
  char scheduleId[40];
  char label[20];
  int minutes;  // นาทีนับจากเที่ยงคืน
  DoseState state;
  bool failureReported;  // กันส่ง FAILED ซ้ำเมื่อผู้ใช้กดจ่ายแล้วพลาดหลายครั้ง
  int snoozedUntil;      // นาทีของวันที่จะกลับมาเตือนอีกครั้ง (-1 = ไม่ได้เลื่อน)
  uint8_t snoozeCount;   // เลื่อนไปแล้วกี่ครั้งในมื้อนี้
};

struct Slot {
  uint8_t number;  // 1..DISPENSER_COUNT
  bool active;
  char medicationId[40];
  char name[34];  // ชื่อสำหรับ LCD (server ตัดมาให้ไม่เกิน 32 ตัวอักษร ยาวเกินจอจะเลื่อนวน)
  float amountPerDose;
  // ช่องปล่อยยาตามขนาดที่ผู้ใช้กรอกบนเว็บ (0-3) หรือ PILL_HOLE_ANY ถ้าไม่ได้กรอก
  int8_t pillHole;
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

/** ตั้งช่องปล่อยยาของช่องยาที่เพิ่ง stage ค่านอกช่วง 0-3 ถือว่าไม่ได้ระบุ */
void scheduleStageSlotPillHole(int slotIndex, int8_t hole);

/** ช่องปล่อยยาของช่องยาหมายเลข `number` (1..DISPENSER_COUNT) หรือ PILL_HOLE_ANY */
int8_t scheduleSlotPillHole(uint8_t number);

/**
 * เดินสถานะของทุกมื้อยาตามเวลาปัจจุบัน และคืนมื้อที่ต้องเตือนอยู่ตอนนี้
 *
 * justBooted = true ในรอบแรกหลังได้เวลาจาก RTC ครั้งแรก ใช้ตัดสินว่ามื้อที่เลยมานาน
 * ควรถูกข้ามเงียบๆ แทนการปลุกย้อนหลัง
 */
DoseRef scheduleTick(int nowMinutes, uint32_t dayKey, bool justBooted);

/** มื้อถัดไปของวันนี้ที่ยังไม่ถึงเวลา (ใช้แสดงบน LCD) */
DoseRef scheduleNextUpcoming(int nowMinutes);

/**
 * เก็บมื้อยาทุกมื้อที่กำลังเตือนอยู่ตอนนี้ลง out แล้วคืนจำนวนที่เก็บได้
 *
 * รอบยาหนึ่งรอบมีได้หลายช่อง ผู้ใช้กดปุ่มครั้งเดียวแล้วจ่ายให้ครบทั้งรอบ
 * จึงต้องไล่ได้ทั้งหมด ไม่ใช่ได้ทีละมื้อแบบ scheduleTick()
 */
uint8_t scheduleAlertingDoses(DoseRef *out, uint8_t maxCount);

/** เปลี่ยนสถานะพร้อมบันทึกลง Serial เพื่อให้ไล่ปัญหาได้ */
void scheduleSetState(const DoseRef &ref, DoseState state);

/**
 * เลื่อนการเตือนของมื้อนี้ออกไป SNOOZE_MINUTES นาที
 *
 * คืน false เมื่อเลื่อนครบโควตาแล้ว หรือเลื่อนไปจะเลยเวลาผ่อนผัน
 * ซึ่งกันไม่ให้ผู้ใช้เลื่อนไปเรื่อยๆ จนกลายเป็นขาดยาโดยไม่รู้ตัว
 */
bool scheduleSnooze(const DoseRef &ref, int nowMinutes);

/**
 * ตรวจว่ามื้อนี้เลื่อนได้ไหม โดยไม่เปลี่ยนสถานะอะไร
 *
 * มีไว้ให้ตรวจทั้งรอบก่อนลงมือ เพราะการเลื่อนได้บางช่องแล้วทิ้งช่องอื่นไว้
 * จะทำให้รอบเดียวแตกออกเป็นสองเวลา
 */
bool scheduleCanSnooze(const DoseRef &ref, int nowMinutes);

/** จำนวนครั้งที่มื้อนี้ถูกเลื่อน ใช้แนบไปกับผลที่ส่งขึ้น server */
uint8_t scheduleSnoozeCount(const DoseRef &ref);

/** มื้อแรกที่กำลังถูกเลื่อนอยู่ ใช้แสดงบนจอว่าจะกลับมาเตือนตอนไหน */
DoseRef scheduleFirstSnoozed();

const char *doseStateName(DoseState state);

/** true เมื่อมื้อนี้ยังรอให้ผู้ใช้จัดการอยู่ (ยังไม่จ่าย ไม่ข้าม และยังไม่หมดเวลา) */
bool doseIsOpen(const Dose &dose);

/**
 * เวลาที่มื้อนี้จะเรียกร้องความสนใจจริง โดยคิดการเลื่อนเข้าไปด้วย
 *
 * มื้อที่ถูกเลื่อนไว้จะใช้เวลาที่เลื่อนไป ไม่ใช่เวลาตามตาราง
 * ถ้าใช้เวลาตามตารางอย่างเดียว มื้อที่เลื่อนไปทับรอบถัดไปจะถูกจัดลำดับผิด
 * แล้วหายไปจากจอทั้งที่กำลังเตือนอยู่
 */
int doseEffectiveMinutes(const Dose &dose);

/** true หนึ่งครั้งเมื่อข้ามไปวันใหม่ ใช้สั่ง sync ตารางของวันใหม่ทันที */
bool scheduleConsumeDayRollover();

/**
 * วันที่ที่สถานะมื้อยาในตารางตอนนี้เป็นของ (0 = ยังไม่เคย tick)
 * ใช้แทนวันที่ของนาฬิกาตอนบันทึกว่ามื้อจบแล้ว: ช่วงข้ามเที่ยงคืน นาฬิกาเป็นวันใหม่แล้ว
 * แต่สถานะที่เพิ่งจบยังเป็นของเมื่อวาน ถ้าใช้วันของนาฬิกา มื้อของวันนี้จะถูกนับว่าจบไปแล้วและไม่เตือน
 */
uint32_t scheduleDayKey();

/** true เมื่อได้รับตารางจาก server อย่างน้อยหนึ่งครั้งแล้ว */
bool scheduleHasData();
