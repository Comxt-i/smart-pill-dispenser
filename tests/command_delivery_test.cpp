#include <cassert>
#include "../ESP32_Main/net_sync.cpp"
#include "../ESP32_Main/command_journal.cpp"
SerialClass Serial;
WiFiClass WiFi;
unsigned long testNow = 1000;
unsigned long millis() { return testNow; }
uint32_t rtcLocalEpoch() { return 1800000000; }
int main() {
  netSyncBegin();
  RemoteCommand incoming = {}, delivered = {};
  strcpy(incoming.id, "same-command"); strcpy(incoming.type, "DISPENSE");
  incoming.slot = 1; incoming.amount = 1; incoming.receivedAtMs = testNow;
  pushCommand(incoming); pushCommand(incoming);
  assert(commandCount == 1);
  assert(netSyncTakeCommand(delivered));
  assert(commandJournalContains(incoming.id)); // Durable before caller starts motion.
  pushCommand(incoming);
  assert(commandCount == 0 && !netSyncTakeCommand(delivered));
  netSyncBegin(); // Reload journal after simulated reboot.
  pushCommand(incoming);
  assert(commandCount == 0);
  strcpy(incoming.id, "storage-failure");
  pushCommand(incoming); commandStorage.writeOk = false;
  assert(!netSyncTakeCommand(delivered));
  assert(!commandJournalContains(incoming.id));
  testNow += 15UL * 60UL * 1000UL;
  commandStorage.writeOk = true;
  assert(!netSyncTakeCommand(delivered) && commandCount == 0); // Expired while blocked.
  strcpy(incoming.id, "fresh-command"); incoming.receivedAtMs = testNow;
  pushCommand(incoming);
  assert(netSyncTakeCommand(delivered));
}
