"""Compile the real controller with hardware stubs; never edit production config."""

from pathlib import Path
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
                config = build / "config.h"
                if enabled:
                    original = config.read_text(encoding="utf-8")
                    self.assertIn("ENABLE_SERVO_MOVEMENT = false;", original)
                    config.write_text(original.replace(
                        "ENABLE_SERVO_MOVEMENT = false;", "ENABLE_SERVO_MOVEMENT = true;"
                    ), encoding="utf-8")
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
