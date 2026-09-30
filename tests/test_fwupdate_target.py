"""Run the linked firmware-update path (hal_firmware_update + FWUPDATE.A66).

Run after build.py stock-95080. A K-line model (every ECU byte echoes back on
its own receiver) feeds the byte stream an LRE-B4 host sends, and an AMD
Am29F400B bottom-boot command model owns the flash. Checks the LRE-B4 wire
protocol (probe, 03, erase, 4-word program, read word, reset) and the two
additions (07 status, 08 page sums), RAM-only execution while flash is busy,
and failure counting. Not a test of physical flash, bus timing or the board.
"""
import sys
if '--stock-95080' not in sys.argv: sys.argv.append('--stock-95080')
import test_target as target

mem, cpu = target.mem, target.cpu
S0TBUF, S0RBUF, S0RIC = 0xFEB0, 0xFEB2, 0xFF6E
host = []          # bytes still to arrive at the ECU receiver
received = []      # bytes the ECU transmitted
rx = {'buf': 0, 'rir': 0, 'empty_polls': 0}


def ric_read(_):
    if not rx['rir'] and host:
        rx['buf'] = host.pop(0)
        rx['rir'] = 1
    rx['empty_polls'] = 0 if rx['rir'] else rx['empty_polls'] + 1
    return rx['rir'] << 7


def ric_write(_, value):
    rx['rir'] = (value >> 7) & 1


def tbuf_write(_, value):
    received.append(value & 255)
    host.insert(0, value & 255)  # single-wire echo arrives first


mem.claim(S0RIC, ric_read, ric_write)
mem.claim(S0RBUF, lambda _: rx['buf'])
mem.claim(S0TBUF, None, tbuf_write)

SECTORS = [(0, 0x4000), (0x4000, 0x6000), (0x6000, 0x8000), (0x8000, 0x10000)] + \
          [(a, a + 0x10000) for a in range(0x10000, 0x80000, 0x10000)]
original_read16, original_write16, original_read8 = mem.read16, mem.write16, mem.read8
flash = {'state': 0, 'busy': None}
operations = []


def in_ram():
    return 0x380000 <= cpu.pc() < 0x384000


def write16(at, value):
    if at >= 0x80000 or 0xe000 <= at < 0x10000:
        return original_write16(at, value)
    assert in_ram(), ('flash command outside RAM', hex(cpu.pc()))
    s = flash['state']
    if at == 0xaaa and value == 0xf0 or value == 0xf0 and s == 0:
        flash['state'] = 0; flash['busy'] = None
        return
    expected = {0: (0xaaa, 0xaa), 1: (0x554, 0x55), 3: (0xaaa, 0xaa), 4: (0x554, 0x55)}
    if s in expected:
        assert (at, value) == expected[s], (s, hex(at), hex(value))
        flash['state'] += 1
    elif s == 2:
        assert at == 0xaaa and value in (0xa0, 0x80)
        flash['state'] = 6 if value == 0xa0 else 3
    elif s == 5:
        assert value == 0x30
        start, end = next(r for r in SECTORS if r[0] <= at < r[1])
        target.image[start:end] = b'\xff' * (end - start)
        flash['busy'] = dict(at=at, value=0xffff, reads=5)
        operations.append(('erase', start)); flash['state'] = 0
    elif s == 6:
        ok = (target.image[at] & (value & 255)) == value & 255 and \
             (target.image[at + 1] & (value >> 8)) == value >> 8
        target.image[at] &= value & 255
        target.image[at + 1] &= value >> 8
        flash['busy'] = dict(at=at, value=value, reads=2, fail=not ok)
        operations.append(('program', at)); flash['state'] = 0
    else:
        raise AssertionError(s)


def read8(at):
    assert not (flash['busy'] and at < 0x80000 and not 0xe000 <= at < 0x10000), \
        ('byte/ROM read while flash busy', hex(at), hex(cpu.pc()))
    return original_read8(at)


def read16(at):
    busy = flash['busy']
    if busy and at < 0x80000 and not 0xe000 <= at < 0x10000:
        assert at == busy['at'] and in_ram(), ('fetch/data while busy', hex(at), hex(cpu.pc()))
        if busy['reads']:
            busy['reads'] -= 1
            return (busy['value'] ^ 0x80) & 0xffdf
        if busy.get('fail'):
            return ((busy['value'] ^ 0x80) & 0xffdf) | 0x20
        flash['busy'] = None
    return original_read16(at)


