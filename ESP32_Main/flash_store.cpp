#include "flash_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <stdio.h>
#include <string.h>

namespace {
bool mounted = false;
}

bool flashStoreBegin()
{
  // true = เมานต์ไม่ได้ให้ฟอร์แมตใหม่ (เครื่องใหม่ หรือพาร์ทิชันเคยเป็น SPIFFS ของโปรแกรมอื่น)
  mounted = LittleFS.begin(true);
  if (mounted)
    Serial.println("[flash] พาร์ทิชันไฟล์พร้อม เก็บตารางยาและผลการจ่ายยาที่ยังไม่ได้ส่งได้หลายสัปดาห์");
  else
    Serial.println("[flash] ไม่มีพาร์ทิชันไฟล์ ใช้ NVS แทน (เก็บผลที่ยังไม่ได้ส่งได้น้อยกว่า)");
  return mounted;
}

bool flashStoreReady()
{
  return mounted;
}

bool flashStoreWrite(const char *path, const void *data, size_t size)
{
  if (!mounted || !path)
    return false;

  char temp[48];
  snprintf(temp, sizeof(temp), "%s.tmp", path);

  File file = LittleFS.open(temp, "w");
  if (!file)
    return false;
  const size_t written = file.write(static_cast<const uint8_t *>(data), size);
  file.close();
  if (written != size)
  {
    LittleFS.remove(temp);
    return false;
  }
  // เปลี่ยนชื่อทับเป็นจังหวะเดียวของ LittleFS ถ้าไฟดับก่อนหน้านี้ ไฟล์เดิมยังอยู่ครบ
  if (!LittleFS.rename(temp, path))
  {
    LittleFS.remove(temp);
    return false;
  }
  return true;
}

size_t flashStoreSize(const char *path)
{
  if (!mounted || !path || !LittleFS.exists(path))
    return 0;
  File file = LittleFS.open(path, "r");
  if (!file)
    return 0;
  const size_t size = file.size();
  file.close();
  return size;
}

bool flashStoreRead(const char *path, void *out, size_t size)
{
  if (!mounted || !path || !out)
    return false;
  File file = LittleFS.open(path, "r");
  if (!file)
    return false;
  const bool ok = file.size() == size && file.read(static_cast<uint8_t *>(out), size) == size;
  file.close();
  return ok;
}

void flashStoreRemove(const char *path)
{
  if (mounted && path && LittleFS.exists(path))
    LittleFS.remove(path);
}
