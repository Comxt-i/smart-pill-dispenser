"""ตรวจไวยากรณ์ของโมดูลที่ต้องใช้ไลบรารีของ ESP32 โดยใช้ header จำลอง

ไม่ได้ทดสอบพฤติกรรม และไม่แทนการ compile ด้วย ESP32 toolchain จริง
แต่จับพวกพิมพ์ผิด ลืม include เรียกฟังก์ชันผิด signature ได้ตั้งแต่บนคอมพิวเตอร์
"""

from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    "alert.cpp",
    "buttons.cpp",
    "dispenser_control.cpp",
    "event_queue.cpp",
    "net_sync.cpp",
    "pill_app.cpp",
    "rtc_lcd.cpp",
    "schedule_store.cpp",
    "wifi_web.cpp",
]


class FirmwareSyntaxTest(unittest.TestCase):
    def test_every_module_parses(self):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "Install a C++ compiler to run these tests")

        for name in SOURCES:
            with self.subTest(source=name):
                result = subprocess.run(
                    [
                        compiler,
                        "-fsyntax-only",
                        "-std=c++17",
                        "-Wall",
                        "-Wextra",
                        "-I",
                        str(ROOT / "tests/syntax"),
                        "-I",
                        str(ROOT / "ESP32_Main"),
                        str(ROOT / "ESP32_Main" / name),
                    ],
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    "%s ไม่ผ่านการตรวจไวยากรณ์:\n%s" % (name, result.stderr),
                )


if __name__ == "__main__":
    unittest.main()
