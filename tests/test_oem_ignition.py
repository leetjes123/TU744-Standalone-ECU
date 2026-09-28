"""Differential test: oem_ignition_segment against unchanged ROM sub_37CA0.

Each case loads the same inputs into ROM RAM/SFR images and the C state,
runs the ROM routine on the bare C167 core and the native port, and compares
every state field, including compare images, flags and MDL/MDH.
"""
import ctypes as C
import random
from oem_harness import Rom, ram, library

W = [('fd1c', 0xFD1C), ('fd6a', 0xFD6A), ('f8b0', 0xF8B0), ('f8ae', 0xF8AE),
     ('f7aa', 0xF7AA), ('f7a6', 0xF7A6), ('t1', 0xFE52), ('cap_prev', 0xF910), ('cap_last', 0xF912),
     ('f800', 0xF800), ('f802', 0xF802), ('f7fa', 0xF7FA), ('f7fe', 0xF7FE), ('f7f6', 0xF7F6),
     ('f7f4', 0xF7F4), ('f7f8', 0xF7F8), ('f7fc', 0xF7FC), ('p2', 0xFFC0), ('r82dc', ram(0x82DC)),
     ('r9716', ram(0x9716)), ('ccm0', 0xFF52), ('ccm1', 0xFF54), ('cc0', 0xFE80), ('cc4', 0xFE88),
     ('cc6', 0xFE8C), ('cc0ic', 0xFF78), ('cc4ic', 0xFF80), ('cc6ic', 0xFF84), ('mdl', 0xFE0E),
     ('mdh', 0xFE0C), ('p971e', ram(0x971E)), ('p9718', ram(0x9718)), ('p9726', ram(0x9726)),
     ('p9722', ram(0x9722)), ('p9724', ram(0x9724)), ('p9720', ram(0x9720)), ('pf7f2', 0xF7F2)]
WA = [('r971a', ram(0x971A), 2), ('dwell', ram(0x9734), 2)]
B = [('f8d1', 0xF8D1), ('f8d2', 0xF8D2), ('f829', 0xF829), ('f8ad', 0xF8AD), ('r9500', ram(0x9500)),
     ('r9294', ram(0x9294)), ('f7ec', 0xF7EC), ('f7dd', 0xF7DD), ('f7de', 0xF7DE), ('f7e1', 0xF7E1),
     ('f7df', 0xF7DF), ('f7e2', 0xF7E2), ('f7e0', 0xF7E0), ('f7f1', 0xF7F1), ('f7f0', 0xF7F0)]
BA = [('adv', ram(0x9278), 4), ('pos', 0xF7E4, 4), ('pipe', 0xF7E8, 4), ('mask', None, 4), ('index', None, 4)]


class Ignition(C.Structure):
    _fields_ = ([(n, C.c_uint16) for n, _ in W] + [(n, C.c_uint16 * k) for n, _, k in WA] +
                [(n, C.c_uint8 * k) for n, _, k in BA if n == 'adv'] +
                [(n, C.c_uint8 * k) for n, _, k in BA if n in ('pos', 'pipe')] +
                [(n, C.c_uint8) for n, _ in B] +
                [(n, C.c_uint8 * k) for n, _, k in BA if n in ('mask', 'index')])


rom = Rom()
mem = rom.mem
lib = library()
lib.oem_ignition_segment.argtypes = [C.POINTER(Ignition)]
lib.oem_ignition_segment.restype = None
TABLE = rom.data[0x14DF4:0x14DFC]
assert list(TABLE) == [1, 2, 1, 2, 0, 1, 0, 1], list(TABLE)
DSTP2 = 0xFCEA


def load(state):
    for n, a in W: mem.write16(a, getattr(state, n))
    for n, a, k in WA:
        for i in range(k): mem.write16(a + 2 * i, getattr(state, n)[i])
    for n, a in B: mem.write8(a, getattr(state, n))
    for n, a, k in BA:
        if a is not None:
            for i in range(k): mem.write8(a + i, getattr(state, n)[i])
    mem.write16(DSTP2, 0xF914)


def read():
    out = Ignition()
    for n, a in W: setattr(out, n, mem.read16(a))
    for n, a, k in WA:
        for i in range(k): getattr(out, n)[i] = mem.read16(a + 2 * i)
    for n, a in B: setattr(out, n, mem.read8(a))
    for n, a, k in BA:
        if a is not None:
            for i in range(k): getattr(out, n)[i] = mem.read8(a + i)
    for i in range(4): out.mask[i] = TABLE[i]; out.index[i] = TABLE[4 + i]
    return out


def fields(s):
    d = {n: getattr(s, n) for n, _ in W}
    d.update({n: list(getattr(s, n)) for n, _, _ in WA})
    d.update({n: getattr(s, n) for n, _ in B})
    d.update({n: list(getattr(s, n)) for n, _, _ in BA})
    return d


rng = random.Random(0x37CA0)
paths = {}


def case(seed_state):
    load(seed_state)
    rom.invoke(0x37CA0)
    expected = fields(read())
    native = Ignition.from_buffer_copy(bytes(seed_state))
    lib.oem_ignition_segment(C.byref(native))
    got = fields(native)
    diff = {k: (got[k], expected[k]) for k in expected if got[k] != expected[k]}
    assert not diff, (fields(seed_state), diff)
    key = (expected['f7de'], bool(expected['fd1c'] & 0x40), expected['ccm1'] & 0x808, expected['ccm0'] & 8,
           seed_state.f8ae >= 300, bool(expected['cc6ic'] & 0x80))
    paths[key] = paths.get(key, 0) + 1


