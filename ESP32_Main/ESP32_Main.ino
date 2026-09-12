#include <Wire.h>

#include "config.h"
#include "nano_control.h"
#include "rtc_lcd.h"
#include "wifi_web.h"

void setup()
{
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(CONFIRM_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DISPENSE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(CANCEL_BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  rtcLcdBegin();
  nanoControlBegin();
  wifiWebBegin();
}

void loop()
{
  wifiWebLoop();
  rtcLcdUpdate();

  uint8_t dispenser;
  uint8_t amount;

  if (takeDispenseRequest(dispenser, amount))
  {
    bool sent = dispenseMedicine(dispenser, amount);
    Serial.println(sent ? "Command sent" : "Command failed");
  }

  if (digitalRead(CANCEL_BUTTON_PIN) == LOW)
  {
    stopDispenser();
    delay(250);
  }
}
