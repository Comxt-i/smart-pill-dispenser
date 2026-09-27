from pathlib import Path
import subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]


class ScheduleCacheTest(unittest.TestCase):
    def test_schedule_and_closed_doses_survive_a_reboot(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = str(Path(directory) / 'schedule-cache-test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'tests/syntax'), '-I', str(ROOT / 'ESP32_Main'),
                            str(ROOT / 'tests/schedule_cache_test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