mem.write16, mem.read16, mem.read8 = write16, read16, read8


class Reset(Exception):
    pass


def reset():
    raise Reset()


cpu.on_reset = reset


def run_until_idle(limit=3000000):
    """Step until the handler has consumed every byte and polls an empty
    receiver (a transmit echo wait never polls an empty receiver 50 times)."""
    rx['empty_polls'] = 0
    for _ in range(limit):
        cpu.step()
        if rx['empty_polls'] > 50 and not host:
            return
    raise AssertionError(('handler did not settle', hex(cpu.pc())))


def exchange(request, reply_length):
    received.clear()
    host.extend(request)
    run_until_idle()
    echo_free = received
    assert len(echo_free) == reply_length, (request, echo_free)
    return bytes(echo_free)


def program_packet(offset, data):
    """Exactly what the Wizard (firmware_flash.cpp) sends for 8 bytes."""
    packet = [0x02, offset // 0x4000, (offset % 0x4000) >> 8, offset & 255]
    for i in range(0, 8, 2):
        packet += [data[i + 1], data[i]]
    return packet


def page_sums(page):
    s1 = s2 = 0
    for at in range(page * 0x4000, page * 0x4000 + 0x4000, 2):
        s1 = (s1 + target.image[at] + (target.image[at + 1] << 8)) & 0xffff
        s2 = (s2 + s1) & 0xffff
    return bytes([0x5a, s1 >> 8, s1 & 255, s2 >> 8, s2 & 255])


checks = 0
target.call('ecu_init', {8: 1})
entry = target.symbols['hal_firmware_update']
cpu.reset(ip=entry & 0xffff, csp=entry >> 16)
cpu.dpp = [0, 1, 0xE0, 3]
cpu.set_rw(0, 0xBE00)
for _ in range(200000):
    cpu.step()
    if in_ram():
        break
assert in_ram(), 'handler not entered'
run_until_idle()
checks += 1

# The Wizard's safety probe: echo 66 then FFh from the handler.
assert exchange([0x66], 1) == b'\xff'; checks += 1
assert exchange([0x03], 1) == b'\x57'; checks += 1
assert exchange([0x07], 2) == b'\x5a\x00'; checks += 1
# Erase the sector at 0x60000 (bank 18h), as FLASH_SECTORS in the Wizard.
target.image[0x60000:0x60010] = bytes(16)
assert exchange([0x05, 0x18, 0x00, 0x00], 0) == b''
assert operations[-1] == ('erase', 0x60000) and target.image[0x60000:0x60010] == b'\xff' * 16
checks += 1
# Small bottom-boot sector 0x6000 is addressed as bank 1, offset 2000h.
exchange([0x05, 0x01, 0x20, 0x00], 0)
assert operations[-1] == ('erase', 0x6000); checks += 1
data = bytes(range(0x10, 0x18))
assert exchange(program_packet(0x60008, data), 0) == b''
assert bytes(target.image[0x60008:0x60010]) == data
assert [op for op in operations[-4:]] == [('program', 0x60008 + 2 * i) for i in range(4)]
checks += 1
assert exchange([0x01, 0x18, 0x00, 0x08], 2) == bytes([0x11, 0x10]); checks += 1
assert exchange([0x08, 0x18], 5) == page_sums(0x18); checks += 1
assert exchange([0x07], 2) == b'\x5a\x00'; checks += 1
# Programming a 1 over a 0 fails (DQ5); the handler counts it.
exchange(program_packet(0x60008, b'\xff' * 8), 0)
assert exchange([0x07], 2) == b'\x5a\x04'; checks += 1
assert exchange([0x42], 1) == b'\xff'; checks += 1
received.clear()
host.extend([0x06, 0x00, 0x00, 0x00])
try:
    run_until_idle()
    raise AssertionError('06 did not reset')
except Reset:
    assert in_ram()
    checks += 1
print(f'PASS {checks} linked firmware-update handler cases; LRE-B4 wire protocol, '
      'erase/program in RAM only, status and page sums')
