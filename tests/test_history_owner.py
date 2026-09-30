"""Actual Keil journal/owner code with an explicit synchronous EEPROM model.

Only HAL EEPROM transfers, service admission and boot watchdog feeding are
substituted. This verifies pointer ABI and persistence state transitions, not
SSC timing, physical EEPROM behavior, reset-cause policy or a diagnostic task.
"""
from pathlib import Path
import os
import re
import subprocess
import test_target as target

root = target.ROOT
out = root / 'build/history-owner'
out.mkdir(parents=True, exist_ok=True)
keil = Path(os.environ.get('KEIL_C166', str(Path.home() / 'AppData/Local/Keil_v5/C166')))
fields = {
    'ecu_size': 'sizeof(Ecu)',
    'lease': 'offsetof(Ecu,storage_owner)',
    'owner_size': 'sizeof(OemHistoryOwner)',
    'phase': 'offsetof(OemHistoryOwner,journal)+offsetof(OemHistory,phase)',
    'result': 'offsetof(OemHistoryOwner,journal)+offsetof(OemHistory,result)',
    **{n: f'offsetof(OemHistoryOwner,{n})' for n in
       ['ready', 'dirty', 'clear_pending', 'clear_durable']},
    'state_size': 'sizeof(OemDiagnostics)',
    'timestamp': 'offsetof(OemDiagnostics,events)+offsetof(OemDtcState,timestamp)',
    **{n: f'offsetof(OemDiagnostics,events)+offsetof(OemDtcState,{n})' for n in
       ['gate', 'clear_request', 'clear_inverse', 'clear_mode', 'live']},
    'count': 'offsetof(OemDiagnostics,events)+offsetof(OemDtcState,store)+offsetof(OemDtcRecords,count)',
    'records': 'offsetof(OemDiagnostics,events)+offsetof(OemDtcState,store)+offsetof(OemDtcRecords,records)',
    'iat_fail': 'offsetof(OemDiagnostics,iat)+offsetof(OemIat,fail_count)',
    'iat_status': 'offsetof(OemDiagnostics,iat)+offsetof(OemIat,status)',
    'voltage_fail': 'offsetof(OemDiagnostics,voltage)+offsetof(OemVoltage,fail_count)',
    'mil_state': 'offsetof(OemDiagnostics,mil)+offsetof(OemMil,state)',
    'mil_retained': 'offsetof(OemDiagnostics,mil)+offsetof(OemMil,retained)',
    **{'readiness_' + n: f'offsetof(OemDiagnostics,readiness)+offsetof(OemReadiness,{n})'
       for n in ['count', 'supported', 'pending', 'once']},
}
probe = out / 'layout.c'
probe.write_text('#include "oem_history.h"\n#include <stddef.h>\nconst u16 layout[]={' +
                 ','.join(fields.values()) + '};\n')
result = subprocess.run([str(keil / 'BIN/C166.EXE'), str(probe), 'LARGE', 'MOD167', 'SRC',
                         f'INCDIR({root / "include"})'], cwd=out, capture_output=True, text=True)
(out / 'build.log').write_text(result.stdout + result.stderr)
assert result.returncode == 0 and '0 WARNING(S),  0 ERROR(S)' in result.stdout, result.stdout
values = [int(v, 16) for v in re.findall(r'\bDW\s+([0-9A-F]+)H', probe.with_suffix('.SRC').read_text())]
assert len(values) == len(fields), values
layout = dict(zip(fields, values))
listing = (target.BUILD / 'TU5JP.m66').read_text()
ecu = int(re.search(r'^\s+([0-9A-F]{6})H\s+ecu\s+VAR', listing, re.M)[1], 16)
owner, other, state = 0x384000, 0x384400, 0x385000
assert owner + layout['owner_size'] <= other
assert other + layout['owner_size'] <= state
assert state + layout['state_size'] < 0x387000
cpu, mem = target.cpu, target.mem
eeprom = bytearray(b'\xff' * 8192)
busy, fail_write, service_ok = 0, False, True
writes = 0


