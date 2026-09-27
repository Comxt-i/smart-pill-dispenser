from pathlib import Path
import subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]


class NetAsyncTest(unittest.TestCase):
    def test_network_work_never_blocks_and_applies_on_the_main_loop(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = str(Path(directory) / 'net-async-test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            # the scriptable HTTP client must win over the syntax-only one
                            '-I', str(ROOT / 'tests/net_stubs'), '-I', str(ROOT / 'tests/syntax'),
                            str(ROOT / 'tests/net_async_test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
