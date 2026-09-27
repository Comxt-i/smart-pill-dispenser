from pathlib import Path
import shutil, subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]

# secrets.h ถูก include แบบ "..." จึงเจอไฟล์จริงข้าง net_sync.cpp ก่อนเสมอ
# คัดลอกเฟิร์มแวร์ไปที่อื่นพร้อม secrets.h ปลอมที่เป็น https (ไม่แตะคีย์จริง)
FAKE_SECRETS = '''#pragma once
#define WIFI_SSID "test"
#define WIFI_PASSWORD "test"
#define SERVER_BASE_URL "https://pillbox.example.com"
#define DEVICE_API_KEY "test-key"
#define DEVICE_MAC_OVERRIDE ""
'''


class TlsClockTest(unittest.TestCase):
    def test_tls_clock_follows_device_clock(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            shutil.copytree(ROOT / 'ESP32_Main', work / 'ESP32_Main',
                            ignore=shutil.ignore_patterns('secrets.h'))
            (work / 'ESP32_Main' / 'secrets.h').write_text(FAKE_SECRETS)
            (work / 'tests').mkdir()
            shutil.copy(ROOT / 'tests/tls_clock_test.cpp', work / 'tests')
            exe = str(work / 'tls-clock-test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'tests/net_stubs'), '-I', str(ROOT / 'tests/syntax'),
                            str(work / 'tests/tls_clock_test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
