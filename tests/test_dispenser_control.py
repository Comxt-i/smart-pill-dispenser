"""Compile the real controller with hardware stubs; never edit production config."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DispenserControlTest(unittest.TestCase):
    def test_controller(self):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "Install a C++ compiler to run these tests")
        for enabled in (False, True):
            with self.subTest(movement_enabled=enabled), tempfile.TemporaryDirectory() as directory:
                build = Path(directory)
                for name in ("config.h", "dispenser_control.h", "dispenser_control.cpp"):
                    shutil.copy(ROOT / "ESP32_Main" / name, build / name)
                # บังคับค่าธงให้ตรงกับโหมดที่กำลังทดสอบ ไม่ว่า config จริงจะตั้งไว้เป็นอะไร
                #
                # เดิมโค้ดนี้ยืนยันว่า config จริงต้องเป็น false แล้วค่อยสลับเป็น true
                # พอโปรเจกต์เปิด servo จริงแล้วเทสต์จึงล้ม และถ้าแก้แบบแทนที่ทางเดียว
                # จะกลายเป็นทดสอบโหมดเดิมซ้ำสองรอบโดยไม่มีใครรู้
                config = build / "config.h"
                wanted = "true" if enabled else "false"
                text = config.read_text(encoding="utf-8")
                patched, count = re.subn(
                    r"ENABLE_SERVO_MOVEMENT = (?:true|false);",
                    f"ENABLE_SERVO_MOVEMENT = {wanted};",
                    text,
                )
                self.assertEqual(count, 1, "ต้องพบธง ENABLE_SERVO_MOVEMENT พอดีหนึ่งจุด")
                self.assertIn(f"ENABLE_SERVO_MOVEMENT = {wanted};", patched)
                config.write_text(patched, encoding="utf-8")
                executable = build / "controller_test"
                subprocess.run([
                    compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "tests/stubs"), "-I", str(build),
                    str(build / "dispenser_control.cpp"),
                    str(ROOT / "tests/dispenser_control_test.cpp"),
                    "-o", str(executable),
                ], check=True)
                subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
