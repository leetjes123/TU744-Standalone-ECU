"""Linked C166 sensor fault policy: preserve measurements, record, keep plans.

Raw ADC samples are explicit test inputs. No physical sensor or EEPROM model
is implied; the native fault suite exercises the journal and torn writes.
"""
from pathlib import Path
import os
import re
import subprocess
import test_target as target

root = target.ROOT
out = root / 'build/fault-policy'
out.mkdir(parents=True, exist_ok=True)
keil = Path(os.environ.get('KEIL_C166', str(Path.home() / 'AppData/Local/Keil_v5/C166')))
fields = {name: f'offsetof(Ecu,{name})' for name in ['cal', 'rotation', 'authority', 'sensors',
           'adc', 'milliseconds', 'key_input', 'control']}
for typ, members in [('Calibration', ['bytes', 'valid', 'generation']),
                     ('Rotation', ['normal', 'state', 'rpm', 'epoch']),
                     ('Authority', ['inhibits', 'plan']), ('EnginePlan', ['pulse_us']),
                     ('Controls', ['previous_key', 'mode']),
                     ('AdcSample', ['raw', 'stamp', 'generation', 'seen']),
                     ('Sensor', ['value', 'quality']),
                     ('Sensors', ['tps', 'clt', 'iat', 'battery', 'map']),
                     ('FaultState', ['active', 'stored'])]:
    fields.update({f'{typ}.{member}': f'offsetof({typ},{member})' for member in members})
fields['adc_size'] = 'sizeof(AdcSample)'
probe = out / 'layout.c'
probe.write_text('#include "faults.h"\n#include <stddef.h>\nconst u16 layout[]={' +
                 ','.join(fields.values()) + '};\n')
result = subprocess.run([str(keil / 'BIN/C166.EXE'), str(probe), 'LARGE', 'MOD167', 'SRC',
                         f'INCDIR({root / "include"})'], cwd=out, capture_output=True, text=True)
(out / 'build.log').write_text(result.stdout + result.stderr)
assert result.returncode == 0 and '0 WARNING(S),  0 ERROR(S)' in result.stdout, result.stdout
values = [int(v, 16) for v in re.findall(r'\bDW\s+([0-9A-F]+)H', probe.with_suffix('.SRC').read_text())]
assert len(values) == len(fields)
layout = dict(zip(fields, values))
listing = (target.BUILD / 'TU5JP.m66').read_text()
variables = {name: int(at, 16) for at, name in
             re.findall(r'^\s+([0-9A-F]{6})H\s+(\w+)\s+VAR', listing, re.M)}
mem = target.mem
base = variables['ecu']
rotation = base + layout['rotation']
cal = base + layout['cal'] + layout['Calibration.bytes']


def pointer(address, register):
    return {register: address & 0x3fff, register + 1: address >> 14}


def setup():
    target.call('ecu_init', {8: 1})
    target.call('cal_example', pointer(cal, 8))
    mem.write8(base + layout['cal'] + layout['Calibration.valid'], 1)
    mem.write16(base + layout['cal'] + layout['Calibration.generation'], 1)
    mem.write8(base + layout['key_input'], 1)
    mem.write8(base + layout['control'] + layout['Controls.previous_key'], 1)
    mem.write8(base + layout['control'] + layout['Controls.mode'], 2)
    mem.write8(rotation + layout['Rotation.state'], 2)
    mem.write16(rotation + layout['Rotation.rpm'], 1000)
    mem.write16(rotation + layout['Rotation.normal'], 1250)
    mem.write16(rotation + layout['Rotation.epoch'], 1)
    for channel, raw in [(8, 400), (10, 300), (11, 400), (5, 486), (0, 300), (6, 512)]:
        at = base + layout['adc'] + channel * layout['adc_size']
        mem.write16(at + layout['AdcSample.raw'], raw)
        mem.write16(at + layout['AdcSample.stamp'], 100)
        mem.write16(at + layout['AdcSample.generation'], 1)
        mem.write8(at + layout['AdcSample.seen'], 1)
    mem.write16(base + layout['milliseconds'], 100)


cases = [('tps', 8, 1000, 1), ('clt', 10, -30, 2), ('iat', 11, -30, 3),
         ('battery', 5, 28416, 4), ('map', 0, 300, 5)]
for name, channel, expected, fault_id in cases:
    setup()
    at = base + layout['adc'] + channel * layout['adc_size']
    mem.write16(at + layout['AdcSample.raw'], 1023)
    target.call('controls_update', {8: 100, 9: 0, **pointer(rotation, 10)})
    sensor = base + layout['sensors'] + layout[f'Sensors.{name}']
    assert mem.read16(sensor + layout['Sensor.value']) == expected & 65535, name
    assert mem.read8(sensor + layout['Sensor.quality']) == 4, name
    assert mem.read16(variables['faults'] + layout['FaultState.active']) == 1 << fault_id, name
    assert mem.read16(base + layout['authority'] + layout['Authority.inhibits']) == 0, name
    assert mem.read16(base + layout['authority'] + layout['Authority.plan'] + layout['EnginePlan.pulse_us']) > 0
setup()
target.call('sensors_update', {8: 100, 9: 0})
at = base + layout['adc'] + 8 * layout['adc_size']
mem.write16(at + layout['AdcSample.raw'], 100)
mem.write16(at + layout['AdcSample.stamp'], 110)
mem.write16(at + layout['AdcSample.generation'], 2)
mem.write16(base + layout['milliseconds'], 110)
target.call('sensors_update', {8: 110, 9: 0})
sensor = base + layout['sensors'] + layout['Sensors.tps']
assert mem.read16(sensor + layout['Sensor.value']) != 55
mem.write16(base + layout['milliseconds'], 200)
target.call('sensors_update', {8: 200, 9: 0})
assert mem.read16(sensor + layout['Sensor.value']) == 55
assert mem.read8(sensor + layout['Sensor.quality']) == 2
print('PASS 6 linked C166 sensor fault cases: measured rail/stale values, DTC recording, uninhibited plans')
