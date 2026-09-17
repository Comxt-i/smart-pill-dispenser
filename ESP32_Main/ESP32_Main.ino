#include <Wire.h>

#include "config.h"
#include "pill_app.h"

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("Smart Pill Dispenser firmware %s\n", FIRMWARE_VERSION);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  // ตรรกะทั้งหมดอยู่ใน pill_app เพื่อให้แต่ละโมดูลเรียกหากันได้โดยไม่ผ่านไฟล์ .ino
  appBegin();
}

void loop()
{
  appLoop();
}
