#!/usr/bin/env python3
"""Run the actual installer with mocked build/system tools; never use sudo."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

DRIVER = Path(__file__).resolve().parents[1]
STUB = r'''#!/usr/bin/env python3
import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['CALL_LOG'], 'a') as log:
    log.write(json.dumps([name, *args]) + '\n')
case = os.environ['CASE']
stage = name
if name == 'cmake':
    stage = 'build' if '--build' in args else 'configure'
if name == 'sudo':
    stage = args[0]
if case == stage:
    sys.exit(1)
if name == 'id':
    print(1000)
elif name == 'uname':
    print('test-kernel')
elif name == 'dpkg-query':
    print('install ok installed')
elif name == 'cmake' and '--build' in args:
    (pathlib.Path(args[1]) / 'smi_stream_dev.ko').write_bytes(b'module')
elif name == 'modinfo':
    print(('wrong-kernel' if case == 'wrong-vermagic' else 'test-kernel') + ' SMP preempt')
elif name == 'find':
    assert args[0] == '/lib/modules/test-kernel', args
    if case != 'missing-destination':
        print('/lib/modules/test-kernel/kernel/drivers/char/broadcom')
    if case == 'ambiguous-destination':
        print('/lib/modules/test-kernel/extra')
elif name == 'xz':
    sys.stdout.buffer.write(b'compressed module')
elif name == 'generate_bin_blob':
    pathlib.Path(args[2]).write_text('generated header')
elif name == 'sudo' and args[0] == 'tee':
    sys.stdin.read()
'''

with tempfile.TemporaryDirectory(prefix='cariboulite installer ') as tmp:
    root = Path(tmp)
    driver = root / 'driver'
    driver.mkdir()
    shutil.copy2(DRIVER / 'install.sh', driver / 'install.sh')
    for name in ('bcm2835_smi.h', 'smi_stream_dev.h'):
        (driver / name).write_text('header')
    (driver / 'udev').mkdir()
    (root / 'software/libcariboulite/src/caribou_smi/kernel').mkdir(parents=True)
    utils = root / 'software/utils'
    utils.mkdir()
    blob = utils / 'generate_bin_blob'
    blob.write_text(STUB)
    blob.chmod(0o755)
    bin_dir = root / 'bin'
    bin_dir.mkdir()
    for name in ('id', 'uname', 'dpkg-query', 'cmake', 'modinfo', 'find', 'xz', 'sudo'):
        tool = bin_dir / name
        tool.write_text(STUB)
        tool.chmod(0o755)
    cases = ('success', 'apt-get', 'configure', 'build', 'modinfo',
             'wrong-vermagic', 'find', 'missing-destination',
             'ambiguous-destination', 'xz', 'generate_bin_blob', 'cp', 'depmod')
    for case in cases:
        log = root / 'calls.jsonl'
        log.write_text('')
        env = dict(os.environ, PATH=f'{bin_dir}:{os.environ["PATH"]}',
                   CALL_LOG=str(log), CASE=case)
        result = subprocess.run(['bash', str(driver / 'install.sh'), 'install'],
                                cwd=root, env=env, capture_output=True, text=True)
        calls = [json.loads(line) for line in log.read_text().splitlines()]
        privileged = [call[1:] for call in calls if call[0] == 'sudo']
        writes = [call for call in privileged if call[0] != 'apt-get']
        if case == 'success':
            assert result.returncode == 0, result.stderr
            assert ['apt-get', '-y', 'install', 'linux-headers-test-kernel'] in privileged
            copies = [call for call in writes if call[0] == 'cp']
            assert len(copies) == 1, copies
            assert copies[0][-1] == '/lib/modules/test-kernel/kernel/drivers/char/broadcom/'
            assert ['depmod', '-a', 'test-kernel'] in writes
            assert sum(call[0] == 'tee' for call in writes) == 6
            configure = next(call for call in calls if call[0] == 'cmake' and '-S' in call)
            assert '-DKERNEL_RELEASE=test-kernel' in configure
            assert '-DKERNELHEADERS_DIR=/lib/modules/test-kernel/build' in configure
        else:
            assert result.returncode != 0, (case, result.stdout, result.stderr)
            assert not any(call[0] == 'tee' for call in writes), (case, writes)
            if case not in ('cp', 'depmod'):
                assert not writes, (case, writes)
            assert 'Installation completed.' not in result.stdout, case
        print(f'PASS: {case}')
print('Installer destination and failure-path checks passed')
