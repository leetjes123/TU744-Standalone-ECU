"""Audit regressions in the actual output-enabled C166 image.

Timers are scripted. Instruction costs are not physical WCET acceptance.
"""
from pathlib import Path
import json
import os
import re
import subprocess
import sys

if '--stock-95080' not in sys.argv:
    sys.argv.append('--engine-experimental')
import test_target as target

ROOT = target.ROOT
OUT = ROOT / 'build/audit-regressions-target'
OUT.mkdir(parents=True, exist_ok=True)
fields = [
    'offsetof(Ecu,rotation)', 'offsetof(Ecu,authority)', 'offsetof(Ecu,cal)',
    'offsetof(Ecu,key_input)', 'offsetof(Calibration,valid)',
    'offsetof(Rotation,normal)', 'offsetof(Rotation,state)', 'offsetof(Rotation,tooth)',
    'offsetof(Authority,plan)', 'offsetof(Authority,inhibits)',
    'offsetof(EnginePlan,epoch)', 'offsetof(EnginePlan,max_age_ms)',
    'offsetof(EnginePlan,dwell_us)', 'offsetof(EnginePlan,trigger10)',
    'offsetof(EnginePlan,injection_phase10)', 'offsetof(EnginePlan,pulse_us)',
    'offsetof(PhaseObservation,captured_at)', 'offsetof(PhaseObservation,armed_at)',
    'offsetof(PhaseObservation,armed)', 'offsetof(PhaseObservation,delay)',
    'offsetof(Rotation,last)', 'offsetof(Rotation,seen)',
    'offsetof(Rotation,have_gap)', 'offsetof(Rotation,gap_stamp)',
    'offsetof(Ecu,control)', 'offsetof(Controls,mode)',
    'offsetof(Authority,epoch)', 'offsetof(EnginePlan,advance10)',
    'offsetof(MisfireObservation,seeded)', 'offsetof(MisfireObservation,boundary)',
    'offsetof(MisfireObservation,boundary_eligible)',
]
source = OUT / 'layout.c'
source.write_text('#include "diagnostic_monitors.h"\n#include <stddef.h>\nconst u16 layout[]={' + ','.join(fields) + '};\n')
keil = Path(os.environ.get('KEIL_C166', str(Path.home() / 'AppData/Local/Keil_v5/C166')))
result = subprocess.run([str(keil / 'BIN/C166.EXE'), str(source), 'LARGE', 'MOD167', 'SRC',
                         f'INCDIR({ROOT / "include"})'], cwd=OUT, capture_output=True, text=True)
assert result.returncode == 0, result.stdout + result.stderr
values = [int(v, 16) for v in re.findall(r'\bDW\s+([0-9A-F]+)H', source.with_suffix('.SRC').read_text())]
assert len(values) == len(fields)
layout = dict(zip(fields, values))
listing = (target.BUILD / 'TU5JP.m66').read_text()
variables = {name: int(at, 16) for at, name in re.findall(r'^\s+([0-9A-F]{6})H\s+(\w+)\s+VAR', listing, re.M)}
regs = {name: int(at, 16) for name, at in re.findall(r'^sfr\s+(\w+)\s*=\s*0x([0-9A-Fa-f]+)', (ROOT / 'target/c167/reg167.h').read_text(), re.M)}
cpu, mem = target.cpu, target.mem
base = variables['ecu']
rotation = base + layout['offsetof(Ecu,rotation)']
authority = base + layout['offsetof(Ecu,authority)']
plan = authority + layout['offsetof(Authority,plan)']
phase = variables['phase_observation']
target.call('ecu_init', {8: 1})
ram = next(device.data for device in mem.devices if device.name == 'ram')
reset_ram = bytes(ram)


def put(at, value, width=2):
    for i in range(width):
        mem.write8(at+i, (value >> (8*i)) & 255)


def get32(at):
    return mem.read16(at) | (mem.read16(at+2) << 16)


def field(at, type_name, name, value, width=2):
    put(at + layout[f'offsetof({type_name},{name})'], value, width)


