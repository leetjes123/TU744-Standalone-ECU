"""Complete digital filter/publication contracts versus unchanged TU5JP ROM."""
import ctypes as C
import itertools
import random
import sys
from oem_harness import Rom,library
from oem_types import Digital
from digital_layout import words,octets,address

rom=Rom();mem=rom.mem;lib=library();rng=random.Random(0x726CE)
entries=dict(init=0x72696,filter=0x726CE,publish=0x2B8E8,aux=0x2BA18)
for name in entries:
    f=getattr(lib,'oem_digital_'+name);f.argtypes=[C.POINTER(Digital)];f.restype=None

def publish(s):
    for mapping,write in [(words,mem.write16),(octets,mem.write8)]:
        for n,a in mapping.items():write(address(a),getattr(s,n))

cases=0
def compare(s,name,label):
    global cases
    getattr(lib,'oem_digital_'+name)(C.byref(s));rom.invoke(entries[name])
    for mapping,read in [(words,mem.read16),(octets,mem.read8)]:
        for n,a in mapping.items():assert getattr(s,n)==read(address(a)),(name,label,n,getattr(s,n),read(address(a)))
    cases+=1

for name in entries:
    for case in range(1024):
        s=Digital()
        for n,kind in s._fields_:setattr(s,n,rng.randrange(65536 if kind==C.c_uint16 else 256))
        s.pulse_count=rng.choice([0,1,2,3,254,255]);s.pulse_state=case%6
        s.clock_high=rng.choice([0,1,65534,65535]);s.stamp_flags=rng.choice([0,8,65535])
        publish(s);compare(s,name,case)

# All old-output/three-input histories for each physical pin. Bits outside the
# seven-pin mask are intentionally varied in the random cases above.
pins=[('p4',4),('p6',3),('p5',2),('p5',3),('p8',4),('p5',4),('p8',6)]
for bit,(port,pin) in enumerate(pins):
    for output,old,new,current in itertools.product([0,1],repeat=4):
        s=Digital();s.published=output<<bit;s.older=old<<bit;s.newer=new<<bit
        setattr(s,port,current<<pin)
        publish(s);compare(s,'filter',('pin',bit,output,old,new,current))
        assert ((s.published>>bit)&1)==(current if old==new==current else output)

# Exhaust all sampled bit combinations against the pulse qualifier states and
# pending-overflow clock capture; retained timestamp must not be recaptured.
for value,count,state,previous,stamped in itertools.product(
        range(128),[0,1,3],[0,1,2,3,4],[0,0x4000],[0,8]):
    s=Digital();s.published=value;s.pulse_count=count;s.pulse_state=state
    s.fd08=previous;s.stamp_flags=stamped;s.clock_high=65535;s.clock_low=31;s.clock_irq=128
    s.stamp_high=123;s.stamp_low=456
    publish(s);compare(s,'publish',('qualification',value,count,state,previous,stamped))

s=Digital();publish(s);compare(s,'init','retained')
for step in range(800):
    for port in ['p4','p5','p6','p8']:
        value=rng.randrange(65536) if step%40<10 else (65535 if step%80<40 else 0)
        setattr(s,port,value);mem.write16(address(words[port]),value)
    s.clock_low=(step*781)&65535;mem.write16(address(words['clock_low']),s.clock_low)
    compare(s,'filter',('retained',step));compare(s,'publish',('retained',step))
    if step%10==0:compare(s,'aux',('retained',step))
print(f'PASS {cases} OEM digital initialization/filter/publication comparisons'+
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
