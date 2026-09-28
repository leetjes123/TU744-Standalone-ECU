"""Actual reset/startup and foreground execution with a synthetic test tune.

Stock M95080 only. Test calibration is packed in the emulated NOR, never in a
delivery image. No supplied plan, heartbeat, rotation state or inhibit clearing.
Physical ADC, coil and bus timing remain outside this digital model.
"""
import argparse
import binascii
import json
import sys
from pathlib import Path

from test_oem_scheduler_target import (Run, OUT, PROFILE, HEX_SHA256, Machine,
                                        image, invoke, layout, variables, symbols, put)


def fixture_image(calibration=None):
    seed = Run(6000)
    at = seed.ecu + layout['Ecu,cal'] + layout['Calibration,bytes']
    invoke(seed.m, 'cal_example', {8: at & 0x3fff, 9: at >> 14})
    tune = bytearray(seed.m.mem.read8(at+i) for i in range(3072))
    tune[0x5db:0x5df] = bytes.fromhex('271026ac')  # synthetic 10000/9900 RPM limits
    tune[0x922:0x924] = b'\0\x14'  # shortest legal plan age: 20 ms
    if calibration is not None:
        tune = calibration.read_bytes()
        assert len(tune)==3072
    header = bytearray(b'\xff' * 32)
    header[:12] = b'LRC3\0\4\x0c\0\0\0\0\1'
    header[12:14] = binascii.crc_hqx(tune, 0xffff).to_bytes(2, 'big')
    header[14:16] = b'\0\1'
    header[20:22] = binascii.crc_hqx(header[:20], 0xffff).to_bytes(2, 'big')
    header[31] = 0xa5
    result = bytearray(image)
    result[0x50000:0x50c20] = header + tune
    return result