def pointer(register):
    return (cpu.rw(register + 1) << 14) + cpu.rw(register)


def hook(name):
    global writes
    value = 1
    if name == 'hal_eeprom_busy':
        value = busy
    elif name in ('hal_eeprom_read', 'hal_eeprom_write'):
        at, address, length = cpu.rw(8), pointer(9), cpu.rw(11) & 255
        assert 0 < length <= 32 and at + length <= len(eeprom), (name, at, address, length)
        if name == 'hal_eeprom_read':
            for i in range(length):
                mem.write8(address + i, eeprom[at + i])
        else:
            assert (at & 31) + length <= 32
            writes += 1
            value = int(not fail_write)
            if value:
                eeprom[at:at + length] = bytes(mem.read8(address + i) for i in range(length))
    elif name == 'service_enter':
        value = int(service_ok)
    cpu.set_rw(4, value)


hooks = {target.symbols[n]: n for n in
         ['hal_eeprom_busy', 'hal_eeprom_read', 'hal_eeprom_write',
          'service_enter', 'hal_watchdog_service']}
saved = {at: bytes(target.image[at:at + 2]) for at in hooks}


def call(name, registers):
    entry = target.symbols[name]
    cpu.reset(ip=entry & 65535, csp=entry >> 16)
    cpu.dpp = [0, 1, 0xE0, 3]
    cpu.set_rw(0, 0xBE00)
    for r, v in registers.items():
        if r == 13:
            # C166 passes only five argument words in R8..R12. The high
            # word of save's third argument arrives on the user stack.
            mem.write16(0x383E00, v & 65535)
        else:
            cpu.set_rw(r, v & 65535)
    stack = cpu.sp
    for _ in range(400000):
        if cpu.pc() in hooks:
            hook(hooks[cpu.pc()])
        if mem.read16(cpu.pc()) == 0xDB and cpu.sp == stack:
            assert not cpu.traps and cpu.rw(0) == 0xBE00, (name, cpu.traps)
            return cpu.rw(4) & 255
        cpu.step()
    raise AssertionError((name, hex(cpu.pc()), cpu.traps))


def ptr(address, register=8):
    return {register: address & 0x3FFF, register + 1: address >> 14}


def owner_call(name, address=owner, now=0, kind=0, event=0):
    registers = ptr(address)
    if name in ('load', 'save', 'clear_request', 'clear_service'):
        registers.update(ptr(state, 10))
    if name == 'clear_request':
        registers.update({12: kind, 13: event})
    if name in ('save', 'poll'):
        register = 12 if name == 'save' else 10
        registers.update({register: now & 65535, register + 1: now >> 16})
    return call('oem_history_owner_' + name, registers)


def status(name, address=owner):
    return mem.read8(address + layout[name])


def finish(now=0):
    for i in range(200):
        if not status('phase'):
            return
        owner_call('poll', now=(now + i) & 0xFFFFFFFF)
    raise AssertionError('writer did not finish')


def zero(address, size):
    for i in range(size):
        mem.write8(address + i, 0)


def state_bytes():
    return bytes(mem.read8(state + i) for i in range(layout['state_size']))


def owner_bytes():
    return bytes(mem.read8(owner + i) for i in range(layout['owner_size']))


def seed_clear():
    zero(state, layout['state_size'])
    mem.write16(state + layout['gate'], 32)
    mem.write16(state + layout['clear_inverse'], 65535)
    mem.write8(state + layout['count'], 1)
    # A stored IAT record with a fault descriptor, retained by the real codec.
    mem.write8(state + layout['records'], 0x5B)
    mem.write16(state + layout['records'] + 2, 3)
    mem.write16(state + layout['live'] + 2*0x5B, 3)
    mem.write16(state + layout['iat_status'], 4)
    mem.write8(state + layout['mil_state'], 0xFF)
    mem.write8(state + layout['mil_retained'], 0xFF)
    for i in range(5): mem.write8(state + layout['readiness_count'] + i, 41+i)
    mem.write8(state + layout['readiness_supported'], 0x6D)


