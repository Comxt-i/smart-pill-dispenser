// ทดสอบการเลื่อนข้อความบนจอด้วยเวลาจำลอง โดยไม่ต้องใช้จอจริง
#include "config.h"
#include "marquee.h"

#include <cassert>
#include <cstring>
#include <set>
#include <string>

unsigned long fakeMillis = 0;
int cancelLevel = HIGH;
FakeSerial Serial;

namespace {

constexpr uint8_t W = 16;  // ความกว้างที่ใช้ทดสอบ

std::string render(Marquee &marquee, unsigned long nowMs)
{
  char out[LCD_MAX_COLS + 1] = {0};
  marqueeRender(marquee, nowMs, out);
  return std::string(out);
}

void testShortTextStaysStill()
{
  Marquee m = {};
  marqueeSet(m, "Paracetamol", W);
  assert(!marqueeScrolls(m));

  // สั้นกว่าจอ: ต้องอยู่นิ่งและถูกเติมช่องว่างให้เต็ม 16 เพื่อลบข้อความเดิม
  const std::string first = render(m, 0);
  assert(first == "Paracetamol     ");
  assert(first.size() == W);

  // เวลาผ่านไปนานแค่ไหนก็ไม่เลื่อน
  assert(render(m, 100000) == first);
}

void testExactWidthDoesNotScroll()
{
  Marquee m = {};
  marqueeSet(m, "1234567890123456", W);  // 16 ตัวพอดี
  assert(m.length == W);
  assert(!marqueeScrolls(m));
  assert(render(m, 50000) == "1234567890123456");
}

void testLongTextScrollsAndWraps()
{
  Marquee m = {};
  const char *text = "Paracetamol, Amoxicillin, Ibuprofen";
  marqueeSet(m, text, W);
  assert(marqueeScrolls(m));

  // เฟรมแรกต้องเริ่มที่ต้นข้อความเสมอ ผู้ใช้จะได้อ่านชื่อแรกทัน
  assert(render(m, 0) == std::string(text).substr(0, W));

  // ระหว่างหยุดพัก ภาพต้องไม่ขยับ
  assert(render(m, LCD_MARQUEE_HOLD_MS - 1) == std::string(text).substr(0, W));

  // พ้นเวลาพักแล้วต้องเลื่อนไปหนึ่งตัวอักษร
  unsigned long now = LCD_MARQUEE_HOLD_MS;
  assert(render(m, now) == std::string(text).substr(1, W));

  // เลื่อนต่อทีละตัวตามจังหวะ STEP
  now += LCD_MARQUEE_STEP_MS;
  assert(render(m, now) == std::string(text).substr(2, W));

  // เลื่อนจนครบรอบต้องกลับมาที่ต้นข้อความ ไม่ค้างและไม่ข้าม
  const uint8_t cycle = static_cast<uint8_t>(strlen(text) + 3);
  bool returnedToStart = false;
  for (uint8_t step = 0; step < cycle * 2; ++step)
  {
    now += LCD_MARQUEE_HOLD_MS;  // เผื่อกรณีหยุดพัก ใช้ช่วงยาวสุดเสมอ
    render(m, now);
    if (m.offset == 0)
    {
      returnedToStart = true;
      break;
    }
  }
  assert(returnedToStart);
  assert(render(m, now) == std::string(text).substr(0, W));
}

void testEveryCharacterBecomesVisible()
{
  Marquee m = {};
  const char *text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  marqueeSet(m, text, W);

  // ไล่ทั้งรอบแล้วต้องเห็นครบทุกตัวอักษร ไม่มีตัวไหนถูกข้าม
  std::set<char> seen;
  unsigned long now = 0;
  const uint8_t cycle = static_cast<uint8_t>(strlen(text) + 3);
  for (uint8_t step = 0; step <= cycle; ++step)
  {
    for (char c : render(m, now))
      if (c != ' ')
        seen.insert(c);
    now += LCD_MARQUEE_HOLD_MS;
  }

  for (const char *p = text; *p; ++p)
    assert(seen.count(*p) == 1);
}

void testRenderAlwaysFillsWidth()
{
  Marquee m = {};
  marqueeSet(m, "Amoxicillin 500mg twice daily", W);

  // ทุกเฟรมต้องกว้าง 16 พอดี ไม่งั้นจะมีเศษข้อความเดิมค้างบนจอ
  unsigned long now = 0;
  for (int i = 0; i < 40; ++i)
  {
    assert(render(m, now).size() == W);
    now += LCD_MARQUEE_STEP_MS;
  }
}

void testSameTextDoesNotRestartScroll()
{
  Marquee m = {};
  marqueeSet(m, "Paracetamol, Amoxicillin, Ibuprofen", W);

  // ตัวจับเวลาเริ่มนับตอน render ครั้งแรก ไม่ใช่ตอน marqueeSet
  // จึงต้อง render หนึ่งครั้งก่อน แล้วค่อยเดินเวลาให้พ้นช่วงหยุดพัก
  unsigned long now = 0;
  render(m, now);

  now += LCD_MARQUEE_HOLD_MS;
  render(m, now);
  now += LCD_MARQUEE_STEP_MS;
  render(m, now);

  const uint8_t offsetBefore = m.offset;
  assert(offsetBefore == 2);

  // loop เรียก marqueeSet ด้วยข้อความเดิมทุกรอบ ต้องไม่ทำให้ภาพกระตุกกลับไปที่ต้น
  marqueeSet(m, "Paracetamol, Amoxicillin, Ibuprofen", W);
  assert(m.offset == offsetBefore);

  // แต่ถ้าเปลี่ยนข้อความจริง ต้องเริ่มใหม่จากต้น
  marqueeSet(m, "Ibuprofen 400mg after meal", W);
  assert(m.offset == 0);
}

void testEdgeCases()
{
  Marquee m = {};

  marqueeSet(m, "", W);
  assert(!marqueeScrolls(m));
  assert(render(m, 0) == std::string(W, ' '));

  marqueeSet(m, nullptr, W);
  assert(m.length == 0);

  // ข้อความยาวเกิน buffer ต้องถูกตัดโดยไม่ล้นหน่วยความจำ
  const std::string huge(LCD_MARQUEE_MAX_TEXT * 2, 'X');
  marqueeSet(m, huge.c_str(), W);
  assert(m.length == LCD_MARQUEE_MAX_TEXT - 1);
  assert(render(m, 0).size() == W);
}

}  // namespace

int main()
{
  testShortTextStaysStill();
  testExactWidthDoesNotScroll();
  testLongTextScrollsAndWraps();
  testEveryCharacterBecomesVisible();
  testRenderAlwaysFillsWidth();
  testSameTextDoesNotRestartScroll();
  testEdgeCases();
  return 0;
}
