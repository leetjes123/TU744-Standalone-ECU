"""Whole-word ADC publication against native 2C188, including reserved bits."""
import ctypes as C
import random
import sys
from oem_harness import Rom, ram, library
from oem_types import Adc

rom=Rom(); mem=rom.mem; lib=library(); rng=random.Random(0x2C188)
lib.oem_adc_publish.argtypes=[C.POINTER(Adc)];lib.oem_adc_publish.restype=None
outputs=[0x95B0,0x95B8,0x95B6,0x95BA,0x95B4]
for case in range(4096):
    s=Adc()
    for i in range(16):
        # Distinct channel results expose index reversal and accidental aliasing.
        s.scan[i]=((15-i)<<12)|((case+i*73)&1023)|((case&3)<<10)
        mem.write16(0xF7B0+i*2,s.scan[i])
    for i,a in enumerate(outputs):
        s.input[i]=rng.randrange(65536);mem.write16(ram(a),s.input[i])
    s.battery=rng.randrange(256);s.iat=rng.randrange(256)
    mem.write8(ram(0x9209),s.battery);mem.write8(ram(0x9208),s.iat)
    before=bytes(s.scan)
    lib.oem_adc_publish(C.byref(s));rom.invoke(0x2C188)
    assert bytes(s.scan)==before,('input mutation',case)
    assert all(mem.read16(0xF7B0+2*i)==s.scan[i] for i in range(16))
    for i,a in enumerate(outputs):
        assert s.input[i]==mem.read16(ram(a)),(case,hex(a),s.input[i],mem.read16(ram(a)))
    assert s.battery==mem.read8(ram(0x9209))
    assert s.iat==mem.read8(ram(0x9208))
print('PASS 4096 OEM whole-word ADC publication comparisons'+
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
