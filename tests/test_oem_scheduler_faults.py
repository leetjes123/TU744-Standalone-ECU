"""Linked scheduler failure/recovery with real timer and compare delivery."""
from test_oem_scheduler_target import Run, invoke, layout, variables


def safe(run):
    m = run.m
    assert m.mem.read16(0xFFC0) & 3 == 3
    assert m.mem.read16(0xFFD0) & 0x70 == 0x70
    assert m.mem.read16(0xFFD4) & 0x80 == 0x80
    assert m.mem.read16(0xFF52) & 0xF0F == 0
    assert m.mem.read16(0xFF54) & 0xF0F == 0
    assert m.mem.read16(0xFF28) & 0xFFF == 0
    assert m.mem.read16(0xFF24) & 0xF0FF == 0


def foreground(run, name, args):
    assert run.m.cpu.pc() == 0xE600
    invoke(run.m, name, args)
    run.m.cpu.csp = 0; run.m.cpu.ip = 0xE600; run.m.cpu.psw = 0x800


# A missed PEC transfer must never silently shift the tooth-counter relation.
# It revokes angle like a wrong gap, reseeds the counter and recovers without
# a reset; it is counted, snapshotted and never a DEADLINE latch.
r = Run(10000)
r.run_revolutions(4)
r.m.cpu.psw = 0
r.m.cpu.cycles += 16000  # 8 teeth while requests coalesce
r.m._service()
r.m.cpu.psw = 0x800
health = variables['timing_health']
report = r.run_revolutions(5, allow_faults=True)
assert report['state'] != 2 and report['inhibits'] & 1, report  # angle revoked at once
report = r.run_revolutions(9, allow_faults=True)
assert report['capture_overruns'] == 1 and report['losses'], report
assert report['inhibits'] & 1 and not report['inhibits'] & 32, report
assert r.value(health, 'TimingHealth', 'capture_resyncs') == 1
assert r.value(health, 'TimingHealth', 'capture_reason') == 2
assert report['state'] == 2, report  # two consistent gaps after the reseed
safe(r)
foreground(r, 'safety_conditions', {8: 0, 9: 0, 10: 0})
assert not r.value(r.authority, 'Authority', 'inhibits')
r.field(r.plan, 'EnginePlan', 'epoch', r.value(r.authority, 'Authority', 'epoch'))
before = len(r.m.ports.events)
report = r.run_revolutions(18)
assert not report['inhibits'] and report['capture_overruns'] == 1 and len(r.m.ports.events) > before, report
for errors in report['fire_error_us'].values():
    assert errors and all(-r.m.gen['gen0'].slot_cycles()/160-10 <= e <= 100 for e in errors[-4:]), report
print('PASS lost-PEC detection, cancellation, reseed and fresh-epoch restart')

# A starved deferred worker (queue exhaustion) still latches DEADLINE until
# reset, and leaves no frozen RUNNING rotation behind.
r = Run(10000)
r.run_revolutions(4)
r.m.intctl.regs['XP1IC'] &= ~64
r.run_revolutions(6, allow_faults=True)
r.m.intctl.regs['XP1IC'] |= 64
report = r.run_revolutions(8, allow_faults=True)
assert report['capture_overruns'] == 1 and report['inhibits'] & 32, report
assert report['state'] == 0 and not r.value(r.rotation, 'Rotation', 'rpm'), report
assert r.value(health, 'TimingHealth', 'capture_reason') == 1
assert r.value(health, 'TimingHealth', 'capture_resyncs') == 0
safe(r)
foreground(r, 'safety_conditions', {8: 0, 9: 0, 10: 0})
assert r.value(r.authority, 'Authority', 'inhibits') & 32
print('PASS queue exhaustion, reset-only deadline latch and rotation release')

# A wrong gap invalidates the decoder; two consistent turns permit reacquisition.
r = Run(4000)
r.run_revolutions(4)
r.m.gen['gen0'].missing = 4
bad = r.run_revolutions(6, allow_faults=True)
assert bad['inhibits'] & 1 and bad['losses'], bad
safe(r)
r.m.gen['gen0'].missing = 2
acquired = r.run_revolutions(9, allow_faults=True)
assert acquired['state'] == 2 and not acquired['capture_overruns'], acquired
foreground(r, 'safety_conditions', {8: 0, 9: 0, 10: 0})
r.field(r.plan, 'EnginePlan', 'epoch', r.value(r.authority, 'Authority', 'epoch'))
before = len(r.m.ports.events)
report = r.run_revolutions(12)
assert not report['inhibits'] and len(r.m.ports.events) > before, report
print('PASS wrong-gap cancellation, resynchronization and fresh-epoch restart')

# Stale plans are cancelled by the real tick supervisor, including queued starts.
r = Run(10000, traffic=True)
r.run_revolutions(4)
r.field(r.plan, 'EnginePlan', 'max_age_ms', 0)
report = r.run_revolutions(6, allow_faults=True)
assert report['inhibits'] & 8, report
safe(r)
print('PASS live tick stale-plan cancellation')
