from pathlib import Path
import subprocess, unittest
ROOT = Path(__file__).resolve().parents[1]
class HardwareProfileTest(unittest.TestCase):
    def test_calibration_gate_and_pulse_limits(self):
        base = ["c++", "-std=c++17", "-fsyntax-only", "-x", "c++", "-", "-I", str(ROOT/"tests/syntax"), "-I", str(ROOT/"ESP32_Main")]
        for flags, succeeds in [([], True), (["-DPILLBOX_REAL_HARDWARE=true"], False),
                                (["-DPILLBOX_REAL_HARDWARE=true", "-DPILLBOX_CALIBRATED=true"], True),
                                (["-DPILLBOX_RELEASE_PULSES={9999,1750,1750}"], False)]:
            with self.subTest(flags=flags):
                result = subprocess.run(base + flags, input='#include "config.h"\n', text=True, capture_output=True)
                self.assertEqual(result.returncode == 0, succeeds, result.stderr)
