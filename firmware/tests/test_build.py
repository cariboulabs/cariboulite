#!/usr/bin/env python3
"""Test dependency invalidation and failure isolation using fake FPGA tools."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
firmware = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    root=Path(temp); work=root/'firmware'; work.mkdir()
    shutil.copy2(firmware/'Makefile',work/'Makefile')
    for name in ['top.v','included.v','io.pcf']:(work/name).write_text('input\n')
    tool=root/'tool'
    tool.write_text('''#!/usr/bin/env python3
import os,sys,shlex
from pathlib import Path
name=Path(sys.argv[0]).name
with open('calls','a') as f:f.write(name+'\\n')
a=sys.argv[1:]
if name=='yosys':
 a=shlex.split(a[a.index('-p')+1]); outputs=[a[a.index('-json')+1],a[a.index('-blif')+1]]
elif name=='nextpnr':
 assert '--timing-allow-fail' not in a
 outputs=[a[a.index('--asc')+1],a[a.index('--report')+1]]
elif name=='icepack':outputs=[a[1]]
else:outputs=[a[2]]
for p in outputs:Path(p).write_text(name+' output\\n')
if os.environ.get('FAIL')==name:sys.exit(1)
''')
    tool.chmod(0o755)
    for name in ['yosys','nextpnr','icepack','blobgen']:(root/name).symlink_to(tool)
    opts=[f'YOSYS={root}/yosys',f'NEXTPNR={root}/nextpnr',f'ICEPACK={root}/icepack',f'BLOBGEN={root}/blobgen',f'LIB_HEADER={root}/lib/header.h']
    def run(fail=None):
        env=os.environ.copy()
        if fail:env['FAIL']=fail
        p=subprocess.run(['make','-j4','build',*opts],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        assert (p.returncode!=0)==bool(fail),p.stdout.decode()
    def calls():
        p=work/'calls'; result=p.read_text().splitlines() if p.exists() else [];p.write_text('');return result
    def change(name):
        # Force a newer prerequisite even on coarse timestamp filesystems.
        import time
        time.sleep(0.02);(work/name).touch()
    run();assert calls()==['yosys','nextpnr','icepack','blobgen']
    run();assert calls()==[]
    change('included.v');run();assert calls()==['yosys','nextpnr','icepack','blobgen']
    change('io.pcf');run();assert calls()==['nextpnr','icepack','blobgen']
    for failed in ['yosys','nextpnr','icepack','blobgen']:
        saved={p:p.read_bytes() for p in [work/'top.bin',work/'h-files/cariboulite_fpga_firmware.h',root/'lib/header.h']}
        change('included.v');run(failed);c=calls()
        assert c[-1]==failed and len(c)==['yosys','nextpnr','icepack','blobgen'].index(failed)+1,c
        for p,b in saved.items():assert p.read_bytes()==b
        run();calls()
    (work/'nextpnr_timing.json').unlink();run();assert calls()==['nextpnr','icepack','blobgen']
    print('PASS: incremental/parallel builds, HDL/PCF dependencies, missing report, all stage failures, preserved firmware')
