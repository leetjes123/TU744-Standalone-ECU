"""Differential dwell controller checks against unchanged TU5JP instructions.

Supply the scheduling/diagnostic eligibility explicitly. This proves the
arithmetic and retained controller state, not eligibility or electrical CC9.
"""
import ctypes as C
import random
from oem_harness import Rom, ram, library


class Dwell(C.Structure):
    _fields_ = [('correction', C.c_int16), ('duration', C.c_uint16),
                ('previous_fallback', C.c_uint8)]


rom = Rom()
mem = rom.mem
lib = library()
lib.oem_dwell_stock_update.argtypes = [C.POINTER(Dwell), C.c_uint16, C.c_uint16,
    C.c_uint8, C.c_uint8, C.c_uint8]
lib.oem_dwell_stock_update.restype = None
assert int.from_bytes(rom.data[0x14E00:0x14E02], 'little') == 63
assert int.from_bytes(rom.data[0x14DFC:0x14DFE], 'little') == 125
assert int.from_bytes(rom.data[0x14DFE:0x14E00], 'little') == 1875
assert rom.data[0x10DD6] == 251
mem.write16(ram(0x9718), 0x972E)
mem.write16(ram(0x971E), 0x9734)
rng = random.Random(0x38BDC)
cases = 0


def compare(state, base, measured, missing, fallback, disabled):
    global cases
    before = (state.correction, state.duration, state.previous_fallback)
    mem.write16(0xFD1C, (1 << 11) | (fallback << 1) | (missing << 2))
    mem.write16(0xFD16, 0)
    mem.write16(ram(0xB2C6), disabled)
    mem.write8(ram(0x9294), 0)
    for at in [0xB2D2, 0xB2D4, 0xB2D6, 0xB2D8]: mem.write16(ram(at), 0)
    mem.write16(ram(0x972E), state.correction & 65535)
    mem.write16(ram(0x9734), state.duration)
    mem.write8(ram(0x835C), state.previous_fallback)
    mem.write16(ram(0x973A), base)
    mem.write16(ram(0x9738), measured)
    rom.invoke(0x38BDC)
    lib.oem_dwell_stock_update(C.byref(state), base, measured, missing, fallback, disabled)
    expected = (C.c_int16(mem.read16(ram(0x972E))).value,
                mem.read16(ram(0x9734)), mem.read8(ram(0x835C)) & 1)
    assert (state.correction, state.duration, state.previous_fallback) == expected, (
        before, base, measured, missing, fallback, disabled, expected, bytes(state))
    cases += 1


for correction in [-32768, -1000, -125, 0, 1, 1875, 32767]:
    for base in [0, 124, 125, 625, 7500, 65000, 65535]:
        for measured in [0, 123, 625, 7000, 65535]:
            for flags in range(16):
                compare(Dwell(correction, 0, flags & 1), base, measured,
                        (flags >> 1) & 1, (flags >> 2) & 1, (flags >> 3) & 1)

# Retained correction and prior-fallback history over independent input changes.
s = Dwell(0, 0, 1)
for i in range(2000):
    compare(s, rng.choice([625, 1250, 3750, 7500]), rng.randrange(9000),
            int(i % 9 == 0), int(i % 19 < 3), int(i % 31 == 0))
print(f'PASS {cases} unchanged-ROM dwell controller comparisons')
