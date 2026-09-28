"""Linked-image regressions for R-01..04 and R-07 of the September 23 audit.

Run directly after building engine-experimental, or add --stock-95080.
--case fuel|gap|race|noise|tuning selects a group. OEM_SCHEDULER_BUILD can
select preserved HEX/map files for a failing negative control. The supplied
plan/heartbeat, 20 MHz emulator and digital pin edges do not qualify hardware.
"""
import argparse
import hashlib
import json
import re

from test_oem_scheduler_target import (Run, BUILD, OUT, PROFILE, HEX_SHA256,
                                       image, invoke, layout, put, symbols, listing)

PINS = [('P2', 0), ('P2', 1), ('P7', 6), ('P7', 5), ('P7', 4), ('P8', 7)]


def edges(r, pin):
    return [(t, v) for t, p, b, v in r.m.ports.events if (p, b) == pin]


def continuity(window=12, **args):
    r = Run(**args)
    r.run_revolutions(6)
    start = r.m.cpu.cycles
    r.run_revolutions(6 + window)
    end = r.m.cpu.cycles
    report = r.run_revolutions(8 + window)  # drain pulses begun in the observation window
    counts = {}
    for pin in PINS:
        e = edges(r, pin)
        # Spark belongs to its firing revolution. Missing CC9 feedback moves
        # charge starts across the observation boundary as dwell adapts.
        coil = pin[0] == 'P2'
        events = [t for t, v in e if start <= t < end and v == (1 if coil else 0)]
        pulses = [(a, z) for (a, v), (z, w) in zip(e, e[1:])
                  if v == 0 and w == 1 and start <= (z if coil else a) < end]
        counts[f'{pin[0]}.{pin[1]}'] = len(events)
        assert len(events) == len(pulses) == window, (args, counts, pulses)
        period = r.m.gen['gen0'].slot_cycles()*60
        buckets = [sum(start+i*period <= t < start+(i+1)*period for t in events) for i in range(window)]
        assert buckets == [1]*window, (args, pin, buckets)
        for a, z in pulses:
            width = (z - a) / 20
            if pin[0] == 'P2':
                assert 0 < width < 6500, (args, pin, width)
            else:
                assert abs(width - r.pulse) <= 2, (args, pin, width)
    for errors in report['fire_error_us'].values():
        assert all(-r.m.gen['gen0'].slot_cycles()/160-10 <= e <= 100 for e in errors), (args, errors)
    assert report['inhibits'] == report['capture_overruns'] == 0, report
    assert report['max_masked_cycles'] < 2000, report
    return dict(case='continuity', parameters=args, measured_revolutions=window,
                injection_starts_and_sparks=counts, max_masked_cycles=report['max_masked_cycles'])


class Noise:
    def __init__(self, m, at):
        self.m, self.at, self.fired = m, at, False
        m.timed.append(self)
        m._mark_dirty()

    def next_deadline(self):
        return None if self.fired else self.at

    def poll(self, now):
        if not self.fired and now >= self.at:
            self.m.ports.drive_pin('P2', 15, 1)
            self.m.ports.drive_pin('P2', 15, 0)
            self.m.capcom.external_edge('P2', 15, False, self.at)
            self.m.timers.count_input('T0', False)
            self.fired = True


def noise(slot):
    r = Run(900, dwell=3000, pulse=1500, advance=100)
    r.run_revolutions(4)
    tooth = r.m.gen['gen0'].slot_cycles()
    at = int(r.first_tooth + (4*60 + slot) * tooth)
    injected = Noise(r.m, at)
    report = r.run_revolutions(7, allow_faults=True)
    assert injected.fired and report['inhibits'] & 1 and report['losses'], report
    # With authority revoked, neither queued nor new starts may escape.
    for pin in PINS:
        assert not [t for t, v in edges(r, pin) if v == 0 and t > at + tooth], (pin, report)
    cancellation = []
    for pin in PINS[:2]:
        e = edges(r, pin)
        charging = [(a, z) for (a, v), (z, w) in zip(e, e[1:]) if v == 0 and w == 1 and a < at < z]
        cancellation += [dict(pin=str(pin), width_us=(z-a)/20,
                              off_error_deg=((z-r.first_tooth)/tooth-(1140-100+pin[1]*1800)/60+30)%60*6-180)
                         for a, z in charging]
    if slot == 15.3:
        assert cancellation and abs(cancellation[0]['width_us'] - 3000) < 100, cancellation
        assert abs(cancellation[0]['off_error_deg']) < 1, cancellation
    return dict(case='noise', slot=slot, inhibits=report['inhibits'],
                active_charge_cancellation=cancellation, physical_spark_proven=False)


