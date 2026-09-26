from pathlib import Path
import subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]


class EventQueueTest(unittest.TestCase):
    def test_offline_intake_records_survive_until_sent(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = str(Path(directory) / 'event-queue-test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'tests/syntax'), '-I', str(ROOT / 'ESP32_Main'),
                            str(ROOT / 'tests/event_queue_test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
