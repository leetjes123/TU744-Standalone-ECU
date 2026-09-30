"""Run new lifecycle leaf policies in actual Keil code at explicit input contracts.

No hardware timing, sensor transfer, or complete ECU startup is simulated.
"""
from pathlib import Path
import os
import re
import subprocess
import test_target as target

root = target.ROOT
out = root / 'build/lifecycle'
out.mkdir(parents=True, exist_ok=True)
fields = {
    'key': 'offsetof(Ecu,key_input)',
    'valid': 'offsetof(Ecu,cal)+offsetof(Calibration,valid)',
    'generation': 'offsetof(Ecu,cal)+offsetof(Calibration,generation)',
    'cal': 'offsetof(Ecu,cal)+offsetof(Calibration,bytes)',
    'oxygen': 'offsetof(Ecu,sensors)+offsetof(Sensors,oxygen)+offsetof(Sensor,quality)',
    'ready': 'offsetof(Ecu,sensors)+offsetof(Sensors,wideband_ready)',
    'raw': 'offsetof(Ecu,adc)+6*sizeof(AdcSample)+offsetof(AdcSample,raw)',
    'inhibits': 'offsetof(Ecu,authority)+offsetof(Authority,inhibits)',
    'tps': 'offsetof(Ecu,sensors)+offsetof(Sensors,tps)+offsetof(Sensor,quality)',
    'vss_count': 'offsetof(Ecu,vss_count)',
    'wideband_state': 'offsetof(WidebandState,state)',
    'armed': 'offsetof(VehicleState,armed)',
}
probe = out / 'layout.c'
probe.write_text('#include "lifecycle.h"\n#include <stddef.h>\nconst u16 layout[]={' +
                 ','.join(fields.values()) + '};\n')
keil = Path(os.environ.get('KEIL_C166', str(Path.home() / 'AppData/Local/Keil_v5/C166')))
build = subprocess.run([str(keil / 'BIN/C166.EXE'), str(probe), 'LARGE', 'MOD167',
                        'SRC', f'INCDIR({root / "include"})'], cwd=out, capture_output=True, text=True)
assert build.returncode == 0 and '0 WARNING(S),  0 ERROR(S)' in build.stdout, build.stdout
values = [int(v, 16) for v in re.findall(r'\bDW\s+([0-9A-F]+)H', probe.with_suffix('.SRC').read_text())]
assert len(values) == len(fields)
layout = dict(zip(fields, values))
listing = (target.BUILD / 'TU5JP.m66').read_text()
variables = {name: int(at, 16) for at, name in
             re.findall(r'^\s+([0-9A-F]{6})H\s+(\w+)\s+VAR', listing, re.M)}
ecu, wb, vehicle = (variables[n] for n in ['ecu', 'wideband', 'vehicle'])
mem = target.mem
target.call('ecu_init', {8: 1})
cal = ecu + layout['cal']
target.call('cal_example', {8: cal & 0x3fff, 9: cal >> 14})
mem.write8(ecu + layout['key'], 1)
mem.write8(ecu + layout['valid'], 1)
mem.write16(ecu + layout['generation'], 1)
mem.write8(cal + 0x600, 1)
mem.write8(cal + 0x916, 1)  # OEM upstream-heater conductors power the controller.
mem.write8(ecu + layout['oxygen'], 1)
mem.write16(ecu + layout['raw'], 512)

def word(at, value):
    mem.write8(cal + at, value >> 8)
    mem.write8(cal + at + 1, value & 255)

def wideband(now):
    target.call('wideband_update', {8: now & 65535, 9: (now >> 16) & 65535})
    return mem.read8(wb + layout['wideband_state']), mem.read8(ecu + layout['ready'])

assert wideband(0) == (0, 0)
mem.write8(cal + 0x926, 1)
word(0x928, 10000)
word(0x92a, 500)
word(0x92c, 100)
word(0x92e, 4900)
start = 0xfffff000
assert wideband(start) == (1, 0)
for i in range(1, 1051):
    state = wideband((start + i * 10) & 0xffffffff)
    expected = (1, 0) if i < 1000 else ((2, 0) if i < 1050 else (3, 1))
    assert state == expected, (i, state)
assert wideband((start + 11500) & 0xffffffff) == (4, 0)
mem.write16(ecu + layout['raw'], 0)
assert wideband((start + 11510) & 0xffffffff) == (4, 0)
mem.write16(ecu + layout['raw'], 512)
assert wideband((start + 11520) & 0xffffffff) == (1, 0)
assert wideband((start + 12520) & 0xffffffff) == (1, 0)
mem.write8(ecu + layout['key'], 0)
assert wideband(100000) == (0, 0)

mem.write8(ecu + layout['key'], 1)
mem.write16(ecu + layout['inhibits'], 0)
mem.write8(ecu + layout['tps'], 1)
mem.write8(cal + 0x5d4, 4)
mem.write8(cal + 0x7af, 10)
word(0x930, 5000)
assert target.call('launch_arm', {8: 0, 9: 2000, 10: 0}) & 255 == 0
assert target.call('launch_arm', {8: 1, 9: 2000, 10: 0}) & 255 == 1
assert target.call('launch_permitted', {8: 2001, 9: 0}) & 255 == 1
mem.write16(ecu + layout['vss_count'], 1)
assert target.call('launch_permitted', {8: 2002, 9: 0}) & 255 == 0
assert mem.read8(vehicle + layout['armed']) == 0
assert target.call('launch_arm', {8: 1, 9: 3000, 10: 0}) & 255 == 1
assert target.call('launch_permitted', {8: 8000, 9: 0}) & 255 == 0
print('PASS 1064 Keil lifecycle policy checks (analog qualification and explicit launch arming)')
