#pragma once

// คัดลอกไฟล์นี้เป็น secrets.h ในโฟลเดอร์เดียวกัน แล้วแก้ค่าให้ตรงกับระบบของตนเอง
// (secrets.h ถูก .gitignore ไว้แล้ว จะไม่ถูก commit ขึ้น git)

#define WIFI_SSID "iPhone"
#define WIFI_PASSWORD "12345678"

// ที่อยู่ backend โดยไม่ต้องมี / ปิดท้าย
//
// ทดสอบในวง LAN: ใช้ IP ของเครื่องที่รัน backend (ห้ามใช้ localhost)
//   #define SERVER_BASE_URL "http://192.168.1.100:3000"
//
// ทดสอบกับ server ที่ deploy แล้ว:
//   #define SERVER_BASE_URL "https://pillbox.example.com"
#define SERVER_BASE_URL "https://pillbox.34-46-62-13.sslip.io" 

// API Key จากตาราง Device ในฐานข้อมูล
// ค่านี้คือค่าที่ seed ตอน dev ให้มา ถ้า deploy แบบ production จะถูกสุ่มใหม่
#define DEVICE_API_KEY "esp32_sec_key_246f28abc199"