def race(channel, program_equal=False):
    # Locate the linked check, not a stale address from a previous build.
    # OEM scheduler: a coarse fire compare is installed by fire_install, which
    # rereads T0 after programming it (target in R4, channel in RL7).
    local = re.search(r'([0-9A-F]{6})H\s+SYMBOL\s+LABEL\s+\S+\s+\S+\s+fire_install', listing)
    assert local, 'fire_install not in the linker listing'
    entry = int(local.group(1), 16)
    read = image.find(bytes.fromhex('f2f550fe'), entry, entry+0x100)
    assert read >= entry, 'compiled MOV R5,T0 check changed; inspect before updating fixture'
    assert image[read-2:read] == bytes.fromhex('a842'), 'inspect linked fire-target register'
    r = Run(900, dwell=800, pulse=1500, advance=22)
    forced = {}
    equal_matches = []
    if program_equal:
        # Sensitivity check for the unsettled silicon behavior. This is a
        # local test variant; the repository emulator is left unchanged.
        unit = r.m.capcom.u1
        write_mode = unit._w_ccm

        def match_on_install(j, value):
            write_mode(j, value)
            cc = channel  # fire compares: CC0 (coil A), CC1 (coil B)
            if forced and j == 0 and unit.mode_of(cc) == (4, 0):
                if unit.cc[cc] == r.m.timers.value('T0'):
                    unit.ic.set_ir(f'CC{cc}IC')
                    equal_matches.append(r.m.cpu.cycles)
        unit._w_ccm = match_on_install

    def edge_in_window(run):
        m, g = run.m, run.m.gen['gen0']
        if forced or not run.enabled or m.cpu.rw(7) & 255 != channel:
            return
        # Linked code loads the stored fire target into R4, then T0 into R5.
        if (m.cpu.rw(4)-m.cpu.rw(5)) & 65535 == 1 and g.phase == 1:
            delta = g.next_at - m.cpu.cycles
            if 0 < delta < g.slot_cycles()//4:
                forced.update(cycle=m.cpu.cycles, moved_by_cycles=delta-1)
                g.next_at = m.cpu.cycles + 1
                m._mark_dirty()

    report = r.run_until(lambda: r.m.gen['gen0'].revolutions >= 6,
                         breakpoints={read+4}, on_break=edge_in_window)
    assert forced and report['inhibits'] == 0, (forced, report)
    # fire_install programs the compare before it rereads T0, so no compare
    # write can follow the forced edge: the equal-on-install window of the old
    # read-then-install order no longer exists and the variant must not fire.
    assert not equal_matches, ('compare written after the T0 reread', equal_matches)
    assert all(0 < w < 6000 for w in report['pulse_widths_us'][f'P2.{channel}']), report
    assert len(report['pulse_widths_us'][f'P2.{channel}']) >= 3, report
    return dict(case='compare-install-race', channel=channel, forced=forced,
                match_on_install=program_equal, equal_matches=equal_matches,
                widths_us=report['pulse_widths_us'][f'P2.{channel}'], inhibits=report['inhibits'])


def lost_start(channel):
    r = Run(4000, dwell=2000)
    shift = 8 if channel == 0 else 0
    r.run_until(lambda: r.enabled and (r.m.mem.read16(0xff54) >> shift) & 15 == 4)
    # Deliberately suppress one installed coarse charge compare. The OEM pass
    # (sub_37CA0) cancels and reprograms both charge compares every segment,
    # so the coil misses at most that charge and resumes without an inhibit.
    before = len(r.report()['pulse_widths_us'][f'P2.{channel}'])
    r.m.mem.write16(0xff54, r.m.mem.read16(0xff54) & ~(15 << shift))
    report = r.run_revolutions(r.m.gen['gen0'].revolutions + 6, allow_faults=True)
    widths = report['pulse_widths_us'][f'P2.{channel}']
    assert not report['inhibits'], report
    assert len(widths) - before >= 3 and all(1900 < w < 2100 for w in widths[-3:]), report
    return dict(case='lost-start-reschedule', channel=channel, inhibits=report['inhibits'],
                widths_us=widths)


