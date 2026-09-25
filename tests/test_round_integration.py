from pathlib import Path
import shutil, subprocess, sys, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class RoundIntegrationTest(unittest.TestCase):
    def test_real_application_schedule_buttons_and_multi_channel_controller(self):
        for real in (False,True):
            with self.subTest(real=real), tempfile.TemporaryDirectory() as directory:
                build=Path(directory)
                shutil.copy(ROOT/'tests/stubs/ESP32Servo.h',build/'ESP32Servo.h')
                exe=str(build/'round-test')
                linker='-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections'
                flags=['-DPILLBOX_REAL_HARDWARE=true','-DPILLBOX_CALIBRATED=true'] if real else []
                subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',linker,
                    '-I',str(build),'-I',str(ROOT/'tests/syntax'),*flags,
                    str(ROOT/'tests/round_integration_test.cpp'), str(ROOT/'ESP32_Main/dispenser_control.cpp'),
                    str(ROOT/'ESP32_Main/schedule_store.cpp'),'-o',exe],check=True)
                subprocess.run([exe],check=True)