def run(name, arguments=None, inject=None, isr=False):
    entry = target.symbols[name]
    cpu.reset(ip=entry & 65535, csp=entry >> 16)
    cpu.dpp = [0, 1, 0xE0, 3]
    cpu.set_rw(0, 0xBE00)
    cpu.psw = 0x9800 if isr else 0x0800
    for reg, value in (arguments or {}).items():
        cpu.set_rw(reg, value)
    stack, start_cycles, pending = cpu.sp, cpu.cycles, False
    min_sp, min_user = stack, cpu.rw(0)
    for step in range(10000):
        if step == inject:
            # The timer can wrap even with IEN=0; service must wait for IEN.
            put(regs['T1'], 10)
            put(regs['T1IC'], 0x80)
            pending = True
        if pending and cpu.psw & 0x800:
            put(variables['capture_high'], 1)
            put(regs['T1IC'], 0)
            pending = False
        if mem.read16(cpu.pc()) == (0x88FB if isr else 0x00DB) and cpu.sp == stack:
            assert not cpu.traps and cpu.rw(0) == 0xBE00
            return {'instructions': step, 'modeled_cpu_cycles': cpu.cycles - start_cycles,
                    'system_stack_bytes': stack - min_sp, 'user_stack_bytes': 0xBE00 - min_user}
        cpu.step()
        min_sp, min_user = min(min_sp, cpu.sp), min(min_user, cpu.rw(0))
    raise AssertionError((name, cpu.pc(), cpu.traps))


def phase_case(inject=None):
    ram[:] = reset_ram
    field(phase, 'PhaseObservation', 'armed', 1, 1)
    field(phase, 'PhaseObservation', 'armed_at', 65400, 4)
    put(variables['capture_high'], 0)
    put(regs['CC8'], 65500)
    put(regs['T1'], 65520)
    put(regs['T1IC'], 0)
    stats = run('phase_isr', inject=inject, isr=True)
    assert get32(phase + layout['offsetof(PhaseObservation,captured_at)']) == 65500
    assert get32(phase + layout['offsetof(PhaseObservation,delay)']) == 100
    return stats


stats = phase_case()
for point in range(stats['instructions'] + 1):
    phase_case(point)

# The real HAL cannot reassert pump/heaters behind foreground fault gating.
for reason in [0, 2, 16, 32, 64, 128, 256]:
    ram[:] = reset_ram
    field(base, 'Ecu', 'key_input', 1, 1)
    field(base + layout['offsetof(Ecu,cal)'], 'Calibration', 'valid', 1, 1)
    field(authority, 'Authority', 'inhibits', reason)
    run('hal_aux', {8: 1, 9: 1, 10: 0, 11: 0, 12: 3, 13: 0})
    assert (mem.read16(regs['P6']) & 0x14) == (0x14 if reason else 0), reason
    assert bool(mem.read16(regs['P8']) & 2) == bool(reason), reason

timings = []
for period in [125, 156, 1250, 12500]:
    for tooth in range(58):
        ram[:] = reset_ram
        field(rotation, 'Rotation', 'state', 2, 1)
        field(rotation, 'Rotation', 'tooth', tooth, 1)
        field(rotation, 'Rotation', 'normal', period, 4)
        field(authority, 'Authority', 'inhibits', 0)
        for name, value in [('epoch', 1), ('max_age_ms', 30), ('dwell_us', 3000),
                            ('trigger10', 1140), ('injection_phase10', 1620), ('pulse_us', 1000)]:
            field(plan, 'EnginePlan', name, value)
        put(variables['capture_high'], 0)
        put(regs['T1'], 1000)
        put(regs['T1IC'], 0)
        put(regs['T7'], 1000)
        observation = run('board_schedule', {8: 1000, 9: 0})
        timings.append({'normal_period_ticks': period, 'tooth': tooth, **observation})

report = {'profile': target.BUILD.name, 'phase_race_boundaries_passed': stats['instructions'] + 1,
          'hal_aux_fault_masks_passed': 7, 'schedule_observations': timings,
          'scope': 'Actual linked instructions; scripted timers and modeled costs, NOT physical WCET.'}
(OUT / f'{target.BUILD.name}.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'PASS {stats["instructions"] + 1} phase-overflow boundaries, 7 HAL gates and {len(timings)} scheduler paths')
print('High-RPM schedule maximum:', max((t for t in timings if t['normal_period_ticks'] == 125), key=lambda t: t['modeled_cpu_cycles']))
