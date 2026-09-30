"""Native rotation-input contracts versus unchanged TU5JP ROM and Keil C.

These calls reproduce native RAM/SFR effects, not peripheral side effects or
the full capture interrupt call sequence. No expected state is copied from
the oracle into the replacement between trajectory steps.
"""
import ctypes as C
import random
import sys
from oem_harness import Rom, ram, library

words = dict(capture_low=0xF7AA, capture_previous=0x8AE2,
             period_previous=0x9BAE, period_older=0x9BB0, period=0x9BB2,
             threshold=0x8AE8, rotation_flags=0xFD6A, flags12=0xFD12,
             flags14=0xFD14, flags08=0xFD08, flags66=0xFD66, speed_word=0xF8AE,
             t0=0xFE50, pecc2=0xFEC4, srcp2=0xFCE8, dstp2=0xFCEA,
             xp1ic=0xF18E, phase_begin=0xF8D6, phase_end=0xF8D8,
             reset_f7a6=0xF7A6, reset_f8d4=0xF8D4, reset_9bb6=0x9BB6,
             history_low=0x8A9A, history_period=0xF8B0, history_filter=0x8A98)
octets = dict(capture_high=0xF7A4, previous_high=0x8AE4, capture_count=0x8AE5,
              equipment=0xAA59, phase_index=0xF8D1, next_phase=0xF91A,
              active=0xF8D2, tooth_count=0x9503, phase_match=0x94FF,
              skipped=0x8AF4, reset_8ae0=0x8AE0, reset_9501=0x9501,
              reset_f8d0=0xF8D0, reset_f8d3=0xF8D3, speed=0xF8AC,
              speed_fast=0x94EF, history_high=0x8A9C, history_period_high=0xF8AD,
              speed_filtered=0x94F0)

class Rotation(C.Structure):
    _fields_ = [(n, C.c_uint16) for n in words] + [(n, C.c_uint8) for n in octets]

rom = Rom(); mem = rom.mem; lib = library(); rng = random.Random(0x688BC)
entries = dict(threshold=0x68548, reset=0x6860A, capture=0x688BC,
               period=0x668B2, speed=0x66A10)
for name in entries:
    fn = getattr(lib, 'oem_rotation_' + name)
    fn.argtypes = [C.POINTER(Rotation)]; fn.restype = None

def publish(s):
    for n, a in words.items(): mem.write16(ram(a), getattr(s, n))
    for n, a in octets.items(): mem.write8(ram(a), getattr(s, n))

cases = 0
def compare(s, name, label):
    global cases
    getattr(lib, 'oem_rotation_' + name)(C.byref(s))
    rom.invoke(entries[name])
    for mapping, read in [(words, mem.read16), (octets, mem.read8)]:
        for n, a in mapping.items():
            assert getattr(s, n) == read(ram(a)), (name, label, n, getattr(s, n), read(ram(a)))
    cases += 1

for name in entries:
    for i in range(1200):
        s = Rotation()
        for n in words: setattr(s, n, rng.randrange(65536))
        for n in octets: setattr(s, n, rng.randrange(256))
        s.capture_count = rng.choice([0, 1, 2, 254, 255])
        s.tooth_count = rng.choice([0, 1, 2, 29, 30, 99, 100, 101, 255])
        s.phase_index = rng.choice([0, 1, 2, 3, 254, 255])
        s.period = rng.choice([0, 1, 2, 3, 3124, 3125, 3126, 34952, 34953, 65535])
        s.threshold = rng.choice([0, 1, 3125, 65535])
        s.period_previous = rng.choice([0, 1, 2, 3124, 3125, 3126, 65535])
        s.speed_word = rng.choice([0, 39, 40, 159, 160, 10239, 10240, 40799, 40800, 65535])
        s.speed_fast = rng.choice([0, 1, 149, 150, 151, 255])
        s.history_filter = rng.choice([0, 1, 38399, 38400, 38401, 65535])
        # Deliberately include low-word borrow, 24-bit wrap, saturation and
        # small periods, which uniformly random timestamps rarely reach.
        elapsed = rng.choice([0, 1, 15, 16, 31, 32, 3124*16, 3125*16, 3126*16,
                              65535*16, 65536*16, 0xFFFFFF])
        before = rng.choice([0, 65535, 65536, 0xFFFFF0, rng.randrange(1 << 24)])
        after = (before + elapsed) & 0xFFFFFF
        s.capture_previous = before & 65535; s.previous_high = before >> 16
        s.history_low = before & 65535; s.history_high = before >> 16
        s.capture_low = after & 65535; s.capture_high = after >> 16
        publish(s); compare(s, name, i)

# Sweep every sixteenth native speed word plus selected adjacent boundaries.
# The randomized cases above also exercise the stopped-state publication.
for value in range(65536):
    if value % 16 and value not in [39, 159, 10239, 40799, 65535]: continue
    s = Rotation(); s.speed_word = value
    publish(s); compare(s, 'speed', ('speed word', value))

# Retained histories with first-capture count wrapping, gap patterns, slow
# rotation, restart and capture rollover. Peripheral registers are observable
# state only: no PEC engine or ignition output is emulated by this test.
s = Rotation(); s.rotation_flags = 32; s.equipment = 1
publish(s); compare(s, 'threshold', 'trajectory'); compare(s, 'reset', 'trajectory')
clock = 0xFF0000
for step in range(1800):
    if step % 400 == 0: compare(s, 'reset', ('trajectory', step))
    interval = [960, 48000, 50000, 160, 16][(step // 120) % 5]
    if step % 58 == 0: interval *= 3
    clock = (clock + interval) & 0xFFFFFF
    s.capture_low = clock & 65535; s.capture_high = clock >> 16
    mem.write16(ram(0xF7AA), s.capture_low); mem.write8(ram(0xF7A4), s.capture_high)
    compare(s, 'capture', ('trajectory', step))
    if step % 58 == 0: compare(s, 'period', ('trajectory', step))
    compare(s, 'speed', ('trajectory', step))
print(f'PASS {cases} OEM rotation, period and speed comparisons' +
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
