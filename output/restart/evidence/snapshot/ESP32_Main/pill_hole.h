#pragma once

#include <stdint.h>

// แปลงขนาดเม็ดยาที่ผู้ใช้เลือกบนเว็บ (pill_size_mm) เป็นช่องปล่อยยาบนจาน
//
// เว็บให้เลือกได้ 4 ค่า ช่องเรียงจากเล็กไปใหญ่ ตรงกับคอลัมน์ของ HOLE_PULSE_US ใน config.h
//    8 -> ช่อง 0  ทรงกลม ไม่เกิน 8 mm
//   13 -> ช่อง 1  ทรงกลม ไม่เกิน 13 mm
//   15 -> ช่อง 2  ทรงกลม ไม่เกิน 15 mm
//   25 -> ช่อง 3  ทรงรี/แคปซูล ไม่เกิน 25 mm
//   ไม่ระบุ (null/0) -> -1 ไล่ลองจากช่องเล็กสุด

constexpr int8_t PILL_HOLE_UNSPECIFIED = -1;

/** ขนาดเป็นมิลลิเมตร -> ช่องที่เล็กที่สุดที่เม็ดขนาดนี้ผ่านได้ */
inline int8_t pillHoleFromMillimetres(long mm)
{
  if (mm <= 0) return PILL_HOLE_UNSPECIFIED;
  if (mm <= 8) return 0;
  if (mm <= 13) return 1;
  if (mm <= 15) return 2;
  // ใหญ่กว่า 25 mm ไม่มีช่องไหนพอดี ช่องใหญ่สุดคือทางเดียวที่มีโอกาส
  return 3;
}
