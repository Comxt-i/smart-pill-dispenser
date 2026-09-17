#pragma once

#include <Arduino.h>

/** เริ่มเชื่อมต่อ Wi-Fi แบบไม่ block และเปิดหน้าเว็บสถานะในเครื่อง */
void wifiWebBegin();

/** ให้บริการหน้าเว็บและดูแลการเชื่อมต่อใหม่เมื่อ Wi-Fi หลุด */
void wifiWebLoop();

bool wifiIsConnected();