def boot(rpm, rotating, scenario='normal', calibration=None, packed_image=None):
    m = Machine(fixture_image(calibration) if packed_image is None else packed_image.read_bytes())
    ecu = variables['ecu']
    authority = ecu + layout['Ecu,authority']
    rotation = ecu + layout['Ecu,rotation']
    cal = ecu + layout['Ecu,cal']
    def value(base, struct, field, width=2):
        at = base + layout[f'{struct},{field}']
        return sum(m.mem.read8(at+i) << (i*8) for i in range(width))
    m.ports.drive_pin('P4', 4, 1)
    for ch, raw in {0:300, 5:486, 6:512, 8:300, 10:300, 11:400}.items():
        m.adc.set_volts(ch, raw * 5 / 1023)
    m.gen['gen0'].configure(teeth=60, missing=2, rpm=rpm, enabled=rotating,
                            pins=[('P2', 15)], count_timer='T0')
    instructions, polls = 0, 0
    started = None
    enabled_at = None
    action_at = restored_at = fault_at = reset_at = None
    feeds = []
    healthy_faults = []
    max_poll_cycles = 0
    previous_poll = None
    measurement = []
    max_age = 0
    generation = None
    bps = {symbols[n] for n in ('ecu_poll', 'hal_watchdog_service', 'hal_system_reset')}
    while instructions < 30000000:
        result = m.run(max_instructions=8192, breakpoints=bps, stop_on_trap=True)
        instructions += 8192  # upper bound including short breakpoint batches
        assert not m.cpu.traps and result in ('budget', 'breakpoint'), (result, m.cpu.traps)
        if result == 'breakpoint':
            if m.cpu.pc() == symbols['hal_system_reset']:
                reset_at = m.cpu.cycles
                break
            if m.cpu.pc() == symbols['hal_watchdog_service']:
                feeds.append(m.cpu.cycles)
                m.step()
                continue
            polls += 1
            if previous_poll is not None:
                max_poll_cycles = max(max_poll_cycles, m.cpu.cycles-previous_poll)
            previous_poll = m.cpu.cycles
            if started is None:
                started = m.cpu.cycles
                if not rotating:
                    m.gen['gen0'].configure(enabled=True)
                    m._mark_dirty()
            inhibit = value(authority, 'Authority', 'inhibits')
            if inhibit & (32 | 256) and scenario != 'watchdog':
                break
            if not inhibit and enabled_at is None:
                enabled_at = m.cpu.cycles
            if enabled_at is not None and action_at is None and m.cpu.cycles-enabled_at >= 20000*60:
                action_at = m.cpu.cycles
                if scenario == 'tuning':
                    protocol = ecu + layout['Ecu,protocol']
                    generation = value(cal, 'Calibration', 'generation')
                    payload = bytes([5, 1, 0, 1, 81])
                    packet = bytes([0xaa, len(payload)]) + payload + bytes([(len(payload)+sum(payload)) & 255])
                    head = value(protocol, 'Protocol', 'head', 1)
                    assert head == value(protocol, 'Protocol', 'tail', 1)
                    for byte in packet:
                        put(m, protocol+layout['Protocol,ring']+head, byte, 1)
                        head = (head+1) & 127
                    put(m, protocol+layout['Protocol,head'], head, 1)
                elif scenario == 'watchdog':
                    m.mem.write16(0xff68, m.mem.read16(0xff68) & ~64)  # T6IE
                elif scenario == 'restart':
                    m.ports.drive_pin('P4', 4, 0)
            if action_at is not None and restored_at is None:
                delay = 40 if scenario == 'restart' else 5
                if m.cpu.cycles-action_at >= 20000*delay:
                    restored_at = m.cpu.cycles
                    if scenario == 'restart':
                        m.ports.drive_pin('P4', 4, 1)
                    elif scenario == 'watchdog':
                        m.mem.write16(0xff68, m.mem.read16(0xff68) | 64)
            if enabled_at is not None:
                if inhibit and scenario in ('normal', 'tuning'):
                    healthy_faults.append((m.cpu.cycles, inhibit))
                if inhibit & 32 and fault_at is None:
                    fault_at = m.cpu.cycles
                plan = authority + layout['Authority,plan']
                max_age = max(max_age, value(ecu, 'Ecu', 'milliseconds', 4)-value(plan, 'EnginePlan', 'stamp', 4))
                if not measurement and m.cpu.cycles-enabled_at >= 20000*100:
                    measurement.append(m.cpu.cycles)
                if measurement and len(measurement) == 1 and m.cpu.cycles-measurement[0] >= m.gen['gen0'].slot_cycles()*60*12:
                    measurement.append(measurement[0] + m.gen['gen0'].slot_cycles()*60*12)
            duration = 350 if scenario == 'watchdog' else max(300, 110+12*60000/rpm)
            if enabled_at is not None and m.cpu.cycles - enabled_at >= 20000 * duration:
                break
            m.step()
        if started is not None and m.cpu.cycles - started > 20000 * 700:
            break
    counts = {}
    if len(measurement) == 2:
        for port, bit in [('P2', 0), ('P2', 1), ('P7', 6), ('P7', 5), ('P7', 4), ('P8', 7)]:
            counts[f'{port}.{bit}'] = sum(1 for t,p,b,v in m.ports.events if
                (p,b)==(port,bit) and v==(1 if port=='P2' else 0) and measurement[0]<=t<measurement[1])
    protocol = ecu + layout['Ecu,protocol']
    response = bytes(m.mem.read8(protocol+layout['Protocol,tx']+i) for i in range(4))
    report = dict(case='foreground-'+scenario, rpm=rpm, rotating_at_reset=rotating,
                  calibration='synthetic test fixture' if calibration is None else str(calibration),
                  packed_image=None if packed_image is None else str(packed_image), image_sha256=m.image_sha256,
                  polls=polls, instructions_bound=instructions, wall_ms=m.cpu.cycles/20000,
                  boot_ms=None if started is None else started/20000,
                  cal_valid=value(cal, 'Calibration', 'valid', 1),
                  inhibits=value(authority, 'Authority', 'inhibits'),
                  rotation=value(rotation, 'Rotation', 'state', 1),
                  observed_rpm=value(rotation, 'Rotation', 'rpm'),
                  capture_overruns=m.mem.read16(variables['capture_overruns']),
                  capture_blocks=m.mem.read16(variables['capture_blocks']),
                  enabled_ms=None if enabled_at is None else enabled_at/20000,
                  events=len(m.ports.events), pc=hex(m.cpu.pc()),
                  injection_starts_and_sparks_in_12_revolutions=counts,
                  max_poll_ms=max_poll_cycles/20000, max_plan_age_ms=max_age,
                  healthy_faults=healthy_faults, watchdog_services=len(feeds),
                  watchdog_services_after_fault=sum(t>fault_at for t in feeds) if fault_at else 0,
                  reset_requested=reset_at is not None, generation=value(cal, 'Calibration', 'generation'),
                  response=response.hex())
    print(json.dumps(report), flush=True)
    if enabled_at is not None:
        assert not report['capture_overruns'], report
        if scenario in ('normal', 'tuning'):
            assert not healthy_faults and not report['inhibits'] and counts and all(n==12 for n in counts.values()), report
        if scenario == 'tuning':
            assert report['generation'] == generation+1 and response == bytes.fromhex('55010001'), report
        if scenario == 'watchdog':
            assert fault_at and report['inhibits'] & 32 and not reset_at and report['watchdog_services_after_fault'] > 100, report
            assert not [e for e in m.ports.events if e[1:3] in [('P2',0),('P2',1),('P7',6),('P7',5),('P7',4),('P8',7)] and e[3]==0 and e[0]>fault_at+20000*7], report
        if scenario == 'restart':
            assert reset_at and report['observed_rpm'] and m.mem.read16(0xffc0)&3==3, report
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock-95080', action='store_true', required=True)
    parser.add_argument('--rpm', type=int, default=6000)
    parser.add_argument('--rotating', action='store_true')
    parser.add_argument('--probe', action='store_true')
    parser.add_argument('--scenario', choices=['normal','tuning','watchdog','restart'], default='normal')
    parser.add_argument('--tune', type=Path)
    parser.add_argument('--image', type=Path, help='boot these exact packed bytes instead of constructing a fixture image')
    args = parser.parse_args()
    assert PROFILE == 'stock-95080'
    report = boot(args.rpm, args.rotating, args.scenario, args.tune, args.image)
    tune_label=args.image.stem if args.image else 'synthetic' if args.tune is None else args.tune.stem
    (OUT / f'foreground-{HEX_SHA256[:12]}-{args.rpm}-{int(args.rotating)}-{args.scenario}-{tune_label}.json').write_text(
        json.dumps(dict(scope=__doc__, hex_sha256=HEX_SHA256, result=report), indent=2)+'\n')
    if not args.probe:
        assert report['enabled_ms'] is not None and not report['capture_overruns'], report
