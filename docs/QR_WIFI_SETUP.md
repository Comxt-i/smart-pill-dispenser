# QR → Login → Wi-Fi setup

This feature spans `embedded_project` (website/backend) and `smart-pill-dispenser` (ESP32 firmware). Deploy the web/backend and flash the updated firmware together. The cloud website cannot directly reach a box that has no internet; the phone must switch to the box's temporary Wi-Fi once.

## What the user does

1. Scan the printed QR. It opens `https://YOUR-DOMAIN/setup/AA:BB:CC:DD:EE:FF` using the box’s registered station MAC. Old `/setup/DEVICE_ID` links still work.
2. Register an account or log in while the phone still has internet. The website returns to that exact box. Click **สร้างรหัสตั้งค่า** to get a code valid for 15 minutes.
3. Copy the code. It tells the ESP32 which logged-in account to pair with; it is not a Wi-Fi password. Paste it into the box’s local portal in step 5. Keep this tab open.
4. Connect the phone to `SmartPill_Setup_XXXXXX`, using the setup-network password printed on the label. Select “stay connected” if the phone warns that the network has no internet.
5. Open the captive portal, or type `http://192.168.4.1`. Select a 2.4 GHz home SSID, enter its password, paste the setup code and submit.
6. The ESP32 connects to home Wi-Fi, verifies the code with the backend using its own hardware API key, then persists the credentials in its NVS. Invalid Wi-Fi or setup codes leave the portal available for correction.
7. After success the box closes its setup Wi-Fi after ten seconds. Reconnect the phone to home Wi-Fi/mobile data and return to the website. The website waits for a new hardware sync before showing completion.

Home Wi-Fi SSID/password travel only from the phone's local portal to the ESP32, never to the cloud API, QR URL, browser localStorage, or backend database. The AP is WPA-protected; its password is a device-specific HMAC derivation distinct from the hardware API key. The QR contains only a website URL and public device identifier.

## สร้าง QR จากเครื่องมือภายนอก

ใส่ลิงก์รูปแบบนี้ลงในเครื่องมือสร้าง QR ที่เลือก:

```text
https://YOUR-DOMAIN/setup/AA:BB:CC:DD:EE:FF
```

แทน `YOUR-DOMAIN` ด้วยโดเมนเว็บจริง และแทน MAC ด้วย **station MAC** ของ ESP32 เครื่องนั้น (ตัวที่ลงทะเบียนกับ backend ไม่ใช่ SoftAP MAC) ใช้ตัวพิมพ์เล็กหรือใหญ่ได้ โดยต้องมีเครื่องหมาย `:` คั่นครบ 6 คู่

หน้า **ฮาร์ดแวร์ ESP32 → พิมพ์ QR → คัดลอกลิงก์สร้าง QR** เตรียมลิงก์นี้ให้ได้เช่นกัน QR มีเฉพาะ URL และ MAC ไม่ต้องใส่รหัสผ่าน Wi-Fi บ้าน, API key หรือรหัสตั้งค่าชั่วคราวลงใน QR

กล่องต้องลงทะเบียนในระบบและติดตั้ง API key เฉพาะเครื่องก่อน QR ไม่ได้ลงทะเบียนฮาร์ดแวร์ให้อัตโนมัติ เมื่อสแกนโดยยังไม่ login จะมีให้สมัครสมาชิกหรือเข้าสู่ระบบ แล้วกลับมาที่กล่องเดิม

พิมพ์ชื่อ Wi-Fi Setup และรหัสผ่าน Setup จากหน้าฉลากลงบนสติกเกอร์ด้วย เพื่อให้มือถือเข้าเครือข่ายของกล่องได้ หลัง login และคัดลอกรหัสตั้งค่าแล้วจึงเปลี่ยนไปเชื่อม Wi-Fi ของ ESP32

## First-time preparation by the device administrator

A permanent hardware identity is still required once per physical board. This step is for preparation, not for a user changing home Wi-Fi.

1. Register the real station MAC and name via `POST /api/devices` using an ADMIN JWT. The API creates an unpaired device and returns its unique `api_key`. Duplicate MACs are rejected to prevent overwriting another device. Existing devices keep their current keys.
2. Set `SERVER_BASE_URL` and this device's `DEVICE_API_KEY` in `ESP32_Main/secrets.h`, then flash the firmware. Keep servo movement disabled until separately calibrated, as before. Wi-Fi credentials no longer need to be compiled into that file.
3. Log into the real hosted website as ADMIN, open Hardware ESP32 and click **พิมพ์ QR** on the device card. The label prints a QR, setup SSID/password and setup-button instructions. Generate the label on the final public HTTPS domain, not localhost.
4. Print at 100%, without cropping the white border around the QR; test a physical scan before attaching it to the box. The application generates QR locally, with no external QR service.

