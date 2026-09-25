#include <ESP32Servo.h>
#include <Wire.h>

#include "config.h"
#include "dispenser_control.h"
#include "pill_app.h"
#include "rtc_lcd.h"
#include "wifi_web.h"

void setup()
{
  // ***ต้องเป็นคำสั่งแรกสุด ห้ามมีอะไรมาก่อน***
  //
  // ขา DIR ของจาน 3 อยู่บน GPIO14 ซึ่งชิปปล่อยสัญญาณออกมาเองตอนบูต
  // ทำให้ DRV8833 เห็น IN2=HIGH แล้วปั่นมอเตอร์เต็มกำลังตั้งแต่ก่อนโค้ดเราจะรัน
  // ถ้าปล่อยไว้จนถึง appBegin() มอเตอร์จะสั่นค้างเป็นวินาทีทุกครั้งที่เปิดเครื่อง
  dispenserSafePinsEarly();

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("Smart Pill Dispenser firmware %s\n", FIRMWARE_VERSION);

  wifiCheckSetupButtonAtBoot();

  // ESP32Servo กับ analogWrite ใช้ LEDC ร่วมกัน ถ้าไม่จองไว้ก่อนจะแย่ง timer กันเอง
  // แล้วอย่างใดอย่างหนึ่งเงียบไปโดยไม่มีอะไรฟ้อง (servo ไม่ขยับ หรือมอเตอร์สั่นไม่ทำงาน)
  //
  // จองแค่ 2 ตัวจาก 4 เพราะ servo ทำงานพร้อมกันได้สูงสุด MAX_CONCURRENT_DISPENSERS
  // ที่เหลือต้องกันไว้ให้ analogWrite ของมอเตอร์สั่น ถ้าจองครบ 4 มอเตอร์จะไม่หมุนเลย
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);

  // ตรวจสภาพเส้นสัญญาณก่อนเปิดใช้บัส ต้องทำก่อน Wire.begin() เท่านั้น
  // ผลจะบอกได้ว่า "ไม่เจออุปกรณ์" เกิดจากไม่มี pull-up หรือสายลัด
  i2cCheckLines();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  // รายงานว่าจอและ RTC ต่อครบไหม ก่อนที่จะไปเริ่มระบบ
  // ถ้าไม่ครบจะเห็นใน Serial ทันที ไม่ต้องมานั่งเดาว่าทำไมจอดับ
  i2cScanAndReport();

  // ตรรกะทั้งหมดอยู่ใน pill_app เพื่อให้แต่ละโมดูลเรียกหากันได้โดยไม่ผ่านไฟล์ .ino
  appBegin();
}

void loop()
{
  appLoop();
}
