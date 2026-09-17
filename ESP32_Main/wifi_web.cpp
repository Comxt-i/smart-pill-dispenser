#include "wifi_web.h"
#include "dispenser_control.h"
#include "secrets.h"

#include <WebServer.h>
#include <WiFi.h>

namespace {
WebServer server(80);

void handleHome()
{
  const char page[] =
    "<!doctype html><html><body>"
    "<h1>Smart Pill Dispenser</h1>"
    "<form action='/dispense'>"
    "Medicine dispenser <input name='dispenser' type='number' min='1' max='3' value='1'><br>"
    "Cycles (pill count unverified) <input name='amount' type='number' min='1' max='9' value='1'><br>"
    "<button type='submit'>Dispense</button>"
    "</form></body></html>";

  server.send(200, "text/html", page);
}

void handleDispense()
{
  int dispenser = server.arg("dispenser").toInt();
  int amount = server.arg("amount").toInt();

  if (dispenser < 1 || dispenser > 3 || amount < 1 || amount > 9)
  {
    server.send(400, "text/plain", "Invalid dispenser or amount");
    return;
  }

  const DispenseResult result = dispenseMedicine(static_cast<uint8_t>(dispenser),
                                                static_cast<uint8_t>(amount));
  switch (result)
  {
    case DispenseResult::Started:
      server.send(202, "text/plain", "Servo cycles started; pill count not verified");
      break;
    case DispenseResult::Invalid:
      server.send(400, "text/plain", "Invalid dispenser or amount");
      break;
    case DispenseResult::Disabled:
      server.send(503, "text/plain", "Servo movement disabled: calibrate and enable in config.h");
      break;
    case DispenseResult::Busy:
      server.send(409, "text/plain", "Dispenser busy; request rejected");
      break;
    case DispenseResult::Cancelled:
      server.send(409, "text/plain", "Cancel button is pressed; request rejected");
      break;
    case DispenseResult::ServoError:
      server.send(503, "text/plain", "Unable to attach servo");
      break;
  }
}
}

void wifiWebBegin()
{
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print('.');
  }

  Serial.println();
  Serial.print("Open http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleHome);
  server.on("/dispense", handleDispense);
  server.begin();
}

void wifiWebLoop()
{
  server.handleClient();
}
