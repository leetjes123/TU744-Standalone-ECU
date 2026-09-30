"""Native divider phases, adaptive period, and actual RTOS release outcomes."""
import ctypes as C
import random
import struct
import sys
from oem_harness import Rom,library

class Cadence(C.Structure):
    _fields_=[('period',C.c_uint16),('countdown',C.c_uint8*5),
              ('rejected',C.c_uint8*5),('adaptation',C.c_uint8),
              ('speed',C.c_uint8),('enabled',C.c_uint8)]

rom=Rom();mem=rom.mem;lib=library();rng=random.Random(0x28D9A)
for name,restype,args in [('init',None,[]),('step',C.c_uint8,[]),
                           ('rejected',None,[C.c_uint8])]:
    f=getattr(lib,'oem_cadence_'+name)
    f.argtypes=[C.POINTER(Cadence),*args];f.restype=restype

for i,value in enumerate(struct.unpack_from('<27H',rom.data,0x199B4)):
    mem.write16(0xE000+2*i,value)

cases=0
for case in range(768):
    s=Cadence()
    s.period=rng.randrange(65536);s.speed=case%256
    s.adaptation=[0,1,2,5,127,128,129,255][case%8]
    s.enabled=case%32
    for i in range(5):
        s.countdown[i]=[0,1,2,5,10,20,100,127,128,129,255][(case+i)%11]
        s.rejected[i]=rng.choice([0,1,254,255])
    # Initialize the same countdown/period region, preserving unrelated bytes.
    if case%3==0:
        lib.oem_cadence_init(C.byref(s))
        assert list(s.countdown)==[1,2,4,10,20] and s.period==781
    for address in range(0xE2F4,0xE36C):mem.write8(address,0)
    for address in range(0xE04C,0xE060):mem.write8(address,0)
    reject=rng.randrange(32)
    for i in range(5):
        descriptor=0xE400+16*i
        for j,value in enumerate((0xE480+16*i,i+1,0 if reject&(1<<i) else 1,descriptor+8)):
            mem.write16(descriptor+2*j,value)
        mem.write8(descriptor+8,0)
        mem.write16(0x3800DC+2*i,descriptor if s.enabled&(1<<i) else 0)
        mem.write8(0x3800F1+i,s.countdown[i]);mem.write8(0x3800EC+i,s.rejected[i])
    mem.write16(0x3800E8,s.period);mem.write16(0xE376,s.period)
    mem.write8(0x3800EA,s.adaptation);mem.write8(0xF8AC,s.speed)
    due=lib.oem_cadence_step(C.byref(s))
    lib.oem_cadence_rejected(C.byref(s),due&reject)
    rom.invoke(0x28D9A)
    assert s.period==mem.read16(0x3800E8)==mem.read16(0xE376),(case,'period')
    assert s.adaptation==mem.read8(0x3800EA),(case,'adaptation')
    for i in range(5):
        assert s.countdown[i]==mem.read8(0x3800F1+i),(case,i,'countdown')
        assert s.rejected[i]==mem.read8(0x3800EC+i),(case,i,'rejection')
        assert mem.read8(0xE408+16*i)==bool(due&(1<<i) and not reject&(1<<i)),(case,i,'accepted')
    cases+=1
print(f'PASS {cases} OEM adaptive-cadence/RTOS release cases'+
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
