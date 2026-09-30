"""Headless Keil AN15 injection: native converter and explicit completion-model lanes.

Links a simulator-only main against freshly built production objects. The model
only supplies ADDAT2 from AIN15 at the production polling location. It never
writes a detector decision, reference, or retard. Each process has a 180s limit.
"""
from pathlib import Path
import argparse
import ast
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
import re
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT/'build/stock-95080'
OUT = ROOT/'build/keil-knock-detection'
OUT.mkdir(exist_ok=True)
KEIL = Path(os.environ.get('KEIL_C166',str(Path.home()/'AppData/Local/Keil_v5/C166')))
UV4 = KEIL.parent/'UV4/UV4.exe'
_user32 = ctypes.windll.user32
_ENUM = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
# Reuse only the dialog inspector; importing the boot runner would execute it.
boot_ast = ast.parse((ROOT/'tests/keil_knock_boot.py').read_text())
dialog_fn = next(n for n in boot_ast.body if isinstance(n, ast.FunctionDef) and n.name == '_dialogs')
exec(compile(ast.Module(body=[dialog_fn], type_ignores=[]), '<boot-dialog-helper>', 'exec'))

def run(args):
    r = subprocess.run([str(a) for a in args],cwd=OUT,capture_output=True,text=True)
    assert r.returncode == 0 and '*** ERROR' not in r.stdout, r.stdout+r.stderr
    return r.stdout

run([KEIL/'BIN/C166.EXE', ROOT/'tests/knock_keil_fixture.c','LARGE','MOD167','DEBUG',
     'OPTIMIZE(4,SPEED)','DEFINE(BOARD_RELEASED=1,STOCK_95080=1)',
     f'INCDIR({ROOT/"include"},{ROOT/"target/c167"})','OBJECT(fixture.obj)','PRINT(fixture.lst)'])
link = (BUILD/'firmware.lnp').read_text()
objects, rest = link.split(' TO ',1)
objects = ','.join(str(OUT/'fixture.obj') if name=='main.obj' else str(BUILD/name) for name in objects.split(','))
(OUT/'test.lnp').write_text(objects+' TO '+rest)
run([KEIL/'BIN/L166.EXE','@test.lnp'])
run([KEIL/'BIN/OH166.EXE','TU5JP','H167'])
listing = (OUT/'TU5JP.m66').read_text()
symbols = {n:int(a,16) for a,n in re.findall(r'^\s+([0-9A-F]{6})H\s+(\w+)\s+(?:LABEL|VAR)',listing,re.M)}
# Resolve the ADC polling site from the freshly linked function bytes.
image = bytearray(b'\xff'*0x80000); base = 0
for line in (OUT/'TU5JP.H86').read_text().splitlines():
    b=bytes.fromhex(line[1:]); n,a,k=b[0],int.from_bytes(b[1:3],'big'),b[3]
    if k==0: image[base+a:base+a+n]=b[4:4+n]
    elif k==2: base=int.from_bytes(b[4:6],'big')<<4
    elif k==4: base=int.from_bytes(b[4:6],'big')<<16
# At function entry hal_unlock is called twice. The first return is immediately
# before the ADCRQ polling loop, after ADDAT2/channel and ADCRQ are installed.
start=symbols['hal_knock_sample']; unlock=symbols['hal_unlock']
call=bytes([0xDA,unlock>>16,unlock&255,(unlock>>8)&255])
hits=[a for a in range(start,start+220,2) if image[a:a+4]==call]
assert len(hits)>=2, (hex(start),hits)
poll=hits[1]+4  # early busy return is the first unlock call
raws=[148,276,464,468,476,904,908,468,148,864]

