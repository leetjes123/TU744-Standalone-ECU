"""Native completion masks/counters, including retained initialization and clear.

Each oracle call executes the unchanged TU5JP ROM. The group event identities
remain raw inputs; this does not supply absent physical monitor producers.
"""
import ctypes as C
import itertools
import random
import sys
from oem_harness import Rom, ram, library
from oem_types import Readiness

rom = Rom()
lib = library()
rng = random.Random(0x6BEDA)
descriptors = [0xB282, 0xB2E2, 0xB2DE, 0xB2CA, 0xB2AE, 0xB2AC,
               0xB2A0, 0xB2A4, 0xB2A2, 0xB27E, 0xB27A]
words = {'fd02': 0xFD02, 'config_a': 0x959C, 'config_b': 0x959E, 'startup_flags': 0xFD14}
octets = {'supported': 0x952C, 'pending': 0xAA6D, 'once': 0x8B3E}
entries = {'init': 0x6C116, 'update': 0x6BEDA, 'clear': 0x6C18E}
for name in entries:
    f = getattr(lib, 'oem_readiness_' + name)
    f.argtypes = [C.POINTER(Readiness)]
    f.restype = None


def seed(s):
    for i, a in enumerate(descriptors): rom.mem.write16(ram(a), s.descriptor[i])
    for name, a in words.items(): rom.mem.write16(ram(a), getattr(s, name))
    for name, a in octets.items(): rom.mem.write8(ram(a), getattr(s, name))
    for i in range(5): rom.mem.write8(ram(0xAA68+i), s.count[i])


def compare(s, context):
    for i, a in enumerate(descriptors):
        assert rom.mem.read16(ram(a)) == s.descriptor[i], (context, hex(a))
    for name, a in words.items():
        assert rom.mem.read16(ram(a)) == getattr(s, name), (context, name)
    for name, a in octets.items():
        assert rom.mem.read8(ram(a)) == getattr(s, name), (context, name, hex(a))
    for i in range(5):
        assert rom.mem.read8(ram(0xAA68+i)) == s.count[i], (context, 'count', i)


cases = 0
def invoke(s, name, reseed=True):
    global cases
    if reseed: seed(s)
    getattr(lib, 'oem_readiness_' + name)(C.byref(s))
    rom.invoke(entries[name])
    compare(s, (name, cases))
    cases += 1


# Cross every configuration bit that affects the support mask, including both
# OR inputs for bit2, with retained/lost-history startup and byte edge states.
for pattern in range(128):
    for lost in (0, 0x8000):
        for retained in (0, 0xFF):
            s = Readiness()
            s.fd02 = ((pattern & 3) << 12) | 0xCFFF
            s.config_a = (0x9FB7 | ((pattern & 4) << 11) | ((pattern & 8) << 11) |
                          ((pattern & 32) >> 2) | ((pattern & 64)))
            s.config_b = 0xFFFE | ((pattern >> 4) & 1)
            s.startup_flags = 0x7FFF | lost
            s.pending = retained
            for i in range(5): s.count[i] = rng.randrange(256)
            invoke(s, 'init')

# Exhaust all four bit0/bit1 combinations of every descriptor in each group.
# Count254->255 and already255 distinguish increment from once-bit retention.
groups = [(1, [0]), (4, [1, 2]), (8, [3]), (32, [4, 5, 6, 7, 8]), (64, [9, 10])]
for mask, indices in groups:
    for values in itertools.product(range(4), repeat=len(indices)):
        for counter in (0, 1, 2, 254, 255):
            for once in (0, 255):
                s = Readiness()
                s.pending = mask | 0x92  # native forcibly-cleared bits
                s.once = once
                for i in range(5): s.count[i] = counter
                for i, value in zip(indices, values): s.descriptor[i] = 0xA5F0 | value
                invoke(s, 'update')

# All pending masks, random unrelated descriptor/config bits, and reset values.
for case in range(1024):
    s = Readiness.from_buffer_copy(bytes(rng.randrange(256) for _ in range(C.sizeof(Readiness))))
    s.pending = case & 255
    invoke(s, 'clear' if case & 256 else 'update')

# Keep C and ROM histories independently across monitor changes, startup rearm
# and explicit clear. Only actual external inputs are published on each step.
s = Readiness()
s.fd02, s.config_a, s.config_b, s.startup_flags = 0x3000, 0x6048, 1, 0x8000
invoke(s, 'init')
for step in range(1800):
    for i, a in enumerate(descriptors):
        value = rng.choice([0, 1, 2, 3, 0x1083, 0x2002])
        s.descriptor[i] = value
        rom.mem.write16(ram(a), value)
    if step % 37 == 0:
        s.startup_flags = 0
        rom.mem.write16(0xFD14, 0)
        invoke(s, 'init', False)
    elif step % 113 == 0:
        invoke(s, 'clear', False)
    else:
        invoke(s, 'update', False)
print(f'PASS {cases} OEM completion-mask/counter/init/clear comparisons' +
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