def tuning(age):
    r = Run(6000, dwell=2000, pulse=1000, traffic=True)
    # Build a valid calibration in a separate stopped fixture.
    seed = Run(6000)
    cal_data = seed.ecu + layout['Ecu,cal'] + layout['Calibration,bytes']
    invoke(seed.m, 'cal_example', {8: cal_data & 0x3fff, 9: cal_data >> 14})
    data = bytes(seed.m.mem.read8(cal_data+i) for i in range(3072))
    r.run_revolutions(4)
    r.run_until(lambda: r.m.cpu.pc() == 0xe600)
    m = r.m
    cal = r.ecu + layout['Ecu,cal']
    for i, value in enumerate(data):
        m.mem.write8(cal + layout['Calibration,bytes'] + i, value)
    put(m, cal + layout['Calibration,valid'], 1, 1)
    put(m, cal + layout['Calibration,active'], 0, 1)
    payload = bytes([5, 1, 0, 1, data[0x100]])
    packet = bytes([0xaa, len(payload)]) + payload + bytes([(len(payload)+sum(payload)) & 255])
    protocol = r.ecu + layout['Ecu,protocol']
    for i, value in enumerate(packet):
        m.mem.write8(protocol + layout['Protocol,ring'] + i, value)
    put(m, protocol + layout['Protocol,head'], len(packet), 1)
    put(m, protocol + layout['Protocol,tail'], 0, 1)
    now = r.value(r.ecu, 'Ecu', 'milliseconds', 4)
    r.field(r.plan, 'EnginePlan', 'max_age_ms', age)
    r.field(r.plan, 'EnginePlan', 'stamp', now, 4)
    r.field(r.ecu, 'Ecu', 'foreground_stamp', now, 4)
    entry = symbols['protocol_poll']
    for i, value in enumerate([0xda, entry >> 16, entry & 255, (entry >> 8) & 255]):
        m.mem.write8(0xe600+i, value)
    m.cpu.set_rw(8, now & 65535); m.cpu.set_rw(9, now >> 16)
    start = m.cpu.cycles
    for _ in range(1000):
        result = m.run(max_instructions=1024, breakpoints={0xe604}, stop_on_trap=True)
        assert not m.cpu.traps and result in ('budget', 'breakpoint'), m.cpu.traps
        if m.cpu.pc() == 0xe604:
            break
    else:
        raise AssertionError('protocol_poll failed to return')
    response = bytes(m.mem.read8(protocol+layout['Protocol,tx']+i) for i in range(4))
    elapsed = (m.cpu.cycles-start)/20000
    assert response == bytes.fromhex('55010001'), response
    assert r.value(cal, 'Calibration', 'generation') == 1
    assert elapsed < 20 and not r.value(r.authority, 'Authority', 'inhibits'), (elapsed, r.report())
    return dict(case='live-tuning', plan_age_ms=age, wall_ms=elapsed, response=response.hex(),
                supplied_plan=True, physical_uart=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock-95080', action='store_true')
    parser.add_argument('--case', choices=['fuel', 'load', 'gap', 'race', 'noise', 'tuning'])
    args = parser.parse_args()
    cases = {
        'fuel': [lambda rpm=rpm, pulse=pulse: continuity(window=24 if pulse == 12500 else 12,
                                                       rpm=rpm, dwell=2000, pulse=pulse)
                 for rpm, pulse in [(4000, 6000), (4000, 9000), (6000, 5000), (4000, 12500)]]
                + [lambda: continuity(window=24, rpm=4000, dwell=2000, pulse=12500,
                                      phase=1620, origin=0xFFFFFF00)],
        'gap': [lambda rpm=rpm, advance=advance: continuity(rpm=rpm, dwell=3000, advance=advance)
                for rpm, advance in [(4800, 200), (6000, 0)]],
        'load': [lambda rpm=rpm, duty=duty: continuity(window=24, rpm=rpm, dwell=2000,
                    pulse=round(60000000/rpm*duty/100), traffic=True)
                 for rpm,duty in [(1500,60),(2500,85),(4000,10),(4000,55),(4000,85),
                                  (6000,10),(6000,85),(7000,85)]],
        'race': [lambda ch=ch, equal=equal: race(ch, equal) for equal in (False, True) for ch in (0, 1)]
                + [lambda ch=ch: lost_start(ch) for ch in (0, 1)],
        'noise': [lambda slot=slot: noise(slot) for slot in (5.3, 35.3, 15.3)],
        'tuning': [lambda age=age: tuning(age) for age in (20, 30, 50)],
    }
    rows = []
    output = OUT / f'regressions-{HEX_SHA256[:12]}-{args.case or "all"}.json'
    for group, tests in cases.items():
        if args.case is not None and args.case != group:
            continue
        for test in tests:
            row = test()
            print(json.dumps(row), flush=True)
            rows.append(row)
            output.write_text(json.dumps(dict(profile=PROFILE, hex_sha256=HEX_SHA256,
                scope=__doc__, results=rows, physical_acceptance=False), indent=2)+'\n')
    assert hashlib.sha256((BUILD/'TU5JP.H86').read_bytes()).hexdigest() == HEX_SHA256
    print(f'PASS {len(rows)} linked scheduler regressions ({PROFILE})')


if __name__ == '__main__':
    main()
