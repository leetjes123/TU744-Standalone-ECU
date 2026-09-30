"""Execute the linked reset vector through C initialization with dirty RAM.

WDTCON inputs are explicit register observations, not simulated physical resets.
The full startup code is unchanged in these tests; main is the stop boundary.
"""
import re
import test_target as target

listing = (target.BUILD / 'TU5JP.m66').read_text()
variables = {name: int(at, 16) for at, name in
             re.findall(r'^\s+([0-9A-F]{6})H\s+(\w+)\s+VAR', listing, re.M)}
raw_at = variables['reset_capture_raw']
marker_at = variables['reset_capture_marker']
main = target.symbols['main']
startup = int(re.search(r'^\s+([0-9A-F]{6})H\s+\?C_STARTUP\s+LABEL', listing, re.M)[1], 16)
# The emulator does not model CP pipeline delay: check the linked barrier.
assert target.image[startup:startup + 2] == b'\xe6\x08'  # MOV CP,#bank
assert target.image[startup + 4:startup + 6] == b'\xcc\x00'  # NOP


def boot(raw, fill):
    mem = target.Memory(fault_on_unmapped=True)
    mem.attach(target.Device('flash', 0, len(target.image), target.image, writable=False))
    ram = target.Device('ram', 0x380000, 0x8000, writable=True)
    ram.data[:] = bytes([fill]) * len(ram.data)
    mem.attach(ram)
    mem.iram[:] = bytes([fill]) * len(mem.iram)
    mem.xram[:] = bytes([fill]) * len(mem.xram)
    cpu = target.Cpu(mem)
    cpu.reset()
    mem.write16(0xFFAE, raw)
    # Deliberately seed an apparently valid stale observation from a prior boot.
    mem.write16(marker_at, 0x5253)
    mem.write16(raw_at, raw ^ 0xffff)
    services = 0
    for steps in range(300000):
        if cpu.pc() == main:
            break
        if mem.read16(cpu.pc()) == 0x58A7:  # SRVWDT prefix
            services += 1
            # WDTR is cleared by service on silicon. Other peripherals are
            # register storage here; do not claim reset/timing model accuracy.
            mem.write16(0xFFAE, mem.read16(0xFFAE) & ~2)
        cpu.step()
        assert not cpu.traps, (hex(cpu.pc()), cpu.traps)
    else:
        raise AssertionError(('startup did not reach main', hex(cpu.pc())))
    assert services > 100, services  # Real RAM clear loop was executed.
    assert not cpu.einit_done and not cpu.wdt_disabled
    assert mem.read16(0xFFAE) == 1  # Original flags no longer available here.
    assert mem.read16(raw_at) == raw, (raw, mem.read16(raw_at))
    assert mem.read16(marker_at) == 0x5253
    # Both application initializers must preserve the captured observation.
    target.mem, target.cpu = mem, cpu
    target.call('board_init', {})
    target.call('ecu_init', {8: 0})
    assert mem.read16(raw_at) == raw and mem.read16(marker_at) == 0x5253
    return steps


observations = [0, 2, 4, 6, 0x0c, 0x1c, 0x801f, 0xffff]
for raw in observations:
    for fill in [0, 0xa5, 0xff]:
        boot(raw, fill)
print(f'PASS {len(observations) * 3} Keil reset-vector/startup cases with dirty RAM and initialization preservation')
