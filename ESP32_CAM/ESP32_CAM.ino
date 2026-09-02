#include <WiFi.h>

#include "secrets.h"

void setup()
{
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("ESP32-CAM connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print('.');
  }

  Serial.println();
  Serial.print("ESP32-CAM IP: ");
  Serial.println(WiFi.localIP());

  // TODO: add the CameraWebServer example for the exact ESP32-CAM model.
  // TODO: add pill-box pickup detection after the camera stream works.
}

void loop()
{
}