def lane(name):
    log=OUT/(name+'.log'); log.unlink(missing_ok=True)
    rd=lambda n: f'_RWORD(0x{symbols[n]:X})'
    writes='\n'.join(f'  if (i == {i}) AIN15 = {(v+.5)*5/1024:.9f};' for i,v in enumerate(raws))
    script=f'''LOG > {log}
MAP 0x380000, 0x387FFF READ WRITE
LOAD {OUT/'TU5JP.H86'}
RESET
DEFINE LONG served
served = 0
FUNC void stimulus(void) {{
  unsigned int i;
  i = {rd('fixture_index')};
{writes}
}}
FUNC void held_integral(void) {{
  if ({rd('fixture_index')} == 10 && (P8 & 1)) {{
    if (P3 & 0x40) AIN15 = 0.725097656;
    else if (P3 & 0x20) AIN15 = 4.221191406;
    else AIN15 = (148.5 + ((P3 >> 1) & 7) * 12) * 5 / 1024;
  }}
}}
FUNC void complete_adc(void) {{
  unsigned int ch;
  ch = (ADDAT2 >> 12) & 15;
  printf("INJECT %u %f\\n", ch, AIN15);
  ADDAT2 = (ch << 12) | (unsigned int)(AIN15 * 1024 / 5);
  ADCON &= ~0x0800;
  served++;
}}
FUNC void result(void) {{
  printf("RESULT %u %u %u %u %u %u %u %u %u\\n", {rd('fixture_index')}, {rd('fixture_ok')}, {rd('fixture_raw')}, {rd('fixture_saved')}, {rd('fixture_ticks')}, _RBYTE(0x{symbols['fixture_monitor']:X}), _RBYTE(0x{symbols['fixture_monitor']+1:X}), _RBYTE(0x{symbols['fixture_monitor']+2:X}), _RBYTE(0x{symbols['fixture_monitor']+3:X}));
}}
FUNC void finished(void) {{
  printf("JOB %u %u %u %u %u\\n", {rd('fixture_ok')}, {rd('fixture_job_state')}, {rd('fixture_job_windows')}, {rd('fixture_job_fault')}, {rd('fixture_service')});
  printf("DONE %lu\\n", served); _break_ = 1;
}}
BS 0x{symbols['fixture_input']:X}, 1, "stimulus()"
BS 0x{symbols['fixture_result']:X}, 1, "result()"
BS 0x{symbols['fixture_done']:X}, 1, "finished()"
BS WRITE 0x000000, 1, "held_integral()"
BS WRITE 0xFFC4, 1, "held_integral()"
BS WRITE 0xFFD4, 1, "held_integral()"
'''
    if name=='completion-model': script+=f'BS 0x{poll:X}, 1, "complete_adc()"\n'
    script+='SIGNAL void stopper(void) { twatch(20000000); printf("TIMEOUT\\n"); _break_ = 1; }\nstopper()\ng\nLOG OFF\nEXIT\n'
    ini=OUT/(name+'.ini'); ini.write_text(script)
    tree=ET.parse(ROOT/'TU5JP.uvproj')
    for node in tree.iter():
        if node.tag=='RunToMain': node.text='0'
        if node.tag=='OutputDirectory': node.text=str(OUT)+'\\'
    tree.find('.//SimDlls/InitializationFile').text=str(ini)
    project=OUT/(name+f'-{os.getpid()}.uvproj'); tree.write(project,encoding='utf-8',xml_declaration=True)
    si=subprocess.STARTUPINFO(); si.dwFlags|=subprocess.STARTF_USESHOWWINDOW; si.wShowWindow=subprocess.SW_HIDE
    p=subprocess.Popen([str(UV4),'-d',str(project)],startupinfo=si)
    deadline = time.monotonic() + 180
    try:
        while p.poll() is None and time.monotonic() < deadline:
            for hwnd,texts,buttons in _dialogs(p.pid):
                if any('Registered ARM Compiler ignored' in t for t in texts):
                    for button in buttons: _user32.PostMessageW(button,0xF5,0,0)
                else: raise AssertionError('Unexpected simulator dialog: '+repr(texts))
            try: p.wait(timeout=0.25)
            except subprocess.TimeoutExpired: pass
        assert p.poll() is not None, name+' UV4 exceeded 180 seconds'
    finally:
        if p.poll() is None: p.kill(); p.wait()
    text=log.read_text(encoding='latin1')
    assert '*** error' not in text.lower() and re.search(r'^DONE \d+',text,re.M) and not re.search(r'^TIMEOUT$',text,re.M), text[-3000:]
    rows=[list(map(int,m)) for m in re.findall(r'^RESULT (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)',text,re.M)]
    assert len(rows)==10, rows
    if name=='native' and any(r[1]==0 for r in rows):
        assert all(r[3]==0xB155 for r in rows),rows
        return dict(status='simulator-blocked',reason='Keil did not complete injected ADC',rows=rows)
    for i,r in enumerate(rows):
        assert r[0:4]==[i,1,raws[i],0xB155],r
    expected=[0,0,0,4,8,8,12,12,12,12]
    assert [r[8] for r in rows]==expected,rows
    job=list(map(int,re.search(r'^JOB (\d+) (\d+) (\d+) (\d+) (\d+)',text,re.M).groups()))
    assert job==[1,2,13,0,1],job
    return dict(status='pass',rows=rows,bench_job=job)

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--lane',choices=['native','completion-model','both'],default='both')
    args=parser.parse_args()
    results={n:lane(n) for n in (['native','completion-model'] if args.lane=='both' else [args.lane])}
    results['image_sha256']=hashlib.sha256((OUT/'TU5JP.H86').read_bytes()).hexdigest()
    results['production_manifest']=json.loads((BUILD/'manifest.json').read_text())
    (OUT/'results.json').write_text(json.dumps(results,indent=2))
    print(json.dumps({k:v['status'] for k,v in results.items() if k in ('native','completion-model')}))
