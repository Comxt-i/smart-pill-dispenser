from pathlib import Path
import subprocess, tempfile, unittest
ROOT = Path(__file__).resolve().parents[1]
class CommandJournalTest(unittest.TestCase):
    def test_retry_reboot_storage_failure_and_capacity(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = str(Path(directory)/"journal")
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT/"tests/syntax"), str(ROOT/"tests/command_journal_test.cpp"), "-o", exe], check=True)
            subprocess.run([exe], check=True)
