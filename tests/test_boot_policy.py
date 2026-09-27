from pathlib import Path
import subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]


class BootPolicyTest(unittest.TestCase):
    def test_server_first_then_cached_schedule(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = str(Path(directory) / 'boot-policy-test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/boot_policy_test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
