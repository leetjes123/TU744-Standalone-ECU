"""Unchanged-ROM speed initialization, ISR payload and complete input task.

SFR fields are data, not peripheral emulation. The capture oracle begins after
the register-bank/DPP prologue and stops before its epilogue; no ISR-context or
PEC transfer equivalence is implied by these payload comparisons.
"""
import ctypes as C
import random
import sys
from oem_harness import Rom,ram,library
from oem_types import VssInput
from vss_input_layout import words,octets

rom=Rom();mem=rom.mem;lib=library();rng=random.Random(0x29CCC)
entries=dict(init=0x29C64,capture=0x29FC2,update=0x29CCC)
for name in entries:
    f=getattr(lib,'oem_vss_input_'+name);f.argtypes=[C.POINTER(VssInput)];f.restype=None

def publish(s):
    for mapping,write in [(words,mem.write16),(octets,mem.write8)]:
        for n,a in mapping.items():write(ram(a),getattr(s,n))

cases=0
def compare(s,name,label):
    global cases
    getattr(lib,'oem_vss_input_'+name)(C.byref(s))
    rom.invoke(entries[name],stop_at=0x2A016 if name=='capture' else None)
    for mapping,read in [(words,mem.read16),(octets,mem.read8)]:
        for n,a in mapping.items():assert getattr(s,n)==read(ram(a)),(name,label,n,getattr(s,n),read(ram(a)))
    cases+=1

for name in entries:
    for case in range(1500):
        s=VssInput()
        for n,kind in s._fields_:setattr(s,n,rng.randrange(65536 if kind==C.c_uint16 else 256))
        s.stale_count=rng.choice([0,1,14,15,16,17,255]);s.captured_batch=rng.choice([0,1,2,4,255])
        s.source=case%4;s.acceleration_status=case%3
        s.fraction=rng.choice([0,1,65535]);s.source_fraction=rng.choice([0,1,65535])
        s.pecc5=rng.choice([0,1,254,255,256,65535])
        s.pulse_total=rng.choice([0,32766,32767,32768,65534,65535])
        if case%4==0:s.capture=s.previous_capture
        if case%4==1:s.capture=(s.previous_capture+1)&65535
        publish(s);compare(s,name,case)

for source in [0,1,2]:
    for value in [0,1,174,175,176,51199,51200,51201,65535]:
        for fault in [0,2]:
            for retained in [0,8]:
                for accel in [0,1,174,175,176,254,255]:
                    s=VssInput();s.source=source;s.source_a=s.source_b=value
                    s.fd08=fault;s.status=retained;s.acceleration_input=accel
                    publish(s);compare(s,'update',('source boundary',source,value,fault,retained,accel))

for accel in range(256):
    s=VssInput();s.source=1;s.acceleration_input=accel
    publish(s);compare(s,'update',('all acceleration bytes',accel))

# Retained physical pulses, stop timeout, restart, source changes and faults.
s=VssInput();s.fd06=s.fd00=256;publish(s);compare(s,'init','retained')
for tick in range(1200):
    s.fd06=256 if tick<800 else 0
    s.source=1 if tick<1000 else 2
    s.source_a=(tick*71)&65535;s.source_b=(tick*131)&65535
    s.timer=(tick*1237)&65535
    s.fd08=(s.fd08|2) if 900<=tick<950 else (s.fd08&0xFFFD)
    for n in ['fd06','source','source_a','source_b','timer','fd08']:
        (mem.write16 if n in words else mem.write8)(ram((words|octets)[n]),getattr(s,n))
    if tick%3==0 and not 300<=tick<500:compare(s,'capture',('retained',tick))
    compare(s,'update',('retained',tick))
print(f'PASS {cases} OEM vehicle-speed input/init/capture comparisons'+
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
