from pathlib import Path
import subprocess
import tempfile
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
class RuntimeSetupTest(unittest.TestCase):
    def test_short_long_and_busy_gestures(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = str(Path(directory) / 'runtime-setup')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(ROOT/'tests/runtime_setup_test.cpp'), '-o', executable], check=True)
            subprocess.run([executable], check=True)
    def test_actual_button_and_application_handlers(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = str(Path(directory) / 'runtime-button-integration')
            linker = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-ffunction-sections', '-fdata-sections', linker,
                            '-I', str(ROOT/'tests/syntax'),
                            str(ROOT/'tests/runtime_button_integration_test.cpp'),
                            '-o', executable], check=True)
            subprocess.run([executable], check=True)
if __name__ == '__main__':
    unittest.main()