cases = 0
try:
    # Patch only private emulator memory with RETS; never alter the linked HEX.
    for at in hooks:
        target.image[at:at + 2] = b'\xdb\x00'
    zero(ecu, layout['ecu_size'])
    zero(owner, layout['owner_size'])
    zero(other, layout['owner_size'])
    zero(state, layout['state_size'])
    assert owner_call('load') == 0 and status('ready') and status('dirty')
    assert owner_call('save') == 1
    assert owner_call('load', other) == 0 and not status('ready', other)
    assert call('storage_claim', ptr(other)) == 0
    call('storage_release', ptr(other))
    assert owner_call('save', other) == 0
    finish()
    assert owner_call('settled') == 1 and status('result') == 1
    cases += 1
    for clear_again in (False, True):
        for phase in range(1, 9):
            mem.write16(state + layout['timestamp'], 99)
            owner_call('cleared')
            assert owner_call('save') == 1
            for i in range(200):
                if status('phase') == phase:
                    break
                owner_call('poll', now=i)
            assert status('phase') == phase
            mem.write16(state + layout['timestamp'], 123)
            owner_call('cleared' if clear_again else 'changed')
            finish()
            assert status('result') == 1 and status('dirty')
            assert status('clear_pending') == int(clear_again)
            assert status('clear_durable') == int(not clear_again)
            assert owner_call('settled') == 0
            zero(other, layout['owner_size'])
            assert owner_call('load', other) == 1
            assert mem.read16(state + layout['timestamp']) == 99
            mem.write16(state + layout['timestamp'], 123)
            assert owner_call('save') == 1
            finish()
            assert owner_call('settled') == 1 and status('clear_durable')
            zero(other, layout['owner_size'])
            assert owner_call('load', other) == 1
            assert mem.read16(state + layout['timestamp']) == 123
            cases += 1
    for failure in ('admission', 'write', 'timeout'):
        owner_call('cleared')
        if failure == 'admission':
            service_ok = False
            assert owner_call('save') == 0
            service_ok = True
        else:
            assert owner_call('save', now=0xFFFFFF00) == 1
            fail_write = failure == 'write'
            busy = int(failure == 'timeout')
            owner_call('poll', now=(0xFFFFFF00 + (10000 if busy else 1)) & 0xFFFFFFFF)
            assert status('result') == (2 if busy else 3), (failure, status('result'), status('phase'))
            fail_write, busy = False, 0
        assert not status('phase') and status('dirty') and status('clear_pending')
        assert not status('clear_durable')
        assert call('storage_claim', ptr(other)) == 1
        call('storage_release', ptr(other))
        assert owner_call('save') == 1
        finish()
        assert owner_call('settled') == 1 and status('clear_durable')
        cases += 1
    before = writes
    assert owner_call('save') == 0 and writes == before

    # Real request -> producer resets -> record clear -> durable snapshot,
    # including C166's stack-passed fourth byte argument for a single event.
    for kind in range(3):
        for phase in range(1, 9):
            seed_clear()
            owner_call('changed')
            assert owner_call('save') == 1
            for i in range(200):
                if status('phase') == phase:
                    break
                owner_call('poll', now=i)
            assert status('phase') == phase
            assert owner_call('clear_request', kind=kind, event=0x5B) == 1
            request = 0x5B if kind == 2 else 106
            assert mem.read16(state + layout['clear_request']) == request
            assert mem.read16(state + layout['clear_inverse']) == request ^ 65535
            pending = state_bytes(), owner_bytes()
            assert owner_call('clear_request', kind=kind, event=0x5B) == 0
            assert (state_bytes(), owner_bytes()) == pending
            assert owner_call('clear_service') == 1
            assert mem.read16(state + layout['clear_request']) == 0
            assert mem.read16(state + layout['clear_inverse']) == 65535
            assert mem.read8(state + layout['iat_fail']) == 20
            assert not mem.read16(state + layout['iat_status']) & 4
            assert mem.read8(state + layout['mil_state']) == 0xB9
            assert mem.read8(state + layout['mil_retained']) == 0xFE
            assert mem.read8(state + layout['records']) == 0
            assert all(mem.read8(state + layout['readiness_count'] + i) == 0 for i in range(5))
            assert mem.read8(state + layout['readiness_pending']) == 0x6D
            assert mem.read8(state + layout['readiness_once']) == 255
            assert mem.read8(state + layout['voltage_fail']) == (5 if kind == 0 else 0)
            assert status('clear_pending') and not status('clear_durable')
            completed = state_bytes(), owner_bytes()
            assert owner_call('clear_service') == 0
            assert (state_bytes(), owner_bytes()) == completed
            finish()
            assert status('dirty') and status('clear_pending') and not status('clear_durable')
            # The earlier successful write still contains the uncleared row.
            zero(other, layout['owner_size'])
            assert owner_call('load', other) == 1
            assert mem.read8(state + layout['records']) == 0x5B
            assert all(mem.read8(state + layout['readiness_count'] + i) == 41+i for i in range(5))
            assert mem.read8(state + layout['readiness_pending']) == 0
            for i, value in enumerate(completed[0]):
                mem.write8(state + i, value)
            assert owner_call('save') == 1
            finish()
            assert owner_call('settled') == 1 and status('clear_durable')
            zero(other, layout['owner_size'])
            assert owner_call('load', other) == 1
            assert mem.read8(state + layout['records']) == 0
            assert mem.read8(state + layout['mil_retained']) == 0xFE
            assert all(mem.read8(state + layout['readiness_count'] + i) == 0 for i in range(5))
            assert mem.read8(state + layout['readiness_pending']) == 0x6D
            cases += 1

    # Invalid/unready/duplicate service must not acknowledge a pending clear,
    # refresh shared aliases or touch any byte of diagnostic/history state.
    for failure in ('unready', 'empty', 'gate', 'inverse', 'request', 'mode', 'count', 'event', 'config'):
        seed_clear()
        assert owner_call('clear_request') == 1
        if failure == 'unready': mem.write8(owner + layout['ready'], 0)
        elif failure == 'empty':
            mem.write16(state + layout['clear_request'], 0)
            mem.write16(state + layout['clear_inverse'], 65535)
        elif failure == 'gate': mem.write16(state + layout['gate'], 0)
        elif failure == 'inverse': mem.write16(state + layout['clear_inverse'], 0)
        elif failure == 'request':
            mem.write16(state + layout['clear_request'], 107)
            mem.write16(state + layout['clear_inverse'], 107 ^ 65535)
        elif failure == 'mode': mem.write16(state + layout['clear_mode'], 2)
        elif failure == 'count': mem.write8(state + layout['count'], 21)
        elif failure == 'event': mem.write8(state + layout['records'], 107)
        elif failure == 'config': mem.write8(state + layout['records'] + 5, 38)
        before = state_bytes(), owner_bytes()
        assert owner_call('clear_service') == 0, failure
        assert (state_bytes(), owner_bytes()) == before, failure
        mem.write8(owner + layout['ready'], 1)
        cases += 1

    for kind, event in [(3, 1), (2, 0), (2, 106), (2, 255)]:
        seed_clear()
        before = state_bytes(), owner_bytes()
        assert owner_call('clear_request', kind=kind, event=event) == 0
        assert (state_bytes(), owner_bytes()) == before
        cases += 1
    seed_clear()
    mem.write8(owner + layout['ready'], 0)
    before = state_bytes(), owner_bytes()
    assert owner_call('clear_request') == 0
    assert (state_bytes(), owner_bytes()) == before
    cases += 1
finally:
    for at, data in saved.items():
        target.image[at:at + 2] = data
print(f'PASS {cases} Keil history-owner scenarios with modeled EEPROM/admission')
