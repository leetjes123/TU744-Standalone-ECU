"""Standard single-slot detector path versus the unchanged hash-verified ROM."""
from pathlib import Path
import ctypes as C
import random
import re
from oem_harness import Rom, ram, ROOT


def structure(name):
    header = (ROOT/'include/knock.h').read_text()
    body = re.search(r'typedef struct \{([^{}]*)\} '+name+r';', header)[1]
    fields = []
    types = {'u8': C.c_uint8, 'u16': C.c_uint16, 'u32': C.c_uint32, 's16': C.c_int16}
    for kind, declarations in re.findall(r'\b(u8|u16|u32|s16)\s+([^;]+);', body):
        for declaration in declarations.split(','):
            field, count = re.fullmatch(r'\s*(\w+)(?:\[(\d+)\])?\s*', declaration).groups()
            fields.append((field, types[kind] * int(count) if count else types[kind]))
    return type(name, (C.Structure,), {'_fields_': fields})


State, Config = structure('KnockState'), structure('KnockConfig')
lib = C.CDLL(str(ROOT/'build/oem/oem.dll'))
lib.knock_detect.argtypes = [C.POINTER(State), C.POINTER(Config), C.c_uint8]
rom = Rom()
cfg = Config(); cfg.eligible=1; cfg.divisor=16
cfg.gain_code[:] = [0,1,2,3,5,6,7]
rng = random.Random(0x49522)
cases = [(raw,32,4,40,37,37) for raw in [37,69,116,117,119]]
cases += [(raw,64,4,80,37,37) for raw in [226,227]]
cases += [(raw,ref,gain,threshold,offset,null) for raw,ref,gain,threshold,offset,null in
          ((rng.randrange(256),rng.randrange(1,256),rng.randrange(7),rng.randrange(16,81),
            rng.randrange(25,49),rng.randrange(25,49)) for _ in range(2000))]
for raw, reference, gain, threshold, offset, null in cases:
    s = State(); s.reference=reference;s.gain=gain;s.offset=offset;s.null_start=null
    cfg.threshold=threshold
    for address,value in [(0xFD40,0x8000),(0xFD16,4),(0xFD42,0),(0xFD44,0),(0x9708,0)]:
        rom.mem.write16(ram(address),value)
    for address,value in [(0x933C,0),(0xF882,0),(0x9348,240),(0x9349,240),(0x93B0,0),
                          (0x9355,reference),(0x935F,gain),(0x9340,threshold),
                          (0xE506,max(0,raw-offset)),(0xE50E,raw),(0x932F,null)]:
        rom.mem.write8(address if address >= 0xC000 else ram(address),value)
    rom.invoke(0x49522)
    lib.knock_detect(C.byref(s), C.byref(cfg),raw)
    actual=(s.decision,s.ratio,s.reference,s.gain,s.gain_code)
    expected=(int(bool(rom.mem.read16(0xFD40)&0x4000)),rom.mem.read8(0xF889),
              rom.mem.read8(ram(0x9355)),rom.mem.read8(ram(0x935F)),rom.mem.read8(ram(0x936A)))
    assert actual==expected, ((raw,reference,gain,threshold,offset,null),actual,expected)
print(f'PASS {len(cases)} unchanged-ROM detector comparisons')
