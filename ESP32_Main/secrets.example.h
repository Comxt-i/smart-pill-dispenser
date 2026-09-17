#pragma once

// คัดลอกไฟล์นี้เป็น secrets.h แล้วแก้ค่าให้ตรงกับระบบของตนเอง (secrets.h ถูก .gitignore ไว้)

#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// URL ของ backend โดยไม่ต้องมี / ปิดท้าย รองรับทั้ง http และ https
//
// แบบที่ 1 - deploy ขึ้น VPS แล้ว (แนะนำ): ใช้โดเมนจริงผ่าน https
//   #define SERVER_BASE_URL "https://pillbox.example.com"
//   ใบรับรองจะถูกตรวจด้วย ISRG Root X1 ใน certs.h (ปรับได้ที่ TLS_VERIFY_CERTIFICATE)
//
// แบบที่ 2 - รัน backend ในวง LAN: ใช้ IP ของเครื่องที่รัน backend
//   ห้ามใช้ localhost เพราะ localhost ของ ESP32 คือตัวมันเอง และ backend ต้อง listen ที่ 0.0.0.0
#define SERVER_BASE_URL "https://pillbox.example.com"

// API Key ของอุปกรณ์จากตาราง Device ในฐานข้อมูล
// ค่าที่ seed ไว้ให้ทดสอบคือ esp32_sec_key_246f28abc199
#define DEVICE_API_KEY "esp32_sec_key_246f28abc199"

// ปล่อยว่างไว้เพื่อใช้ MAC address จริงของบอร์ด
// กำหนดค่าเองได้เมื่อต้องการให้ตรงกับ mac_address ที่ seed ไว้ (24:6F:28:AB:C1:99)
#define DEVICE_MAC_OVERRIDE ""
