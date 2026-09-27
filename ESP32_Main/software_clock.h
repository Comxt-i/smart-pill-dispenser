#pragma once
#include <stdint.h>

/**
 * เวลาจริงไม่มีทางเดินถอยหลัง
 *
 * RTC ที่บอกเวลาก่อน "เวลาล่าสุดที่รู้แน่ว่าถูก" เกินค่าเผื่อ = นาฬิกาหยุดเดินตอนไม่มีไฟ
 * (ถ่านสำรองหมด หรือโมดูล Tiny RTC ใส่ CR2032 กับวงจรชาร์จ) ห้ามเชื่อเวลานั้น
 * trustedFloor = 0 คือเครื่องใหม่ที่ยังไม่เคยรู้เวลาที่ถูก จึงยังตรวจไม่ได้
 */
inline bool rtcTimeIsPlausible(uint32_t rtcEpoch, uint32_t trustedFloor, uint32_t toleranceSec)
{
  return trustedFloor == 0 || rtcEpoch + toleranceSec >= trustedFloor;
}

// A volatile local-time clock. Reboot requires a new server sync or a valid RTC.
class SoftwareClock {
 public:
  static constexpr uint32_t MIN_EPOCH = 1704067200UL; // 2024-01-01
  static constexpr uint32_t MAX_EPOCH = 4102444800UL; // 2100-01-01
  static bool validEpoch(uint32_t value) { return value >= MIN_EPOCH && value < MAX_EPOCH; }
  void sync(uint32_t localEpoch, uint32_t nowMs) {
    if (!validEpoch(localEpoch)) return;
    epoch = localEpoch;
    lastMs = nowMs;
    remainder = 0;
  }
  uint32_t localEpoch(uint32_t nowMs) {
    if (!epoch) return 0;
    const uint64_t elapsed = uint64_t(uint32_t(nowMs - lastMs)) + remainder;
    lastMs = nowMs;
    epoch += elapsed / 1000;
    remainder = elapsed % 1000;
    return epoch;
  }
 private:
  uint32_t epoch = 0, lastMs = 0, remainder = 0;
};
