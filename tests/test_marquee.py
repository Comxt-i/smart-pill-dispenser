"""Compile the real marquee logic with hardware stubs; never edit production config."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MarqueeTest(unittest.TestCase):
    def test_marquee(self):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "Install a C++ compiler to run these tests")
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            for name in ("config.h", "hardware_profile.h", "marquee.h", "marquee.cpp"):
                shutil.copy(ROOT / "ESP32_Main" / name, build / name)
            executable = build / "marquee_test"
            subprocess.run([
                compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "tests/stubs"), "-I", str(build),
                str(build / "marquee.cpp"),
                str(ROOT / "tests/marquee_test.cpp"),
                "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
