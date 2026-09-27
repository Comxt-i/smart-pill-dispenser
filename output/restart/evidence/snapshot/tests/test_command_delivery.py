from pathlib import Path
import subprocess, sys, tempfile, unittest
ROOT = Path(__file__).resolve().parents[1]
class CommandDeliveryTest(unittest.TestCase):
    def test_real_command_queue_deduplicates_and_reserves_before_delivery(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = str(Path(directory)/'delivery')
            linker = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-ffunction-sections', '-fdata-sections', linker,
                            '-I', str(ROOT/'tests/syntax'), str(ROOT/'tests/command_delivery_test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)
