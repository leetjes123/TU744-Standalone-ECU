"""Event68 producer/reset versus unchanged ROM, including retained qualifiers."""
import ctypes as C
import itertools
import random
import sys
from oem_harness import Rom, ram, library
from oem_types import Vss

rom=Rom();mem=rom.mem;lib=library();rng=random.Random(0x2A01E)
words=[0xB2F4,0xB254,0xB256,0x8178,0xFD06,0xFD08,0xFD18,0xFD52,0xFD5E,0x95AA,0x95A4]
octets=[0x94A3,0x9477,0x947D,0x8174,0x8175,0x8176,0x950E,0xF8AC,0xF86E]
for name in ['reset','update']:
    f=getattr(lib,'oem_vss_'+name);f.argtypes=[C.POINTER(Vss)]
    f.restype=C.c_uint16 if name=='update' else None

def publish(s):
    for fields,addresses,write in [(s._fields_,words,mem.write16),(s._fields_[11:],octets,mem.write8)]:
        for (n,_),a in zip(fields,addresses):write(ram(a),getattr(s,n))

cases=0
def compare(s,name,context):
    global cases
    descriptor=getattr(lib,'oem_vss_'+name)(C.byref(s))
    rom.invoke(0x2A01E if name=='update' else 0x2A298,stop_at=0x2A282 if name=='update' else None)
    if name=='update':assert descriptor==mem.read16(rom.cpu.rw(0)),('descriptor',context,hex(descriptor),hex(mem.read16(rom.cpu.rw(0))))
    for fields,addresses,read in [(s._fields_,words,mem.read16),(s._fields_[11:],octets,mem.read8)]:
        for (n,_),a in zip(fields,addresses):assert getattr(s,n)==read(ram(a)),(name,context,n,getattr(s,n),read(ram(a)))
    cases+=1
    return descriptor

for name in ['reset','update']:
    for case in range(2048):
        s=Vss()
        for n,kind in s._fields_:setattr(s,n,rng.randrange(65536 if kind==C.c_uint16 else 256))
        s.source=case%5
        for n in ['fail_count','pass_count','source_count']:setattr(s,n,rng.choice([0,1,49,50,51,254,255]))
        publish(s);compare(s,name,('random',case))

# Strict boundary comparisons and both independent enable branches. Constants
# are taken directly from the ROM, not from replacement-generated definitions.
cal=rom.data;count=cal[0x10023];limit=cal[0x10025]*160
for coolant,rpm,speed,branch,counter in itertools.product(
        [cal[0x10024]-1,cal[0x10024],cal[0x10024]+1],
        [cal[0x1001F]-1,cal[0x1001F],cal[0x1001F]+1,cal[0x10021]-1,cal[0x10021],cal[0x10021]+1],
        [limit-1,limit,limit+1],[0,1,2,3],[0,count-1,count,255]):
    s=Vss();s.coolant=coolant;s.engine_speed=rpm;s.condition_speed=speed
    s.fd06=256;s.fd18=32 if branch&1 else 0;s.fd52=8 if branch&2 else 0
    s.load=cal[0x10022]+1;s.fail_count=s.pass_count=counter
    publish(s);compare(s,'update',('boundary',coolant,rpm,speed,branch,counter))

for source,a_status,b_status,desc_a,desc_b,counter in itertools.product(
        [0,1,2,3,255],[0,1,2],[0,1,2],[0,0x800,0x900],[0,0x800,0x900],[0,1,count]):
    s=Vss();s.source=source;s.source_a_status=a_status;s.source_b_status=b_status
    s.source_a_descriptor=desc_a;s.source_b_descriptor=desc_b;s.source_count=counter
    publish(s);compare(s,'update',('source',source,a_status,b_status,desc_a,desc_b,counter))

# Qualification, latched fault, recovery, source-policy change and reset keep
# independent states on both sides. Ingestion is covered by the composed suite.
s=Vss();s.fd06=256;s.coolant=200;s.engine_speed=90;s.fd18=32
publish(s);seen=set()
for step in range(800):
    phase=step//100
    values={'condition_speed':0 if phase in (0,1,2,6) else 2000,
            'fd06':(s.fd06&0xFEFF)|(256 if phase<4 or phase>=6 else 0),
            'fd18':0 if phase==1 else 32,'source':3 if phase==4 else 0}
    for n,value in values.items():
        setattr(s,n,value)
        index=[n for n,_ in s._fields_].index(n)
        (mem.write16 if index<11 else mem.write8)(ram((words+octets)[index]),value)
    descriptor=compare(s,'update',('retained',step))
    s.descriptor=descriptor;mem.write16(ram(0xB2F4),descriptor)
    if descriptor&1:seen.add((descriptor>>8)&15)
    if step==620:
        s.descriptor|=128;mem.write16(ram(0xB2F4),s.descriptor)
        compare(s,'reset',('retained',step))
assert seen=={4,8},seen
print(f'PASS {cases} OEM vehicle-speed producer/reset comparisons'+
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
