#include <Wire.h>

#include "config.h"
#include "pill_app.h"
#include "rtc_lcd.h"
#include "wifi_web.h"

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("Smart Pill Dispenser firmware %s\n", FIRMWARE_VERSION);

  Serial.println(ENABLE_SERVO_MOVEMENT ? "Mode: HARDWARE (motion enabled)" : "Mode: SIMULATION (no motion)");

  wifiCheckSetupButtonAtBoot();

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
