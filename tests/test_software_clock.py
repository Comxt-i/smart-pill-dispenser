from pathlib import Path
import subprocess, tempfile, unittest
ROOT = Path(__file__).resolve().parents[1]
class SoftwareClockTest(unittest.TestCase):
    def test_real_rtc_module_without_hardware(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = str(Path(directory)/'clock-test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT/'tests/clock_stubs'), '-I', str(ROOT/'tests/syntax'),
                            str(ROOT/'tests/software_clock_test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)
if __name__ == '__main__': unittest.main()
