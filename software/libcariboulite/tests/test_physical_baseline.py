#!/usr/bin/env python3
"""Test reporting and the runner with a fake app; never access radio hardware."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'tools/physical_baseline.py'
spec = importlib.util.spec_from_file_location('baseline', SCRIPT)
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)


class BaselineTest(unittest.TestCase):
    def test_device_discovery(self):
        listing = 'card 2: USB [Jabra SPEAK 510 USB], device 0: USB Audio [USB Audio]'
        self.assertEqual(b.jabra_device(listing), 'plughw:CARD=USB,DEV=0')
        with self.assertRaises(ValueError):
            b.jabra_device('no soundcards')
        with self.assertRaises(ValueError):
            b.jabra_device(listing + '\n' + listing)

    def test_incomplete_run_is_not_pass(self):
        self.assertFalse(b.summarize([], 0)['automation_completed'])

    def test_fake_app_reporting(self):
        with tempfile.TemporaryDirectory() as directory:
            d = Path(directory)
            app = d / 'fake-app'
            app.write_text('''#!/usr/bin/env python3
import sys
print('diagnostic on stderr', file=sys.stderr)
for menu, direction in ((11, 'tx'), (12, 'rx'), (14, 'tx'), (14, 'rx')):
    for rate in (2000000, 4000000):
        if direction == 'rx':
            print(f'BASELINE,rssi,{menu},{direction},{rate},1,-95.0,ok')
        print(f'BASELINE,stop,{menu},{direction},{rate},0,,ok')
print('BASELINE,complete,0,none,0,0,,awaiting-listener')
''')
            app.chmod(0o755)
            firmware = d / 'firmware.bin'
            firmware.write_bytes(b'test fixture')
            result = subprocess.run(['python3', str(SCRIPT), '--app', str(app),
                '--firmware', str(firmware), '--capture', 'null', '--playback', 'null',
                '--output', str(d / 'results'), '--seconds', '2'], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            out = next((d / 'results').iterdir())
            summary = json.loads((out / 'summary.json').read_text())
            self.assertTrue(summary['automation_completed'])
            self.assertEqual(summary['audible_result'], 'pending user confirmation')
            self.assertEqual(len(summary['rssi']), 4)
            self.assertTrue(all(s['mean_dbm'] == -95 for s in summary['rssi'].values()))
            self.assertIn('diagnostic on stderr', (out / 'app.log').read_text())
            self.assertTrue((out / 'listening-checklist.md').exists())


if __name__ == '__main__':
    unittest.main()
