#!/usr/bin/env python3
"""Run the app's physical baseline mode; save provenance, events and RSSI."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import select
import signal
import subprocess
import time
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[3]
EXPECTED = [(m, d, r) for m, d in ((11, 'tx'), (12, 'rx'), (14, 'tx'), (14, 'rx'))
            for r in (2000000, 4000000)]


def command(*args):
    try:
        p = subprocess.run(args, text=True, capture_output=True, timeout=10)
        return (p.stdout + p.stderr).strip()
    except (OSError, subprocess.TimeoutExpired) as exc:
        return str(exc)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def jabra_device(listing):
    matches = re.findall(r'^card \d+: (\S+) \[[^\n]*Jabra[^\n]*\], device (\d+):',
                         listing, re.M | re.I)
    if len(matches) != 1:
        raise ValueError('Expected one Jabra device; use --capture and --playback explicitly.\n' + listing)
    card, device = matches[0]
    return f'plughw:CARD={card},DEV={device}'


def summarize(rows, returncode):
    stops = [(int(r[1]), r[2], int(r[3])) for r in rows if r[0] == 'stop' and r[6] == 'ok']
    complete = any(r[0] == 'complete' and r[6] == 'awaiting-listener' for r in rows)
    stats = {}
    for menu, direction, rate in EXPECTED:
        values = [float(r[5]) for r in rows if r[0] == 'rssi'
                  and (int(r[1]), r[2], int(r[3])) == (menu, direction, rate)]
        if direction == 'rx':
            stats[f'{menu}_{direction}_{rate}'] = {
                'count': len(values), 'min_dbm': min(values) if values else None,
                'mean_dbm': sum(values) / len(values) if values else None,
                'max_dbm': max(values) if values else None,
            }
    ok = returncode == 0 and complete and stops == EXPECTED and all(s['count'] for s in stats.values())
    return {'automation_completed': ok, 'returncode': returncode, 'rssi': stats,
            'audible_result': 'pending user confirmation'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=ROOT / 'build/cariboulite_test_app')
    parser.add_argument('--firmware', type=Path, default=ROOT / 'firmware/top.bin')
    parser.add_argument('--audio-route', choices=('loopback', 'direct'), default='loopback',
                        help='existing Jabra loopback bridges (default), or exclusive direct Jabra access')
    parser.add_argument('--capture', help='override ALSA capture name')
    parser.add_argument('--playback', help='override ALSA playback name')
    parser.add_argument('--seconds', type=int, default=10, help='seconds per session, 2–300 (default 10)')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/physical-baseline')
    parser.add_argument('--dry-run', action='store_true', help='show sequence without touching hardware')
    args = parser.parse_args()
    if not 2 <= args.seconds <= 300:
        parser.error('--seconds must be 2–300')
    for menu, direction, rate in EXPECTED:
        print(f'Option {menu} pipeline: {direction.upper()} HiF, {rate // 1000000} MS/s, {args.seconds}s')
    if args.dry_run:
        print('Preceded by FPGA hard reset and firmware load. No hardware accessed.')
        return 0
    args.app = args.app.resolve()
    args.firmware = args.firmware.resolve()
    if not args.app.is_file() or not os.access(args.app, os.X_OK) or not args.firmware.is_file():
        parser.error('Build the app first and provide an existing firmware file.')
    capture_list, playback_list = command('arecord', '-l'), command('aplay', '-l')
    try:
        capture = args.capture or ('plughw:Loopback,1,1' if args.audio_route == 'loopback' else jabra_device(capture_list))
        playback = args.playback or ('plughw:Loopback,0,0' if args.audio_route == 'loopback' else jabra_device(playback_list))
    except ValueError as exc:
        parser.error(str(exc))
    out = args.output.resolve() / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    out.mkdir(parents=True)
    cmd = [str(args.app), '--baseline-test', str(args.seconds), capture, playback, str(args.firmware)]
    metadata = {
        'utc': datetime.now(timezone.utc).isoformat(), 'command': cmd,
        'revision': command('git', '-C', str(ROOT), 'rev-parse', 'HEAD'),
        'git_status': command('git', '-C', str(ROOT), 'status', '--short'),
        'uname': platform.uname()._asdict(), 'os_release': command('cat', '/etc/os-release'),
        'pi_model': command('cat', '/proc/device-tree/model').replace('\x00', ''),
        'capture_listing': capture_list, 'playback_listing': playback_list,
        'audio_route': args.audio_route,
        'audio_processes': command('ps', '-C', 'alsaloop,pipewire,pulseaudio,arecord,aplay', '-o', 'pid,args'),
        'app_sha256': digest(args.app), 'firmware_sha256': digest(args.firmware),
        'setup': {'load': '50 ohm dummy load on HiF', 'audio': 'Jabra GN 510 USB',
                  'monitor': 'SDRPlay RSP2PRO', 'monitor_rate': 2000000,
                  'frequency_hz': 430100000, 'mode': 'NFM',
                  'audio_bandwidth_hz': 10000, 'squelch_dbm': -120,
                  'tx_power_dbm': -3},
    }
    (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (out / 'source.diff').write_text(command('git', '-C', str(ROOT), 'diff', 'HEAD'))
    print(f'Capture: {capture}\nPlayback: {playback}\nResults: {out}', flush=True)
    rows = []
    proc = None
    failure = None
    try:
        with (out / 'app.log').open('wb', buffering=0) as log:
            proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=log,
                                    start_new_session=True)
            pending = b''
            deadline = time.monotonic() + 8 * (args.seconds + 15) + 120
            while True:
                if time.monotonic() > deadline:
                    raise TimeoutError('App exceeded overall test deadline')
                ready, _, _ = select.select([proc.stdout], [], [], 0.5)
                if not ready:
                    continue
                block = os.read(proc.stdout.fileno(), 65536)
                if not block:
                    break
                log.write(block)
                log.flush()
                pending += block
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    text = line.decode(errors='replace')
                    if text.startswith('BASELINE,') and not text.startswith('BASELINE,event,'):
                        row = next(csv.reader([text]))[1:]
                        if len(row) != 7:
                            raise ValueError('Malformed baseline event: ' + text)
                        rows.append(row)
                        print(text, flush=True)
            proc.wait(timeout=10)
    except (KeyboardInterrupt, TimeoutError, OSError, ValueError, subprocess.TimeoutExpired) as exc:
        failure = str(exc) or 'Interrupted by operator'
    finally:
        if proc is not None and proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
                failure = (failure or '') + '; forced termination: verify radio is idle before retrying'
    with (out / 'events.csv').open('w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['event', 'menu', 'direction', 'rate', 'elapsed_s', 'rssi_dbm', 'status'])
        writer.writerows(rows)
    result = summarize(rows, proc.returncode if proc else None)
    if failure:
        result['error'] = failure
        result['automation_completed'] = False
    (out / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    (out / 'listening-checklist.md').write_text('''# Listening results — pending

- [ ] Option 11: 600 Hz tone at 2 MS/s, correct pitch.
- [ ] Option 11: 600 Hz tone at 4 MS/s, same pitch.
- [ ] Quindar start/end tones in all four TX sessions (11 and 14, both rates).
- [ ] Receiver noise in all four RX sessions (12 and 14, both rates).

Record missing tones, distortion, dropouts or hangs by option and rate.
A completed automation run is not an audible pass. Dummy-load receiver noise
also does not establish reception/demodulation of a known RF signal.
''')
    print(json.dumps(result, indent=2))
    print(f'Record listening results in {out / "listening-checklist.md"}')
    return 0 if result['automation_completed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
