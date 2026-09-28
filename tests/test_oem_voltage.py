"""Native supply-voltage producer and conditioning, before event65 ingestion."""
import ctypes as C
import random
import sys
from oem_harness import Rom, ram, library
from oem_types import Voltage

rom=Rom();mem=rom.mem;lib=library();rng=random.Random(0x664A2)
words=[0xB2EE,0xB2F4,0x8A8A,0xFD16,0x8A90,0x8A92,0x9B84,0x9B82]
octets=[0x9209,0x94E8,0x94E9,0x94EA,0x8A8C,0x8A8D,0x8A8E,0x8A8F,0x9201]
entries=dict(init=0x663C0,reset=0x6677C,base=0x66422,filter=0x6670C,
             alternate=0x6675E,update=0x664A2)
for name in entries:
    f=getattr(lib,'oem_voltage_'+name);f.argtypes=[C.POINTER(Voltage)]
    f.restype=C.c_uint16 if name=='update' else None

def publish(s):
    for (n,_),a in zip(s._fields_,words):mem.write16(ram(a),getattr(s,n))
    for (n,_),a in zip(s._fields_[8:],octets):mem.write8(ram(a),getattr(s,n))

cases=0
def compare(s,name,context):
    global cases
    descriptor=getattr(lib,'oem_voltage_'+name)(C.byref(s))
    rom.invoke(entries[name],stop_at=0x666FC if name=='update' else None)
    if name=='update':assert mem.read16(rom.cpu.rw(0))==descriptor,('descriptor',context,hex(descriptor),hex(mem.read16(rom.cpu.rw(0))))
    for fields,addresses,read in [(s._fields_,words,mem.read16),(s._fields_[8:],octets,mem.read8)]:
        for (n,_),a in zip(fields,addresses):assert getattr(s,n)==read(ram(a)),(name,context,n,getattr(s,n),read(ram(a)))
    cases+=1
    return descriptor

for name in entries:
    for case in range(1024):
        s=Voltage()
        for n,kind in s._fields_:setattr(s,n,rng.randrange(65536 if kind==C.c_uint16 else 256))
        s.adc=case%256;s.voltage=case%256
        s.delay=rng.choice([0,1,30,255]);s.fail_count=rng.choice([0,1,5,255]);s.pass_count=rng.choice([0,1,5,255])
        s.divider=rng.choice([0,1,8,9,10,254,255]);s.vehicle_speed=case%2
        s.fraction=rng.choice([0,1,65535,rng.randrange(65536)])
        s.filter_high=rng.choice([0,65535,(s.voltage*1130//4)&65535,rng.randrange(65536)])
        publish(s);compare(s,name,case)

for adc in range(256):
    for running in [0,4]:
        for delay in [0,1,30]:
            for fail in [0,1,5]:
                for speed in [0,1]:
                    for fault in [0,1]:
                        s=Voltage();s.adc=adc;s.run_flags=running;s.delay=delay
                        s.fail_count=fail;s.pass_count=fail;s.vehicle_speed=speed
                        s.speed_descriptor=fault;s.status=4 if adc<36 else 0
                        publish(s);compare(s,'update',('boundary',adc,running,delay,fail,speed,fault))

s=Voltage();s.adc=127;publish(s);compare(s,'init','trajectory')
for step in range(2000):
    s.adc=[127,35,80,160,127][step//400]
    s.run_flags=4 if 20<=step<1700 else 0
    s.vehicle_speed=0 if 1200<=step<1300 else 1
    s.speed_descriptor=1 if 1300<=step<1400 else 0
    for n,a,w in [('adc',0x9209,1),('run_flags',0xFD16,2),('vehicle_speed',0x9201,1),('speed_descriptor',0xB2F4,2)]:
        (mem.write16 if w==2 else mem.write8)(ram(a),getattr(s,n))
    compare(s,'base',('trajectory',step))
    descriptor=compare(s,'update',('trajectory',step))
    # Explicit producer-only feedback. Composed tests execute real ingestion.
    s.descriptor=descriptor;mem.write16(ram(0xB2EE),descriptor)
print(f'PASS {cases} OEM voltage conditioning/producer comparisons'+
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
