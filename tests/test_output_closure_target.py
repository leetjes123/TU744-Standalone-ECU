"""Linked regression coverage for retained spark, feedback and capture ownership.

Runs with the supplied-plan peripheral fixture; test_foreground_target.py covers
the real startup/foreground separately. Deliberate ISR delays below test ownership
when an electrical edge arrives at a particular instruction, not silicon timing.
"""
import json
from test_oem_scheduler_target import Run, OUT, HEX_SHA256, PROFILE, symbols, image, layout


def pending_capture():
    r = Run(900, dwell=2000)
    forced = []
    def delay(run):
        m, g = run.m, run.m.gen['gen0']
        if forced or not run.enabled or run.value(run.rotation, 'Rotation', 'tooth', 1) != 3:
            return
        # The completed PEC word belongs to the interrupted block. Allow the
        # next regular edge to arrive before that block's metadata is read.
        assert g.phase == 0
        at = g.next_at + g.slot_cycles()//2
        forced.append(dict(before=m.cpu.cycles, after=at))
        m.cpu.cycles = at
    report = r.run_until(lambda:r.m.gen['gen0'].revolutions >= 6,
                         breakpoints={symbols['capture_isr']}, on_break=delay)
    assert forced and not report['capture_overruns'] and not report['inhibits'], report
    return dict(case='pending-capture-handoff', forced=forced, capture_blocks=report['capture_blocks'],
                pec_transfers=report['pec_transfers'], inhibits=report['inhibits'])


def torn_snapshot():
    r = Run(4000, dwell=2000)
    entry = symbols['board_capture_snapshot']
    read = image.find(bytes.fromhex('f2f650fe'), entry, entry+128)
    assert read >= entry, 'inspect linked snapshot first T0 read'
    forced = []
    def delay(run):
        m, g = run.m, run.m.gen['gen0']
        if forced or not run.enabled:
            return
        # Retain input cadence while delaying this snapshot across one edge.
        at = g.next_at + (g.slot_cycles()//2 if g.phase == 0 else 0)
        forced.append(dict(before=m.cpu.cycles, after=at))
        m.cpu.cycles = at
    report = r.run_until(lambda:r.m.gen['gen0'].revolutions >= 6,
                         breakpoints={read+4}, on_break=delay)
    late = r.value(r.authority, 'Authority', 'late_events')
    assert forced and late and not report['inhibits'] and not report['losses'], report
    assert all(w<6500 for p in ('P2.0','P2.1') for w in report['pulse_widths_us'][p]), report
    return dict(case='torn-capture-snapshot', forced=forced, late_events=late,
                inhibits=report['inhibits'], widths_us={p:report['pulse_widths_us'][p] for p in ('P2.0','P2.1')})


def feedback_absent(enabled):
    r = Run(4000, dwell=3000, feedback=enabled)
    report = r.run_revolutions(20)
    missing = [r.m.mem.read16(r.authority + layout['Authority,feedback_missing']+2*ch) for ch in (0,1)]
    corrections = [r.m.mem.read16(r.authority + layout['Authority,feedback_correction']+2*ch) for ch in (0,1)]
    assert all(n>=12 for n in missing) if enabled else missing == [0,0], missing
    assert corrections == [0,0] and not report['inhibits'], report
    for pin in ('P2.0','P2.1'):
        assert len(report['pulse_widths_us'][pin]) >= 16
        assert all(abs(w-3000)<150 for w in report['pulse_widths_us'][pin]), report
    return dict(case='absent-feedback', enabled=enabled, missing=missing, correction=corrections,
                width_range_us={p:[min(report['pulse_widths_us'][p]),max(report['pulse_widths_us'][p])] for p in ('P2.0','P2.1')})


def changing_speed():
    r=Run(2000,dwell=3000,pulse=4500,traffic=True)
    r.run_revolutions(6)
    rows=[]
    rpms=list(range(2250,7001,250))+list(range(6750,1999,-250))
    for rpm in rpms:
        g=r.m.gen['gen0'];g.configure(rpm=rpm);r.m._mark_dirty()
        before=r.m.cpu.cycles
        report=r.run_revolutions(g.revolutions+1)
        rows.append(dict(rpm=rpm,counts={f'{port}.{bit}':sum(t>=before and p==port and b==bit and v==(1 if port=='P2' else 0)
            for t,p,b,v in r.m.ports.events) for port,bit in [('P2',0),('P2',1),('P7',6),('P7',5),('P7',4),('P8',7)]}))
    assert not report['inhibits'] and not report['capture_overruns'],report
    assert all(all(n==1 for n in row['counts'].values()) for row in rows),rows
    return dict(case='changing-speed',ramp=rows,inhibits=report['inhibits'])


if __name__ == '__main__':
    rows=[]
    for case in (pending_capture, torn_snapshot, lambda:feedback_absent(False),lambda:feedback_absent(True),changing_speed):
        row=case(); rows.append(row);print(json.dumps(row),flush=True)
    (OUT/f'closure-{HEX_SHA256[:12]}.json').write_text(json.dumps(dict(profile=PROFILE,
        hex_sha256=HEX_SHA256, scope=__doc__, results=rows),indent=2)+'\n')
    print(f'PASS {len(rows)} output closure regressions ({PROFILE})')
