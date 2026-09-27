from pathlib import Path
import subprocess, unittest
ROOT = Path(__file__).resolve().parents[1]
class HardwareProfileTest(unittest.TestCase):
    def test_calibration_gate_and_pulse_limits(self):
        base = ["c++", "-std=c++17", "-fsyntax-only", "-x", "c++", "-", "-I", str(ROOT/"tests/syntax"), "-I", str(ROOT/"ESP32_Main"),
            "-DPILLBOX_IGNORE_LOCAL_PROFILE=1"]
        for flags, succeeds in [([], True), (["-DPILLBOX_REAL_HARDWARE=true"], False),
                                (["-DPILLBOX_REAL_HARDWARE=true", "-DPILLBOX_CALIBRATED=true"], True),
                                (["-DPILLBOX_HOLE_PULSES={{9999,2463,833,537},{2167,2463,833,537},{2167,2463,833,537}}"], False),
                                # a hole calibrated onto the rest position would never move the plate
                                (["-DPILLBOX_HOLE_PULSES={{2167,2463,833,537},{2167,1500,833,537},{2167,2463,833,537}}"], False),
                                (["-DPILLBOX_HOLE_PULSES={{2100,2400,900,600},{2100,2400,900,600},{2100,2400,900,600}}"], True)]:
            with self.subTest(flags=flags):
                result = subprocess.run(base + flags, input='#include "config.h"\n', text=True, capture_output=True)
                self.assertEqual(result.returncode == 0, succeeds, result.stderr)