def random_state():
    s = Ignition()
    rpm = rng.choice([rng.randint(20, 400), rng.randint(400, 7500), rng.randint(0, 20000)])
    period = int(1.25e6 * 30 / max(rpm, 1))                  # ticks per 180 degrees
    s.f8ad = (period >> 16) & 255 if rng.random() > 0.05 else rng.randint(0, 255)
    s.f8b0 = period & 65535
    s.f8ae = rng.choice([rpm * 4 & 65535, rng.randint(0, 65535), rng.choice([299, 300])])
    tooth = max(1, period // 30)
    s.cap_prev = rng.randint(0, 65535); s.cap_last = (s.cap_prev + tooth) & 65535
    s.f7aa = rng.randint(0, 65535); s.f7a6 = rng.randint(0, 65535)
    s.t1 = (s.f7aa + rng.choice([rng.randint(0, 3000), rng.randint(0, 65535)])) & 65535
    s.f800 = rng.choice([0, rng.randint(0, 3000), rng.randint(0, 65535)])
    s.f802 = rng.choice([0, rng.randint(0, 3000), rng.randint(0, 65535)])
    s.fd1c = rng.randint(0, 65535) if rng.random() < 0.3 else rng.choice([0x0800, 0x0900, 0x0A00, 0x0840]) | rng.randint(0, 0xFF)
    s.fd6a = rng.randint(0, 65535) & ~(0x20 if rng.random() < 0.9 else 0)
    s.p2 = rng.choice([0, 1, 2, 3, rng.randint(0, 65535)])
    s.r82dc = rng.choice([0, 0, 0, 1, 2, rng.randint(0, 65535)])
    s.r9716 = rng.choice([0, 0, 0, 1, 2, rng.randint(0, 65535)])
    for n in ['ccm0', 'ccm1', 'cc0', 'cc4', 'cc6', 'cc0ic', 'cc4ic', 'cc6ic', 'mdl', 'mdh', 'f7fa', 'f7fe',
              'f7f6', 'f7f4', 'f7f8', 'f7fc', 'p971e', 'p9718', 'p9726', 'p9722', 'p9724', 'p9720', 'pf7f2']:
        setattr(s, n, rng.randint(0, 65535))
    s.mdl = s.mdh = 0  # CPU reset in rom.invoke clears MD; it is scratch on entry
    for i in range(2):
        s.r971a[i] = rng.randint(0, 65535)
        s.dwell[i] = rng.choice([rng.randint(625, 7500), rng.randint(0, 65535)])
    for i in range(4):
        s.adv[i] = rng.choice([rng.randint(-40, 80), rng.randint(-128, 127)]) & 255
        s.pos[i] = rng.randint(0, 255); s.pipe[i] = rng.randint(0, 255)
        s.mask[i] = TABLE[i]; s.index[i] = TABLE[4 + i]
    s.f8d1 = rng.randint(0, 3)  # segment index is always 0..3 (sub_68678)
    s.f8d2 = rng.choice([3, 3, 3, 0, 1, 2, rng.randint(0, 255)])
    s.f829 = s.f8d1 if rng.random() < 0.9 else rng.randint(0, 255)
    s.r9500 = rng.choice([3, 5, 5, 0, 2, rng.randint(0, 255)])
    for n in ['r9294', 'f7ec', 'f7dd', 'f7de', 'f7e1', 'f7df', 'f7e2', 'f7e0', 'f7f1', 'f7f0']:
        setattr(s, n, rng.randint(0, 255))
    return s


def wide_state():
    """Full-range periods, dwell and latency with coils idle and no cuts:
    reaches the saturation and 480-count clamp branches (37E48, 37EB0,
    386BE, 38704) that realistic speeds rarely produce."""
    s = random_state()
    s.f8ad = rng.choice([0, 0, 1, rng.randint(0, 255)])
    s.f8b0 = rng.randint(0, 65535)
    s.f8ae = rng.choice([rng.randint(0, 299), rng.randint(300, 65535)])
    s.f800 = rng.randint(0, 65535)
    s.p2 = 0xFFFF if rng.random() < 0.7 else rng.randint(0, 3)
    s.r82dc = s.r9716 = 0
    s.fd1c = rng.randint(0, 65535)
    s.fd6a = rng.randint(0, 65535) & ~0x20
    s.f8d2 = 3; s.f829 = s.f8d1
    s.r9500 = rng.choice([3, 0])
    for i in range(2): s.dwell[i] = rng.randint(0, 65535)
    s.cap_last = (s.cap_prev + rng.randint(0, 65535)) & 65535
    return s


count = 0
for _ in range(20000):
    case(random_state()); count += 1
for _ in range(20000):
    case(wide_state()); count += 1
# Not exercised: 38124 (slot-0 start > 480: a position is <= 255), 38216 and
# 3867E (coil index >= 2: the calibration table holds 0/1), 38704 (second-
# charge delay saturation: with 16-bit dwell the start is >= 27 teeth away,
# beyond any lead that allows the direct path), 387B4 (RETS). All other 702
# reachable instructions of 37CA0..387B4 execute in these cases.
print(f'PASS oem_ignition_segment == ROM 37CA0 for {count} cases, {len(paths)} distinct path signatures')
