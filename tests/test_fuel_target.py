"""Exercise schema-4 fuel arithmetic in the actual linked C167 image."""
from pathlib import Path
import os
import re
import subprocess
import test_target as target

root = target.ROOT
out = root / 'build/fuel-policy'
out.mkdir(parents=True, exist_ok=True)
keil = Path(os.environ.get('KEIL_C166', str(Path.home() / 'AppData/Local/Keil_v5/C166')))
fields = {name: f'offsetof(Ecu,{name})' for name in ['cal', 'rotation', 'authority', 'sensors', 'control']}
for typ, members in [('Calibration', ['bytes']), ('Rotation', ['rpm']),
                     ('Authority', ['plan']), ('EnginePlan', ['requested_us', 'pulse_us', 'rpm']),
                     ('Controls', ['mode', 'running_at', 'afterstart', 've', 'ae_percent', 'ae_peak']),
                     ('Sensor', ['value', 'quality']),
                     ('Sensors', ['tps', 'clt', 'iat', 'battery', 'map'])]:
    fields.update({f'{typ}.{member}': f'offsetof({typ},{member})' for member in members})
probe = out / 'layout.c'
probe.write_text('#include "ecu.h"\n#include <stddef.h>\nconst u16 layout[]={' +
                 ','.join(fields.values()) + '};\n')
result = subprocess.run([str(keil / 'BIN/C166.EXE'), str(probe), 'LARGE', 'MOD167', 'SRC',
                         f'INCDIR({root / "include"})'], cwd=out, capture_output=True, text=True)
assert result.returncode == 0 and '0 WARNING(S),  0 ERROR(S)' in result.stdout, result.stdout
values = [int(v, 16) for v in re.findall(r'\bDW\s+([0-9A-F]+)H', probe.with_suffix('.SRC').read_text())]
assert len(values) == len(fields)
layout = dict(zip(fields, values))
variables = {name: int(at, 16) for at, name in re.findall(
    r'^\s+([0-9A-F]{6})H\s+(\w+)\s+VAR', (target.BUILD / 'TU5JP.m66').read_text(), re.M)}
mem = target.mem
base = variables['ecu']
cal = base + layout['cal'] + layout['Calibration.bytes']
rotation = base + layout['rotation']
control = base + layout['control']
plan = base + layout['authority'] + layout['Authority.plan']
checks = 0


def pointer(address, register):
    return {register: address & 0x3fff, register + 1: address >> 14}


def be16(at, value):
    mem.write8(cal + at, value >> 8)
    mem.write8(cal + at + 1, value & 255)


def table(at, value, count=16, wide=False):
    for i in range(count):
        if wide: be16(at + 2*i, value)
        else: mem.write8(cal + at + i, value)


def sensor(name, value, quality=1):
    at = base + layout['sensors'] + layout[f'Sensors.{name}']
    mem.write16(at + layout['Sensor.value'], value & 65535)
    mem.write8(at + layout['Sensor.quality'], quality)


def setup():
    target.call('ecu_init', {8: 1})
    target.call('cal_example', pointer(cal, 8))
    mem.write16(rotation + layout['Rotation.rpm'], 1000)
    mem.write16(plan + layout['EnginePlan.rpm'], 1000)
    mem.write8(control + layout['Controls.mode'], 1)
    sensor('iat', 0); sensor('map', 100); sensor('clt', 20); sensor('battery', 12000)


def fuel(now, expected, bounded=None):
    global checks
    # C166 uses R8..R12, then the user stack; the final pointer straddles it.
    mem.write16(0x383e00, cal >> 14)
    mem.write16(0x383e02, plan & 0x3fff)
    mem.write16(0x383e04, plan >> 14)
    target.call('fuel_plan', {8: now & 65535, 9: now >> 16,
        **pointer(rotation, 10), 12: cal & 0x3fff})
    actual = mem.read16(plan + layout['EnginePlan.requested_us'])
    assert actual == expected, (now, actual, expected)
    assert mem.read16(plan + layout['EnginePlan.pulse_us']) == (min(expected, 25000) if bounded is None else bounded)
    checks += 1


setup()
table(0x490, 80, wide=True)
fuel(0, 2850)
sensor('map', 50); fuel(0, 1850)
sensor('map', 0, 4); fuel(0, 0)
mem.write8(cal + 0x5D4, 1); fuel(0, 2850)
be16(0x5DF, 10000); fuel(0, 4850)
be16(0x5DF, 5000)
table(0x490, 400, wide=True); fuel(0, 10850)
assert mem.read16(control + layout['Controls.ve']) == 400
table(0x490, 0, wide=True); fuel(0, 0)
table(0x490, 80, wide=True)
sensor('iat', 100); fuel(0, (5000*27315//37315)*80//100//2 + 850)
sensor('iat', 0)
table(0, 100, 256); table(0x4B0, 150); table(0x4C0, 10)
mem.write8(control + layout['Controls.mode'], 2)
mem.write16(control + layout['Controls.running_at'], 0xff00)
mem.write16(control + layout['Controls.running_at'] + 2, 0xffff)
fuel(0xffffff00, 4600)
fuel(244, 3975)
fuel(744, 3350)
assert mem.read8(control + layout['Controls.afterstart']) == 100
table(0x4C0, 0); fuel(0xffffff00, 3350)
mem.write16(control + layout['Controls.ae_percent'], 150); fuel(744, 4600)
be16(0x5DF, 10000); fuel(744, 8350)
be16(0x5DF, 5000); table(0x4C0, 10); fuel(0xffffff00, 6475)
mem.write8(control + layout['Controls.mode'], 1); fuel(0xffffff00, 2850)
# Maximum valid VE/MAP/fuel demand must saturate, not wrap before dead time.
mem.write8(cal + 0x5D4, 0); be16(0x5DF, 20000)
table(0x490, 1000, wide=True); sensor('map', 600); sensor('iat', -40)
fuel(0, 60850, 25000)

setup()
mem.write8(control + layout['Controls.mode'], 2)
table(0x759, 200, 48); table(0x799, 50, 8); mem.write8(cal + 0x758, 1)


def ae(now, tps, expected, quality=1):
    global checks
    sensor('tps', tps, quality)
    mem.write16(0x383e00, cal >> 14)
    target.call('acceleration_update', {8: now & 65535, 9: now >> 16,
                10: 10, 11: 1000, 12: cal & 0x3fff})
    actual = mem.read16(control + layout['Controls.ae_percent'])
    assert actual == expected, (now, actual, expected)
    checks += 1


ae(0xffffff00, 0, 100)
ae(0xffffff0a, 100, 150)
ae(4, 100, 125)
ae(254, 100, 100)
table(0x799, 0, 8); ae(264, 200, 100)
table(0x799, 255, 8); table(0x759, 255, 48); ae(274, 300, 495)
ae(284, 300, 100, quality=4)
ae(294, 300, 100); ae(304, 300, 100)
print(f'PASS {checks} linked C167 fuel/AE cases: VE, multiplicative enrichment, wrap, saturation, neutral recovery')
