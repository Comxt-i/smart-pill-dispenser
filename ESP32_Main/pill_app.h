#pragma once

#include <Arduino.h>

/** ตรรกะหลักของกล่องยา: รวมตารางยา ปุ่มกด เสียงเตือน จอ และการคุยกับ server */
void appBegin();
void appLoop();

/** สรุปสถานะปัจจุบันเป็น JSON สำหรับหน้าเว็บในเครื่องและการไล่ปัญหา */
void appStatusJson(String &out);
