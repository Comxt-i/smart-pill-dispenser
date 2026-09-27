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
        # (มอเตอร์จริง, เซ็นเซอร์ IR) รอบสุดท้ายคือสภาพเครื่องจริงตอนนี้ที่ยังไม่มี IR
        for enabled, sensor in ((False, True), (True, True), (True, False)):
            with self.subTest(movement_enabled=enabled, sensor=sensor), tempfile.TemporaryDirectory() as directory:
                build = Path(directory)
                for name in ("config.h", "hardware_profile.h", "dispenser_control.h", "dispenser_control.cpp"):
                    shutil.copy(ROOT / "ESP32_Main" / name, build / name)
                # บังคับค่าธงให้ตรงกับโหมดที่กำลังทดสอบ ไม่ว่า config จริงจะตั้งไว้เป็นอะไร
                #
                # เดิมโค้ดนี้ยืนยันว่า config จริงต้องเป็น false แล้วค่อยสลับเป็น true
                # พอโปรเจกต์เปิด servo จริงแล้วเทสต์จึงล้ม และถ้าแก้แบบแทนที่ทางเดียว
                # จะกลายเป็นทดสอบโหมดเดิมซ้ำสองรอบโดยไม่มีใครรู้
                config = build / "config.h"
                profile = ""
                if enabled:
                    profile += "#define PILLBOX_REAL_HARDWARE true\n#define PILLBOX_CALIBRATED true\n"
                if not sensor:
                    profile += "#define PILLBOX_PILL_SENSOR false\n#define PILLBOX_ALLOW_UNVERIFIED_DISPENSE true\n"
                if profile:
                    (build / "hardware.local.h").write_text(profile)
                executable = build / "controller_test"
                subprocess.run([
                    compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    # ยืนยันว่าโหมดที่ได้จริงตรงกับที่ตั้งใจ ไม่งั้นสองรอบอาจทดสอบโหมดเดียวกันซ้ำ
                    f"-DEXPECT_SERVO_MOVEMENT={1 if enabled else 0}",
                    f"-DEXPECT_PILL_SENSOR={1 if sensor else 0}",
                    "-I", str(ROOT / "tests/stubs"), "-I", str(build),
                    str(build / "dispenser_control.cpp"),
                    str(ROOT / "tests/dispenser_control_test.cpp"),
                    "-o", str(executable),
                ], check=True)
                subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
