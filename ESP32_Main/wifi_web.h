#pragma once

#include <Arduino.h>

/** เริ่มเชื่อมต่อ Wi-Fi แบบไม่ block และเปิดหน้าเว็บสถานะในเครื่อง */
void wifiWebBegin();

/** Read and latch the 3-second boot gesture before initializing optional peripherals. */
void wifiCheckSetupButtonAtBoot();

/** ให้บริการหน้าเว็บและดูแลการเชื่อมต่อใหม่เมื่อ Wi-Fi หลุด */
void wifiWebLoop();

bool wifiIsConnected();

/** True while physical setup is active; normal sync/dispensing must wait. */
bool wifiSetupActive();

/** Enter setup after an authorized physical gesture; caller must check mechanism is idle. */
bool wifiStartSetup();

/**
 * ชื่อและรหัสผ่านของ Wi-Fi ที่เครื่องปล่อยเองในโหมดตั้งค่า
 *
 * มีค่าเฉพาะขณะ wifiSetupActive() เป็น true (ก่อนหน้านั้นคืนสตริงว่าง)
 * ใช้เอาไปแสดงบนจอ เพื่อให้ผู้ใช้จริงอ่านได้โดยไม่ต้องต่อคอมดู Serial
 *
 * ตัวชี้ที่คืนมาชี้ไปยัง buffer ภายในที่อยู่ตลอดอายุโปรแกรม จึงเก็บไว้ใช้ข้ามรอบ loop ได้
 */
const char *wifiSetupSsid();
const char *wifiSetupPassword();
