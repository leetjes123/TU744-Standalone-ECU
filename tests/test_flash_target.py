"""Execute linked flash HAL + relocated worker against an AMD command model.

Run after build.py stock-95080. This checks code execution and command ordering,
not the physical flash, bus timing or board power supply.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
if '--stock-95080' not in sys.argv: sys.argv.append('--stock-95080')
import test_target as target

root = target.ROOT
out = root / 'build/flash-test'
out.mkdir(parents=True, exist_ok=True)
keil = Path(os.environ.get('KEIL_C166', str(Path.home() / 'AppData/Local/Keil_v5/C166')))
fields = ['offsetof(Ecu,service)', 'offsetof(Ecu,rotation)', 'offsetof(Rotation,rpm)']
probe = out/'layout.c'
probe.write_text('#include "ecu.h"\n#include <stddef.h>\nconst u16 layout[]={' + ','.join(fields) + '};\n')
r = subprocess.run([str(keil/'BIN/C166.EXE'), str(probe), 'LARGE', 'MOD167', 'SRC',
                    'DEFINE(STOCK_95080=1)', f'INCDIR({root/"include"})'],
                   cwd=out, capture_output=True, text=True)
assert r.returncode == 0 and '0 WARNING(S),  0 ERROR(S)' in r.stdout, r.stdout
layout = [int(v,16) for v in re.findall(r'\bDW\s+([0-9A-F]+)H', probe.with_suffix('.SRC').read_text())]
listing = (target.BUILD/'TU5JP.m66').read_text()
ecu = int(re.search(r'^\s+([0-9A-F]{6})H\s+ecu\s+VAR', listing, re.M)[1],16)
mem, cpu = target.mem, target.cpu
original_read8, original_read16, original_write16 = mem.read8, mem.read16, mem.write16
state = 0
busy = None
force_failure = False
force_timeout = False
operations = []
checks = 0


def write16(at, value):
    global state, busy
    if at >= 0x80000 or 0xe000 <= at < 0x10000:
        return original_write16(at, value)
    assert 0x380000 <= cpu.pc() < 0x384000, ('flash command outside RAM', hex(cpu.pc()))
    if at == 0xaaa and value == 0xf0:
        state = 0; busy = None
        return
    expected = {0:(0xaaa,0xaa), 1:(0x554,0x55), 3:(0xaaa,0xaa), 4:(0x554,0x55)}
    if state in expected:
        assert (at,value) == expected[state], (state, hex(at),hex(value))
        state += 1
    elif state == 2:
        assert at == 0xaaa and value in (0xa0,0x80)
        state = 6 if value == 0xa0 else 3
    elif state == 5:
        assert at in (0x50000,0x60000) and value == 0x30
        busy = dict(at=at, value=0xffff, reads=3, erase=True)
        operations.append(('erase',at)); state = 0
    elif state == 6:
        assert 0x50000 <= at < 0x50c20 or 0x60000 <= at < 0x60c20, hex(at)
        busy = dict(at=at, value=value, reads=3, erase=False)
        operations.append(('program',at)); state = 0
    else: raise AssertionError(state)


def read8(at):
    if busy and at < 0x80000 and not 0xe000 <= at < 0x10000:
        raise AssertionError(('byte/ROM read while flash busy', hex(at), hex(cpu.pc())))
    return original_read8(at)


def read16(at):
    global busy
    if busy and at < 0x80000 and not 0xe000 <= at < 0x10000:
        assert at == busy['at'], ('ROM fetch/data while busy', hex(at),hex(cpu.pc()))
        assert 0x380000 <= cpu.pc() < 0x384000
        if force_timeout:
            # Accelerate the bounded polling counter without changing code.
            cpu.set_rw(7, 1); cpu.set_rw(12, 1)
            return (busy['value'] ^ 0x80) & 0xffdf
        if busy['reads']:
            busy['reads'] -= 1
            return (busy['value'] ^ 0x80) & 0xffdf
        if force_failure: return ((busy['value'] ^ 0x80) & 0xffdf) | 0x20
        value = busy['value']
        if busy['erase']: target.image[at:at+0x10000] = b'\xff'*0x10000
        else:
            target.image[at] &= value & 255
            target.image[at+1] &= value >> 8
        busy = None
    return original_read16(at)


mem.write16, mem.read16, mem.read8 = write16, read16, read8
target.call('ecu_init', {8:1})
assert target.call('hal_cal_erase',{8:0}) & 255 == 0
mem.write8(ecu + layout[0], 1)
source = 0x383400


def call(name, regs, expected):
    global checks
    result = target.call(name, regs, limit=1000000) & 255
    assert result == expected, (name,result,expected)
    assert not busy and state == 0
    checks += 1


for slot in [0,1]:
    at = 0x50000 + slot*0x10000
    target.image[at:at+3104] = bytes(3104)
    call('hal_cal_erase',{8:slot},1)
    assert target.image[at:at+3104] == b'\xff'*3104
    for i in range(32): mem.write8(source+i, (i*7)&255)
    args = {8:slot,9:32,10:source&0x3fff,11:source>>14,12:32}
    call('hal_cal_program',args,1)
    assert target.image[at+32:at+64] == bytes((i*7)&255 for i in range(32))
    for i in range(32): mem.write8(source+i,0)
    call('hal_cal_read',args,1)
    assert bytes(mem.read8(source+i) for i in range(32)) == bytes((i*7)&255 for i in range(32))
    call('hal_cal_read',{**args,9:3104},0)
    # Forbidden zero-to-one programming, address/length overflow and odd words.
    for i in range(32): mem.write8(source+i,255)
    call('hal_cal_program',args,0)
    for changes in [{8:2},{9:3104},{9:1},{12:1},{12:0},{12:34}]:
        call('hal_cal_program',{**args,**changes},0)
    force_failure = True
    call('hal_cal_erase',{8:slot},0)
    force_failure = False
    call('hal_cal_erase',{8:slot},1)
    force_failure = True
    mem.write8(source,0); mem.write8(source+1,0)
    call('hal_cal_program',{**args,12:2},0)
    force_failure = False
mem.write16(ecu+layout[1]+layout[2],1000)
call('hal_cal_erase',{8:0},0)
assert all(0x50000 <= at < 0x70000 for _,at in operations)
mem.write16(ecu+layout[1]+layout[2],0)
force_timeout = True
class ResetRequested(Exception): pass
def reset_requested(): raise ResetRequested()
cpu.on_reset = reset_requested
try:
    target.call('hal_cal_erase', {8:0}, limit=1000000)
    raise AssertionError('busy-flash timeout returned instead of requesting reset')
except ResetRequested:
    assert busy and 0x380000 <= cpu.pc() < 0x384000
    checks += 1
print(f'PASS {checks} linked stock flash HAL cases; RAM-only commands, both slots, DQ5 failures, timeout reset and bounds')
