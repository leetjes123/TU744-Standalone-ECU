"""Native input, operating-state and freeze-frame conversion contracts.

Each routine executes from the unchanged OEM image. --target adds the actual
Keil-linked C as a third implementation, including its far-pointer ABI.
"""
import ctypes as C
from oem_types import Context,Engine,Iat
import json
import random
import sys
from oem_harness import ROOT,Rom,ram,library

rom=Rom();mem=rom.mem;lib=library();rng=random.Random(0x697CE)

iat_words=[0xB2DA,0xB2E6,0x8B18,0x8B16,0xFD14,0xFD16]
iat_bytes=[0x9208,0x950F,0x9510,0xAA5D,0x8B14,0x8B15,0x950E]

def publish_fields(s,words,byte_addresses):
    for (field,_),address in zip(s._fields_,words):mem.write16(ram(address),getattr(s,field))
    for (field,_),address in zip(s._fields_[len(words):],byte_addresses):mem.write8(ram(address),getattr(s,field))

def compare_fields(s,words,byte_addresses,context):
    for (field,_),address in zip(s._fields_,words):
        assert mem.read16(ram(address))==getattr(s,field),(context,field,mem.read16(ram(address)),getattr(s,field))
    for (field,_),address in zip(s._fields_[len(words):],byte_addresses):
        assert mem.read8(ram(address))==getattr(s,field),(context,field,mem.read8(ram(address)),getattr(s,field))

cases=0
for name,entry in [('init',0x69740),('reset',0x697A4),('capture',0x69970),('update',0x697CE)]:
    function=getattr(lib,'oem_iat_'+name);function.argtypes=[C.POINTER(Iat)]
    function.restype=C.c_uint16 if name=='update' else None
    for case in range(1024):
        s=Iat()
        for field,kind in s._fields_:setattr(s,field,rng.randrange(65536 if kind==C.c_uint16 else 256))
        s.adc=case%256
        s.pass_count=rng.choice([0,1,19,20,21,255]);s.fail_count=rng.choice([0,1,19,20,21,255])
        publish_fields(s,iat_words,iat_bytes)
        descriptor=function(C.byref(s))
        rom.invoke(entry,stop_at=0x6995E if name=='update' else None)
        if name=='update':assert mem.read16(rom.cpu.rw(0))==descriptor,('IAT descriptor',case)
        compare_fields(s,iat_words,iat_bytes,('IAT',name,case));cases+=1

# Keep filter/count/latch state across repeated fault/recovery and cold/hot
# substitution. These are producer trajectories before shared ingestion.
s=Iat();s.adc=90;lib.oem_iat_init(C.byref(s))
for step in range(1800):
    s.adc=[90,0,90,255,40,0][(step//75)%6]
    s.coolant=[0,91,104,105,255][(step//33)%5]
    s.coolant_descriptor=step%2;s.run_flags=4 if step>100 else 2
    publish_fields(s,iat_words,iat_bytes)
    descriptor=lib.oem_iat_update(C.byref(s));rom.invoke(0x697CE,stop_at=0x6995E)
    assert mem.read16(rom.cpu.rw(0))==descriptor,('IAT trajectory descriptor',step)
    compare_fields(s,iat_words,iat_bytes,('IAT trajectory',step));s.descriptor=descriptor;cases+=1

engine_words=[0xFD16,0xFD6A,0x9616,0x9618];engine_bytes=[0x950E,0x9510,0xF8AC]
for name in ['init','update']:
    f=getattr(lib,'oem_engine_'+name);f.argtypes=[C.POINTER(Engine)];f.restype=None
for case in range(256):
    s=Engine(rng.randrange(65536),rng.randrange(65536),rng.randrange(65536),rng.randrange(65536),case,case,case)
    publish_fields(s,engine_words,engine_bytes);lib.oem_engine_init(C.byref(s));rom.invoke(0x329C8)
    compare_fields(s,engine_words,engine_bytes,('engine init',case));cases+=1
for coolant in range(256):
    # The ROM curve is evaluated independently here only to select boundaries;
    # the actual expected output is always the unchanged ROM routine.
    upper=25 if coolant<=24 else (25-(9*(coolant-24)//53) if coolant<77 else
          (16-((coolant-77)//54) if coolant<131 else 15))
    for speed in [0,9,10,11,upper,upper+1,255]:
        for state in [0,2,4,6]:
            for rotation in [0,64]:
                s=Engine(state|0x8000,rotation,4711,rng.choice([0,65534,65535]),coolant,rng.randrange(256),speed)
                publish_fields(s,engine_words,engine_bytes);lib.oem_engine_update(C.byref(s));rom.invoke(0x329D6)
                compare_fields(s,engine_words,engine_bytes,('engine update',coolant,speed,state,rotation));cases+=1

layout=json.loads((ROOT/'docs/oem-input-layout.json').read_text())
lib.oem_context_update.argtypes=[C.POINTER(Context)];lib.oem_context_update.restype=None
for entry in [0x6B760,0x6B8BA]:
    for case in range(800):
        s=Context()
        for i in range(14):s.value[i]=rng.choice([0,1,1023,1024,4095,4096,8191,8192,65535,rng.randrange(65536)])
        s.coolant=case%256;s.vehicle_speed=case%256
        for address,value in zip(layout['context_words'],s.value):mem.write16(ram(address),value)
        for address,value in zip(layout['context_bytes'],[s.coolant,s.vehicle_speed]):mem.write8(ram(address),value)
        for i,address in enumerate(layout['context_output']):s.output[i]=rng.randrange(256);mem.write8(ram(address),s.output[i])
        lib.oem_context_update(C.byref(s));rom.invoke(entry)
        for i,address in enumerate(layout['context_output']):
            assert mem.read8(ram(address))==s.output[i],('context',hex(entry),case,hex(address),mem.read8(ram(address)),s.output[i])
        cases+=1
print(f'PASS {cases} OEM IAT, operating-state and context comparisons'+
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
