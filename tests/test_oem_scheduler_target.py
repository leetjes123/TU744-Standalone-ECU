"""Continuous linked standalone capture/compare execution on C167 peripherals.

Supplied engine plan and foreground heartbeat; real PEC, input edges, timers,
interrupt arbitration/vector entry/RETI and output pin transitions. No physical
clock, bus-wait-state, analog coil or complete foreground-load acceptance.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from oem_repo import oem_repo
sys.path.insert(0, str(oem_repo() / 'src'))
from c167re.emu.machine import Machine

PROFILE = 'stock-95080' if '--stock-95080' in sys.argv else 'engine-experimental'
BUILD = Path(os.environ.get('OEM_SCHEDULER_BUILD', ROOT / 'build' / PROFILE))
OUT = ROOT / 'build/oem-scheduler' / PROFILE
OUT.mkdir(parents=True, exist_ok=True)
listing = (BUILD / 'TU5JP.m66').read_text()
symbols = {n: int(a, 16) for a, n in re.findall(r'^\s+([0-9A-F]{6})H\s+(\w+)\s+LABEL', listing, re.M)}
variables = {n: int(a, 16) for a, n in re.findall(r'^\s+([0-9A-F]{6})H\s+(\w+)\s+VAR', listing, re.M)}
variables.update({n: int(a, 16) for a, n in re.findall(r'^\s+([0-9A-F]{6})H\s+SYMBOL\s+VAR\s+\S+\s+\S+\s+(\w+)', listing, re.M)})
image = bytearray(b'\xff' * 0x80000)
base = 0
hex_bytes = (BUILD / 'TU5JP.H86').read_bytes()
HEX_SHA256 = hashlib.sha256(hex_bytes).hexdigest()
for line in hex_bytes.decode().splitlines():
    b = bytes.fromhex(line[1:]); assert sum(b) & 255 == 0
    n, at, kind = b[0], int.from_bytes(b[1:3], 'big'), b[3]
    if kind == 0: image[base+at:base+at+n] = b[4:4+n]
    elif kind == 2: base = int.from_bytes(b[4:6], 'big') << 4
    elif kind == 4: base = int.from_bytes(b[4:6], 'big') << 16

FIELDS = {
    'Ecu': ['rotation', 'authority', 'control', 'milliseconds', 'foreground_stamp', 'cal', 'protocol'],
    'Rotation': ['state', 'tooth', 'epoch', 'cycle', 'normal', 'last', 'rpm', 'losses'],
    'Authority': ['plan', 'inhibits', 'epoch', 'coil_active', 'first_reason', 'spark_draining',
                  'feedback_missing', 'feedback_invalid', 'feedback_correction', 'late_events'],
    'EnginePlan': ['epoch', 'max_age_ms', 'stamp', 'dwell_us', 'pulse_us', 'trigger10',
                   'advance10', 'injection_phase10', 'spark_cut', 'fuel_cut', 'soft_spark', 'dwell_feedback'],
    'Controls': ['mode'],
    'TimingHealth': ['capture_resyncs', 'capture_reason'],
    'Calibration': ['bytes', 'valid', 'active', 'generation'],
    'Protocol': ['ring', 'head', 'tail', 'tx', 'tx_length'],
}
keys = [f'{s},{f}' for s, fields in FIELDS.items() for f in fields]
source = OUT / 'layout.c'
source.write_text('#include "ecu.h"\n#include <stddef.h>\nconst u16 layout[]={' +
                  ','.join(f'offsetof({k})' for k in keys) + '};\n')
keil = Path(os.environ.get('KEIL_C166', str(Path.home() / 'AppData/Local/Keil_v5/C166')))
r = subprocess.run([str(keil / 'BIN/C166.EXE'), str(source), 'LARGE', 'MOD167', 'SRC',
                    f'INCDIR({ROOT / "include"})'], cwd=OUT, capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
values = [int(v, 16) for v in re.findall(r'\bDW\s+([0-9A-F]+)H', source.with_suffix('.SRC').read_text())]
assert len(values) == len(keys)
layout = dict(zip(keys, values))


def put(m, at, value, width=2):
    for i in range(width): m.mem.write8(at + i, (value >> (8*i)) & 255)


def invoke(m, name, args=None):
    entry = symbols[name]
    m.cpu.reset(ip=entry & 65535, csp=entry >> 16)
    m.cpu.dpp = [0, 1, 0xE0, 3]; m.cpu.set_rw(0, 0xBE00)
    for reg, value in (args or {}).items(): m.cpu.set_rw(reg, value & 65535)
    stack = m.cpu.sp
    for _ in range(100000):
        if m.mem.read16(m.cpu.pc()) == 0xDB and m.cpu.sp == stack:
            assert not m.cpu.traps
            return
        m.step()
    raise AssertionError((name, hex(m.cpu.pc()), m.cpu.traps))


class Run:
    def __init__(self, rpm, dwell=500, pulse=1000, phase=840, origin=50000, traffic=False,
                 advance=0, cranking=False, feedback=False):
        self.m = m = Machine(image)
        self.ecu = variables['ecu']
        self.rotation = self.ecu + layout['Ecu,rotation']
        self.authority = self.ecu + layout['Ecu,authority']
        self.plan = self.authority + layout['Authority,plan']
        invoke(m, 'board_init'); invoke(m, 'ecu_init', {8: 1})
        invoke(m, 'board_knock_init')
        # Supplied initialization for a controlled run, not a full ECU boot.
        for node in m.intctl.regs:
            m.intctl.regs[node] &= ~128
        if not traffic:
            for node in ['T6IC', 'ADCIC', 'SSCRIC', 'CC22IC', 'S0RIC']:
                m.intctl.regs[node] &= ~64
        m.mem.write16(0xFE52, origin & 65535)
        m.mem.write16(0xF050, origin & 65535)
        m.mem.write16(variables['capture_high'], origin >> 16)
        self.field(self.ecu + layout['Ecu,control'], 'Controls', 'mode', 1 if cranking else 2, 1)
        for key, value in [('epoch', 1), ('max_age_ms', 1000), ('dwell_us', dwell),
                           ('pulse_us', pulse), ('trigger10', 1140), ('advance10', 0),
                           ('advance10', advance), ('injection_phase10', phase)]: self.field(self.plan, 'EnginePlan', key, value)
        self.field(self.plan, 'EnginePlan', 'dwell_feedback', int(feedback), 1)
        m.mem.write16(0xE600, 0xFF0D)  # controlled idle loop in unused test RAM
        m.cpu.csp = 0; m.cpu.ip = 0xE600; m.cpu.psw = 0x800
        m.gen['gen0'].configure(teeth=60, missing=2, rpm=rpm, enabled=True,
                                pins=[('P2', 15)], count_timer='T0')
        self.first_tooth = m.cpu.cycles + 1.5 * m.gen['gen0'].slot_cycles()
        m.ports.events.clear()
        self.rpm, self.dwell, self.traffic = rpm, dwell, traffic
        self.pulse, self.advance, self.cranking, self.origin = pulse, advance, cranking, origin
        self.enabled = False
        self.steps = 0
        self.max_stack = self.max_user = 0
        self.max_masked = 0
        self.masked_at = None
        step = m.cpu.step
        def observed_step():
            step()
            self.steps += 1
            self.max_stack = max(self.max_stack, 0xFC00 - m.cpu.sp)
            self.max_user = max(self.max_user, 0xBE00 - m.cpu.rw(0))
            if not (m.cpu.psw_rest & 0x800):
                if self.masked_at is None: self.masked_at = m.cpu.cycles
            elif self.masked_at is not None:
                self.max_masked = max(self.max_masked, m.cpu.cycles - self.masked_at)
                self.masked_at = None
        m.cpu.step = observed_step

    def field(self, base, struct, name, value, width=2):
        put(self.m, base + layout[f'{struct},{name}'], value, width)

    def value(self, base, struct, name, width=2):
        return sum(self.m.mem.read8(base + layout[f'{struct},{name}'] + i) << (i*8) for i in range(width))

    def run_revolutions(self, n, allow_faults=False):
        return self.run_until(lambda: self.m.gen['gen0'].revolutions >= n, allow_faults)

    def run_until(self, stop, allow_faults=False, breakpoints=(), on_break=None):
        m = self.m
        bps = set(breakpoints)
        while not stop():
            # Only emulate publication while the foreground could run. Never
            # overwrite an inhibit inside a preempted ISR.
            if m.cpu.pc() == 0xE600:
                now = self.value(self.ecu, 'Ecu', 'milliseconds', 4)
                self.field(self.ecu, 'Ecu', 'foreground_stamp', now, 4)
                self.field(self.plan, 'EnginePlan', 'stamp', now, 4)
                if not self.enabled and self.value(self.rotation, 'Rotation', 'state', 1) == 2:
                    self.field(self.authority, 'Authority', 'inhibits', 0)
                    epoch = self.value(self.authority, 'Authority', 'epoch')
                    self.field(self.plan, 'EnginePlan', 'epoch', epoch)
                    self.enabled = True
                if hasattr(self, 'foreground_hook'): self.foreground_hook()
                # No work in this supplied foreground: advance to the next
                # hardware event, retaining continuous execution inside ISRs.
                # Never skip time over a pending enabled request: the idle
                # breakpoint returns before the controller polls, and a
                # skipped-to event at a higher level would otherwise starve a
                # lower-level deferred worker (XP1) that a CPU takes at once.
                deadline = m._next_deadline()
                pending = any((v & 0xC0) == 0xC0 for v in m.intctl.regs.values())
                if not pending and deadline is not None and deadline > m.cpu.cycles:
                    m.cpu.cycles = deadline
            result = m.run(max_instructions=1024, breakpoints=bps | {0xE600}, stop_on_trap=True)
            assert result in ['budget', 'breakpoint'], result
            assert not m.cpu.traps, m.cpu.traps
            if result == 'breakpoint' and m.cpu.pc() in bps:
                if on_break is not None:
                    on_break(self)
                m.step()
            if self.enabled and not allow_faults and self.value(self.authority, 'Authority', 'inhibits'):
                raise AssertionError(self.report())
            assert self.steps < 30000000, self.report()
        return self.report()

    def report(self):
        m = self.m
        outputs = {f'{p}.{pin}': [v for _, port, bit, v in m.ports.events if (port, bit) == (p, pin)]
                   for p, pin in [('P2', 0), ('P2', 1), ('P7', 6), ('P7', 5), ('P7', 4), ('P8', 7)]}
        widths, fire_errors = {}, {}
        slot = m.gen['gen0'].slot_cycles()
        for port, pin in [('P2', 0), ('P2', 1), ('P7', 6), ('P7', 5), ('P7', 4), ('P8', 7)]:
            edges = [(t, v) for t, p, b, v in m.ports.events if (p, b) == (port, pin)]
            widths[f'{port}.{pin}'] = [(t1-t0)/20 for (t0,v0),(t1,v1) in zip(edges,edges[1:]) if v0 == 0 and v1 == 1]
            if port == 'P2':
                target = ((1140 - self.advance + pin * 1800) % 3600) / 60
                fire_errors[pin] = [((t-self.first_tooth-target*slot+30*slot) % (60*slot)-30*slot)/20
                                    for t,v in edges if v == 1]
        return dict(rpm=self.rpm, dwell_us=self.dwell, traffic=self.traffic,
                    pulse_us=self.pulse, origin=self.origin, advance10=self.advance, cranking=self.cranking,
                    revolutions=m.gen['gen0'].revolutions, instructions=self.steps,
                    inhibits=self.value(self.authority, 'Authority', 'inhibits'),
                    tooth=self.value(self.rotation, 'Rotation', 'tooth', 1),
                    state=self.value(self.rotation, 'Rotation', 'state', 1),
                    epoch=self.value(self.rotation, 'Rotation', 'epoch'),
                    losses=self.value(self.rotation, 'Rotation', 'losses', 4),
                    capture_blocks=m.mem.read16(variables['capture_blocks']),
                    capture_overruns=m.mem.read16(variables['capture_overruns']),
                    pec_transfers=m.pec.by_channel[6], irq_count=m.itc.taken,
                    outputs=outputs, pc=hex(m.cpu.pc()), stack=self.max_stack, user=self.max_user,
                    max_masked_cycles=self.max_masked, pulse_widths_us=widths,
                    fire_error_us=fire_errors)


if __name__ == '__main__':
    reports = []
    cases = [dict(rpm=r, dwell=d, traffic=t) for r in [1000, 4000, 10000]
             for d in [500, 3000] for t in [False, True]]
    cases += [dict(rpm=200, dwell=3000, cranking=True),
              dict(rpm=10000, dwell=6000, traffic=True),
              dict(rpm=10000, dwell=500, traffic=True, origin=0xFFFFFF00, advance=350, phase=120),
              dict(rpm=4000, dwell=3000, traffic=True, origin=65520, advance=-100, phase=1620)]
    for args in ([dict(rpm=10000)] if '--probe' in sys.argv else cases):
        run = Run(**args)
        report = run.run_revolutions(6)
        print(report, flush=True)
        assert report['capture_overruns'] == report['inhibits'] == 0
        assert report['pec_transfers'] > report['capture_blocks'] if run.rpm > 1000 else True
        assert report['max_masked_cycles'] < 2000, report
        for edges in report['outputs'].values():
            assert len(edges) >= 4 and edges[:4] == [0, 1, 0, 1], report
        for pin, widths in report['pulse_widths_us'].items():
            assert widths, report
            if pin.startswith('P2'):
                assert all(0 < w < 6500 for w in widths), report
            else:
                assert all(abs(w-run.pulse) <= 2 for w in widths), report
        if not run.cranking:
            # OEM segment scheduling (sub_37CA0): a coil whose dwell spans more
            # than its fire position is charged during the previous segment. On
            # the first segment after enabling there was none, so the ROM
            # charges at once and fires by time (381A4, FD1C.13). Only that
            # first spark per coil may be late; every later one must be exact.
            # A dwell longer than one segment plus the fire position cannot be
            # started in time; the ROM keeps the dwell by moving the next fire
            # to min(dwell - 240, 192) counts after its boundary (38332).
            count_us = 60e6 / run.rpm / 480
            dwell_counts = min(run.dwell / count_us, 464)
            excess = max(0.0, min(dwell_counts - 240, 192) - (144 - run.advance / 7.5)) * count_us
            for errors in report['fire_error_us'].values():
                assert all(-run.m.gen['gen0'].slot_cycles()/160-10 <= e - excess <= 100 for e in errors[1:]), report
                assert -run.m.gen['gen0'].slot_cycles()/160-10 <= errors[0], report
        reports.append(report)
    assert hashlib.sha256((BUILD / 'TU5JP.H86').read_bytes()).hexdigest() == HEX_SHA256, 'image changed during run'
    (OUT / f'{PROFILE}-live.json').write_text(json.dumps(dict(scope=__doc__,
        hex_sha256=HEX_SHA256,
        reports=reports, physical_acceptance=False), indent=2) + '\n')
