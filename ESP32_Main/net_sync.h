#pragma once

#include <Arduino.h>

#include "config.h"
#include "event_queue.h"

/** คำสั่งจ่ายยาที่เว็บฝากไว้ให้เครื่องมารับ */
struct RemoteCommand {
  char id[40];
  char type[12];  // DISPENSE, BUZZ, CANCEL
  char scheduleId[40];
  uint8_t slot;
  float amount;
  unsigned long receivedAtMs; // Local receipt time; expire queued commands after 15 minutes.
};

void netSyncBegin();

/**
 * ดึงเวลา + ตารางยาของวันนี้ + คำสั่งค้าง จาก GET /api/device/sync
 * คืน true เมื่อได้ข้อมูลครบและ commit ลง schedule_store แล้ว
 */
bool netSyncFetch();

/** ส่งผลการจ่ายยาที่ค้างในคิวขึ้น server แล้วลบรายการที่ server ตอบรับออกจากคิว */
bool netSyncFlushEvents();

/** true เมื่อถึงรอบที่ควรเรียก netSyncFetch() (คุมจังหวะตาม next_poll_sec ของ server) */
bool netSyncDue();

/** สั่งให้ sync รอบถัดไปเกิดขึ้นทันที เช่น หลังขึ้นวันใหม่หรือผู้ใช้กดปุ่ม */
void netSyncRequestNow();

/** เวลาที่ server ส่งมาล่าสุด (epoch ท้องถิ่น) 0 = ยังไม่เคย sync สำเร็จ */
uint32_t netSyncLastServerEpoch();
int netSyncTimezoneOffsetMinutes();

/** ดึงคำสั่งที่รออยู่ออกมาทีละรายการ คืน false เมื่อคิวคำสั่งว่าง */
bool netSyncTakeCommand(RemoteCommand &command);

/** สถิติสำหรับหน้าเว็บสถานะและ Serial */
bool netSyncLastCallOk();
unsigned long netSyncLastOkMs();
const char *netSyncLastError();
const char *netSyncConfigVersion();

/** Complete a logged-in setup session using the hardware API key. Returns HTTP status or 0 while waiting for TLS. */
int netSyncCompleteSetup(const char *token);
