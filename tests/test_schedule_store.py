"""Compile the real schedule engine with hardware stubs; never edit production config."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ScheduleStoreTest(unittest.TestCase):
    def test_schedule_engine(self):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "Install a C++ compiler to run these tests")
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            for name in ("config.h", "schedule_store.h", "schedule_store.cpp"):
                shutil.copy(ROOT / "ESP32_Main" / name, build / name)
            executable = build / "schedule_test"
            subprocess.run([
                compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "tests/stubs"), "-I", str(build),
                str(build / "schedule_store.cpp"),
                str(ROOT / "tests/schedule_store_test.cpp"),
                "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
