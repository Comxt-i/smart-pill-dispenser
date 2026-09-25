#!/usr/bin/env python3
"""Build the current source, upload without erasing NVS, or open Serial Monitor."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FQBN = 'esp32:esp32:esp32'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'upload', 'monitor', 'ports'])
    parser.add_argument('--port', help='Explicit USB serial port; required for upload/monitor')
    parser.add_argument('--cli', default=os.environ.get('ARDUINO_CLI'))
    parser.add_argument('--config', type=Path, help='Optional arduino-cli.yaml')
    parser.add_argument('--build-dir', type=Path, default=ROOT/'build'/'esp32')
    args = parser.parse_args()
    if args.action in ('upload', 'monitor') and not args.port:
        parser.error('--port is required; run ports first')
    cli = args.cli or shutil.which('arduino-cli')
    bundled = Path('/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli')
    if not cli and bundled.is_file():
        cli = str(bundled)
    if not cli:
        parser.error('Install Arduino IDE 2 or arduino-cli, then use --cli if necessary')
    cmd = [cli]
    config = args.config or Path.home()/'.arduinoIDE'/'arduino-cli.yaml'
    if config.is_file():
        cmd += ['--config-file', str(config)]
    elif args.config:
        parser.error('The specified Arduino CLI config does not exist')
    if args.action == 'ports':
        subprocess.run(cmd + ['board', 'list'], check=True)
        return
    if args.action == 'monitor':
        subprocess.run(cmd + ['monitor', '-p', args.port, '--config', 'baudrate=115200'], check=True)
        return
    sketch = ROOT/'ESP32_Main'
    if not (sketch/'secrets.h').is_file():
        parser.error('Create ESP32_Main/secrets.h from secrets.example.h with this device\'s settings')
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    manifest = build/'upload-manifest.json'
    # Never leave a previous manifest implying a failed build is current.
    manifest.unlink(missing_ok=True)
    subprocess.run(cmd + ['compile', '--fqbn', FQBN, '--build-path', str(build), str(sketch)], check=True)
    files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in build.glob('*.bin')}
    manifest.write_text(json.dumps({
        'fqbn': FQBN, 'source': str(sketch), 'binaries_sha256': files,
        'local_hardware_profile': (sketch/'hardware.local.h').is_file(),
        'note': 'Build artifacts contain device credentials. Keep private. Upload preserves NVS.'
    }, indent=2) + '\n')
    print(f'Build ready: {build}\nManifest: {manifest}', flush=True)
    if args.action == 'upload':
        subprocess.run(cmd + ['upload', '--fqbn', FQBN, '-p', args.port,
                              '--input-dir', str(build), str(sketch)], check=True)
        print('Upload complete. Open monitor to verify firmware version and operating mode.')

if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except KeyboardInterrupt:
        sys.exit(130)
