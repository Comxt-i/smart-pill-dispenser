from pathlib import Path
import subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]


class PillHoleTest(unittest.TestCase):
    def test_size_from_web_maps_to_hole(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = str(Path(directory) / 'pill-hole-test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/pill_hole_test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
