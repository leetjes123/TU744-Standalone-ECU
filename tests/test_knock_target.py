"""Execute linked C166 detector and monitor code; compiler-derived layouts."""
from pathlib import Path
import os
import re
import subprocess
import random
import test_target as target

ROOT = target.ROOT
out = ROOT/'build/knock-target'
out.mkdir(exist_ok=True)
fields = {
    'KnockState': ['reference','gain','offset','null_start','decision','ratio','gain_code',
                   'mode','retard','scheduled','valid','raw','mv','stamp','last_knock','recent',
                   'qualified','fault','last_normal'],
    'KnockConfig': ['eligible','divisor','threshold','gain_code','manual_gain','latch_ms','stale_ms'],
}
expressions = ['sizeof(KnockState)', 'sizeof(KnockConfig)']
expressions += [f'offsetof({s},{f})' for s, fs in fields.items() for f in fs]
probe = out/'layout.c'
probe.write_text('#include "knock.h"\n#include <stddef.h>\nconst u16 layout[]={' + ','.join(expressions) + '};\n')
keil = Path(os.environ.get('KEIL_C166', str(Path.home()/'AppData/Local/Keil_v5/C166')))
run = subprocess.run([str(keil/'BIN/C166.EXE'),str(probe),'LARGE','MOD167','SRC',
                      f'INCDIR({ROOT/"include"})'],cwd=out,capture_output=True,text=True)
assert run.returncode == 0 and '0 WARNING(S),  0 ERROR(S)' in run.stdout, run.stdout
values = [int(v,16) for v in re.findall(r'\bDW\s+([0-9A-F]+)H', probe.with_suffix('.SRC').read_text())]
assert len(values) == len(expressions), values
size_s, size_c = values[:2]
layout = dict(zip(expressions, values))
def offset(s, f): return layout[f'offsetof({s},{f})']
def put(base, s, f, value, width=1):
    for i in range(width): target.mem.write8(base+offset(s,f)+i, (value>>(8*i))&255)
def get(base,s,f): return target.mem.read8(base+offset(s,f))
state, cfg = 0x383000, 0x383100
for i in range(size_s): target.mem.write8(state+i, 0)
for i in range(size_c): target.mem.write8(cfg+i, 0)
put(cfg,'KnockConfig','eligible',1); put(cfg,'KnockConfig','divisor',16)
for i, code in enumerate([0,1,2,3,5,6,7]): target.mem.write8(cfg+offset('KnockConfig','gain_code')+i,code)
rng = random.Random(744)
cases = [(raw>>2,32,4,40,37) for raw in range(1024)]
cases += [(rng.randrange(256),rng.randrange(1,256),rng.randrange(7),rng.randrange(16,81),rng.randrange(25,49)) for _ in range(1000)]
for raw, ref, gain, threshold, null in cases:
    for f,v in [('reference',ref),('gain',gain),('offset',null),('null_start',null)]: put(state,'KnockState',f,v)
    put(cfg,'KnockConfig','threshold',threshold)
    target.call('knock_detect',{8:0x3000,9:0xE0,10:0x3100,11:0xE0,12:raw})
    amp = max(0,raw-null)
    ratio = (amp*16//min(ref,255 if gain else 240))&255
    decision = int(ratio >= threshold or raw-null > 189)
    assert (get(state,'KnockState','decision'),get(state,'KnockState','ratio')) == (decision,ratio)
listing = (target.BUILD/'TU5JP.m66').read_text()
variables = {name:int(at,16) for at,name in re.findall(r'^\s+([0-9A-F]{6})H\s+(\w+)\s+VAR',listing,re.M)}
ks, kc = variables['knock'], variables['knock_config']
target.call('knock_init',{})
for f,v,w in [('mv',2285,2),('valid',1,1),('stamp',1000,4),('last_knock',1000,4),
              ('recent',1,1),('mode',2,1),('qualified',1,1),('last_normal',1,1),('retard',4,1)]:
    put(ks,'KnockState',f,v,w)
put(kc,'KnockConfig','stale_ms',150,2); put(kc,'KnockConfig','latch_ms',500,2)
put(kc,'KnockConfig','eligible',1)
# Far pointer R8/R9, long R10/R11.
target.call('knock_monitor',{8:0x3200,9:0xE0,10:1050,11:0})
assert bytes(target.mem.read8(0x383200+i) for i in range(4)) == bytes([8,237,135,4])
assert target.call('knock_advance',{8:20}) == 16
assert get(ks,'KnockState','scheduled') == 4
for raw in range(1024): assert target.call('knock_mv',{8:raw}) == (raw*5000+512)//1024
print(f'PASS linked C166 knock: {len(cases)} detector cases, 1024 voltage conversions, global advance and serialized monitor')
