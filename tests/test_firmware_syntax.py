"""ตรวจไวยากรณ์ของโมดูลที่ต้องใช้ไลบรารีของ ESP32 โดยใช้ header จำลอง

ไม่ได้ทดสอบพฤติกรรม และไม่แทนการ compile ด้วย ESP32 toolchain จริง
แต่จับพวกพิมพ์ผิด ลืม include เรียกฟังก์ชันผิด signature ได้ตั้งแต่บนคอมพิวเตอร์
"""

from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

# (โฟลเดอร์ของ sketch, ไฟล์ที่จะตรวจ)
SOURCES = [
    ("ESP32_Main", "alert.cpp"),
    ("ESP32_Main", "buttons.cpp"),
    ("ESP32_Main", "command_journal.cpp"),
    ("ESP32_Main", "dispenser_control.cpp"),
    ("ESP32_Main", "event_queue.cpp"),
    ("ESP32_Main", "marquee.cpp"),
    ("ESP32_Main", "net_sync.cpp"),
    # คอมไพล์ซ้ำในโหมด ESP32 ไม่งั้นโค้ด task เบื้องหลังจะไม่ถูกตรวจเลยจนกว่าจะกด Upload
    ("ESP32_Main", "net_sync.cpp", ["-DARDUINO_ARCH_ESP32"]),
    ("ESP32_Main", "pill_app.cpp"),
    ("ESP32_Main", "rtc_lcd.cpp"),
    ("ESP32_Main", "schedule_store.cpp"),
    ("ESP32_Main", "status_led.cpp"),
    ("ESP32_Main", "wifi_web.cpp"),
    ("examples/TestDispenseEvent", "TestDispenseEvent.ino"),
    # sketch นี้แยกสาขาตามบอร์ด ต้องบอกให้ชัดว่าตรวจสาขาของ ESP32
    ("examples/TwoPlateDispense", "TwoPlateDispense.ino", ["-DESP32"]),
    ("examples/ServoCalibrate", "ServoCalibrate.ino"),
]


class FirmwareSyntaxTest(unittest.TestCase):
    def test_every_module_parses(self):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "Install a C++ compiler to run these tests")

        for source in SOURCES:
            folder, name = source[0], source[1]
            extra_flags = source[2] if len(source) > 2 else []
            with self.subTest(source="%s/%s" % (folder, name)):
                command = [
                    compiler,
                    "-fsyntax-only",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-I",
                    str(ROOT / "tests/syntax"),
                    # ให้ไฟล์ในโฟลเดอร์ sketch เดียวกัน include หากันได้
                    # เหมือนตอนที่ Arduino IDE คอมไพล์ทั้งโฟลเดอร์รวมกัน
                    "-I",
                    str(ROOT / folder),
                ]

                # .ino เป็น C++ แต่ g++ ไม่รู้จักนามสกุลนี้ ต้องบอกภาษาให้ชัด
                if name.endswith(".ino"):
                    command += ["-x", "c++"]

                command += extra_flags
                command.append(str(ROOT / folder / name))

                result = subprocess.run(
                    command,
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    "%s/%s ไม่ผ่านการตรวจไวยากรณ์:\n%s" % (folder, name, result.stderr),
                )


if __name__ == "__main__":
    unittest.main()