Normal accounts can print labels for their paired devices; only ADMIN can print an unpaired label. A box already paired to another account cannot be claimed through a copied QR. Physical possession of the AP password does not replace login authorization.

## Changing Wi-Fi and unpairing

- On first boot without saved credentials, the portal starts automatically.
- To change Wi-Fi later, hold **CONFIRM while powering on for about 3 seconds**, then follow the website setup flow again. CANCEL retains its existing emergency-stop function.
- A temporary router outage only triggers reconnect attempts; it does not automatically erase credentials or open a setup network.
- **ยกเลิกการจับคู่** releases the account binding, invalidates setup sessions, clears and disables slots, detaches medications from the device and expires queued server commands in a transaction. Medication records/history stay with the previous account; stored Wi-Fi credentials remain on the box. Firmware receives server changes on its next sync. A powered-off/disconnected box cannot be stopped immediately by a cloud button.
- After re-pairing, choose medications belonging to the new account and explicitly re-enable appropriate slots. Remove the previous user’s physical pills before loading new medication.
- New accounts are always PATIENT accounts, regardless of a submitted role. Medication, schedule, intake-history and command queries are scoped to the current account. Administrators can additionally access legacy medications with no owner; saving one assigns it to that administrator. Legacy medications already attached to an owned device inherit its owner during migration. A QR or MAC alone cannot take over a paired box.

## Deployment

- Migrations: `backend/prisma/migrations/202609200001_device_setup/migration.sql` and `backend/prisma/migrations/202609210001_account_commands/migration.sql`. The second migration adds command ownership and backfills existing ownership without deleting medical history.
- Run `prisma migrate deploy` before starting the new backend; the Docker entrypoint already does this.
- Rebuild both web and backend images, then update the ESP32 firmware. Do not just copy new frontend assets onto an old backend.
- Existing devices with `user_id = null` must complete pairing. Until then their sync slots are inactive and pending commands are withheld.
- No migrations or secrets are applied to production by the local implementation/test commands.

## API contract

All `/api/devices/...` setup routes require a user JWT. Setup info, label, issue and session-status routes accept either a Device ID or colon-separated MAC in `:id`; tokens always store the canonical Device ID. Unpair uses the Device ID returned by the API:

| Route | Purpose |
| --- | --- |
| `GET /api/devices/:id/setup` | Check availability; return box name, MAC, paired state and AP SSID |
| `GET /api/devices/:id/setup-label` | Owner/admin-only printable label metadata, no hardware API key |
| `POST /api/devices/:id/setup-session` | Issue `{token, session_id, expires_at}`; token is stored as SHA-256 hash |
| `GET /api/devices/:id/setup-session/:sessionId` | Return completion only to the issuing account |
| `DELETE /api/devices/:id/pairing` | Release the caller's own device and disable its server-side operation |

`POST /api/device/setup/complete` requires the **hardware X-API-Key**, and body `{ "token": "20_HEX_CHARACTERS" }`. The token is bound to the authenticated device and user, expires in 15 minutes and cannot take over another owner. Retries after a lost response are idempotent while the session/pairing is valid. Unpairing revokes even previously consumed sessions.

## Validation

Backend tests: `node --test test/device-setup.cjs test/account-scope.cjs test/medication-dose.cjs test/medication-expiry.cjs` from `backend`.
HTTP authentication/DTO smoke test: build backend, then `node test/deployment-smoke.cjs` (uses a mocked database on localhost).
Frontend: `npm run build`. Browser checks on the production build cover MAC QR registration/login return, setup code display, completion polling, label SVG/print view, wrong-owner rejection and unpairing, with mocked APIs. Account tests cover cross-account reads/writes, legacy data, command ownership and attempts to register as ADMIN.
Firmware: `python3 -m unittest discover -s tests -p 'test_*.py'`. Includes real portal control flow with mocked Wi-Fi/NVS/backend responses: invalid inputs, wrong nonce, association timeout, rejected claim, NVS failure, successful persistence, AP close and reboot.

Real ESP32 compilation also passed with Arduino ESP32 core 3.3.12 (`esp32:esp32:esp32`): 1,148,555 bytes flash (87%) and 58,172 bytes static RAM (17%). This compile used placeholder device credentials for validation; configure the real per-device credentials before flashing.

Physical captive-portal behavior on iOS/Android, router compatibility, reboot persistence on flash, and QR scanning from the final printed sticker still need a real ESP32 and phone. No hardware was flashed by these checks.
