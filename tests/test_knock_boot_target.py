"""Knock IC (CC195) boot sequence: OEM ROM versus the linked standalone.

Both images run from reset on the c167re emulator. The recorded port events
are turned into the sequence of logic levels at the seven CC195 control pins,
assuming the IC's documented internal pull-ups before the MCU drives a line
(CC195 spec v6.9a p.6). The standalone must produce the same ordered states,
hold the port-init state at least as long as the OEM's hardware-paced
minimum, and map every filter setting to the same BF2 level as the ROM's
sub_493DC jump table. Emulator code timing is approximate (2 clocks per
instruction, no bus wait states), so the OEM hold is taken from the Keil run.
Evidence: engines/TU5JP/archive/36-knock-ic-cc195-init-and-calibration.md.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from oem_repo import oem_repo
REPO = oem_repo()
sys.path.insert(0, str(REPO / 'src'))
from c167re.emu.machine import Machine  # noqa: E402
from c167re.emu.tools.probe import build as oem_build  # noqa: E402

PROFILE = next((p for p in ('stock-95080', 'engine-experimental') if f'--{p}' in sys.argv), 'c166')
BUILD = ROOT / 'build' / PROFILE
LINES = [('P3', 3), ('P3', 2), ('P3', 1), ('P3', 5), ('P3', 6), ('P8', 0), ('P8', 5)]
NAMES = ['G2', 'G1', 'G0', 'KTI', 'KSA3', 'MF', 'BF2']
PULLUP = (1,) * len(LINES)
HOLD_MIN_MS = 15.61          # 1244 SPI bytes at 1 Mbit/s + code, archive/36 s3
# OEM hold measured by running the unchanged ROM in the Keil C166 simulator with its
# own bus wait states (tests/keil_knock_boot.py): 320836 CPU states at 20 MHz.
KEIL_OEM_HOLD_MS = 320836 / 20000.0
CLOCK = 20_000_000


def load_hex(path):
    image, base = bytearray(b'\xff' * 0x80000), 0
    for line in path.read_text().splitlines():
        b = bytes.fromhex(line[1:])
        n, at, kind = b[0], int.from_bytes(b[1:3], 'big'), b[3]
        if kind == 0:
            image[base + at:base + at + n] = b[4:4 + n]
        elif kind == 2:
            base = int.from_bytes(b[4:6], 'big') << 4
        elif kind == 4:
            base = int.from_bytes(b[4:6], 'big') << 16
    return bytes(image)


def states(machine):
    """Ordered (time_ms, levels) at the IC pins, one entry per level change."""
    level = list(PULLUP)
    out = [(0.0, tuple(level))]
    for t, port, pin, value in sorted(machine.ports.events):
        if (port, pin) in LINES:
            level[LINES.index((port, pin))] = value
            if tuple(level) != out[-1][1]:
                out.append((t * 1000.0 / CLOCK, tuple(level)))
    return out


def show(seq):
    return ['%9.4f ms  %s' % (t, ' '.join(f'{n}={v}' for n, v in zip(NAMES, s))) for t, s in seq]


# --- OEM: reset through sub_493DC's P8.5 write (init-list entry 0x140F0) ---
oem = oem_build(image=str(REPO / 'bins/M744_C167_FULL.bin'),
                eeprom=str(REPO / 'bins/M7.4.4 EEPROM ORI_ImmoOff.BIN'))
oem.gen['gen0'].configure(enabled=False)
assert oem.run(50_000_000, breakpoints={0x84987C}) == 'breakpoint', 'OEM did not reach sub_4987C'
oem_seq = states(oem)

# --- standalone: reset through main's initialization to the first ecu_poll ---
listing = (BUILD / 'TU5JP.m66').read_text()
symbols = {n: int(a, 16) for a, n in re.findall(r'^\s+([0-9A-F]{6})H\s+(\w+)\s+LABEL', listing, re.M)}
image = load_hex(BUILD / 'TU5JP.H86')
std = Machine(image)
assert std.run(50_000_000, breakpoints={symbols['ecu_poll']}) == 'breakpoint', 'standalone did not reach ecu_poll'
std_seq = states(std)

print('OEM:'); print('\n'.join(show(oem_seq)))
print(f'standalone ({PROFILE}):'); print('\n'.join(show(std_seq)))
assert [s for _, s in std_seq] == [s for _, s in oem_seq], 'knock IC pin-state sequence differs from the OEM'

# Hold: MF falls (state 1) until the gain leaves 111 (state 2).
std_hold = std_seq[2][0] - std_seq[1][0]
oem_hold = oem_seq[2][0] - oem_seq[1][0]
# The standalone hold is T1-paced, so the emulator times it exactly.
assert std_hold >= HOLD_MIN_MS and abs(std_hold - KEIL_OEM_HOLD_MS) < 0.05, (std_hold, KEIL_OEM_HOLD_MS)
# Release: G1, KTI, KSA3 fall within a few instructions of each other.
release = std_seq[-1][0] - std_seq[2][0]
assert release < 0.01, release

# --- filter select: every kHz setting drives BF2 like the ROM's jump table ---
rom = (REPO / 'bins/M744_C167_FULL.bin').read_bytes()
table = [int.from_bytes(rom[0x122A0 + 2 * i:0x122A2 + 2 * i], 'little') for i in range(12)]


def rom_code(khz):
    if not 5 <= khz <= 16:
        return 0
    label = 0x40000 + table[khz - 5]          # segment 0x84 -> file 0x4xxxx
    assert rom[label] == 0xE1 and rom[label + 1] & 0x0F == 0xC, hex(label)  # MOVB RL6,#imm
    return rom[label + 1] >> 4


assert [rom_code(k) for k in (5, 6, 7, 8, 9, 10, 12, 14, 16)] == [8, 0xA, 0xB, 0xC, 6, 0, 2, 3, 4]
entry = symbols['board_knock_ic_release']
for khz in range(0, 20):
    std.cpu.reset(ip=entry & 0xFFFF, csp=entry >> 16)
    std.cpu.dpp = [0, 1, 0xE0, 3]
    std.cpu.set_rw(0, 0xBE00)
    std.cpu.set_rw(8, khz)
    std.mem.write16(0xFE52, 0xFFFF)            # T1 past the hold: no wait
    stack = std.cpu.sp
    for _ in range(2000):
        if std.mem.read16(std.cpu.pc()) == 0xDB and std.cpu.sp == stack:
            break
        std.step()
    else:
        raise AssertionError(('board_knock_ic_release did not return', khz))
    assert std.ports.pin_state('P8', 5) == (rom_code(khz) >> 2) & 1, khz

print(f'PASS knock IC boot: {len(std_seq)} identical pin states; standalone hold '
      f'{std_hold:.3f} ms vs OEM {KEIL_OEM_HOLD_MS:.3f} ms (Keil; emulator {oem_hold:.3f} ms)'
      f'; release {release * 1000:.1f} us; BF2 matches ROM for 0..19 kHz')
