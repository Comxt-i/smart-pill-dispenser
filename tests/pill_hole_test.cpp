#include <cassert>
#include "../ESP32_Main/pill_hole.h"

int main()
{
  // สี่ค่าที่เว็บให้เลือกได้จริง (backend ยอมรับเฉพาะ 8/13/15/25)
  assert(pillHoleFromMillimetres(8) == 0);   // ทรงกลม ไม่เกิน 8 mm
  assert(pillHoleFromMillimetres(13) == 1);  // ทรงกลม ไม่เกิน 13 mm
  assert(pillHoleFromMillimetres(15) == 2);  // ทรงกลม ไม่เกิน 15 mm
  assert(pillHoleFromMillimetres(25) == 3);  // ทรงรี/แคปซูล ไม่เกิน 25 mm

  // ไม่ระบุ: เว็บส่ง null ซึ่งเครื่องอ่านได้เป็น 0
  assert(pillHoleFromMillimetres(0) == PILL_HOLE_UNSPECIFIED);
  assert(pillHoleFromMillimetres(-3) == PILL_HOLE_UNSPECIFIED);

  // ขอบของแต่ละช่อง: ช่องที่เล็กที่สุดที่เม็ดผ่านได้
  assert(pillHoleFromMillimetres(1) == 0);
  assert(pillHoleFromMillimetres(9) == 1);
  assert(pillHoleFromMillimetres(14) == 2);
  assert(pillHoleFromMillimetres(16) == 3);
  assert(pillHoleFromMillimetres(40) == 3);  // ใหญ่เกินทุกช่อง: ช่องใหญ่สุดคือทางเดียว
  return 0;
}
