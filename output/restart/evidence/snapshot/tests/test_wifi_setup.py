from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class WifiSetupTest(unittest.TestCase):
    def run_firmware_test(self, source, preset):
        with tempfile.TemporaryDirectory() as directory:
            # Use fake credentials even when a real local secrets.h exists.
            sketch = Path(directory) / 'ESP32_Main'
            shutil.copytree(ROOT/'ESP32_Main', sketch, ignore=shutil.ignore_patterns('secrets.h'))
            shutil.copy(ROOT/'tests/syntax/secrets.h', sketch/'secrets.h')
            test_dir = Path(directory) / 'tests'
            test_dir.mkdir()
            shutil.copy(ROOT/'tests'/source, test_dir/source)
            exe = str(Path(directory) / 'wifi-setup-test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', f'-DWIFI_PRESET_ENABLED={int(preset)}', '-I', str(ROOT/'tests/syntax'), '-I', str(sketch), str(test_dir/source), '-o', exe], check=True)
            subprocess.run([exe], check=True)

    def test_portal_state_machine(self):
        self.run_firmware_test('wifi_setup_test.cpp', False)

    def test_preset_network(self):
        self.run_firmware_test('wifi_preset_test.cpp', True)
