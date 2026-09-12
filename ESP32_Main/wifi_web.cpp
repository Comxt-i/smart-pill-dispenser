#include "wifi_web.h"
#include "secrets.h"

#include <WebServer.h>
#include <WiFi.h>

namespace {
WebServer server(80);
bool requestPending = false;
uint8_t requestedDispenser = 1;
uint8_t requestedAmount = 1;

void handleHome()
{
  const char page[] =
    "<!doctype html><html><body>"
    "<h1>Smart Pill Dispenser</h1>"
    "<form action='/dispense'>"
    "Medicine dispenser <input name='dispenser' type='number' min='1' max='3' value='1'><br>"
    "Amount <input name='amount' type='number' min='1' max='9' value='1'><br>"
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

  requestedDispenser = static_cast<uint8_t>(dispenser);
  requestedAmount = static_cast<uint8_t>(amount);
  requestPending = true;
  server.send(200, "text/plain", "Dispense request queued");
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

bool takeDispenseRequest(uint8_t &dispenser, uint8_t &amount)
{
  if (!requestPending)
    return false;

  dispenser = requestedDispenser;
  amount = requestedAmount;
  requestPending = false;
  return true;
}
