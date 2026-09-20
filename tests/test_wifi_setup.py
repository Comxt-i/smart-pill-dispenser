from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class WifiSetupTest(unittest.TestCase):
    def test_portal_state_machine(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = str(Path(directory) / 'wifi-setup-test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'tests/syntax'), '-I', str(ROOT/'ESP32_Main'), str(ROOT/'tests/wifi_setup_test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)
