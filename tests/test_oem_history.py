"""Portable history codec against native retained RAM spans and Keil ABI.

The EEPROM transport is standalone. The retained field selection is checked
against the native boot clear table and one execution of the actual ROM clear
walker; it is not claimed to be the OEM's persistence implementation.
"""
import ctypes as C
import hashlib
import json
import os
import random
import sys
from oem_harness import ROOT,Rom,ram
from oem_types import Diagnostics

lib=C.CDLL(str(ROOT/'build/oem'/('oem.dll' if os.name=='nt' else 'oem.so')))
Payload=C.c_uint8*768
for n in ['encode','decode']:
    f=getattr(lib,'oem_history_'+n);f.argtypes=[C.POINTER(Diagnostics),C.POINTER(C.c_uint8)];f.restype=C.c_uint8
lib.oem_history_validate.argtypes=[C.POINTER(C.c_uint8)];lib.oem_history_validate.restype=C.c_uint8
lib.oem_diagnostics_bind.argtypes=[C.POINTER(Diagnostics)];lib.oem_diagnostics_bind.restype=None
target=None
if '--target' in sys.argv:import test_target as target
rom=Rom();rng=random.Random(0x148C8)
clt=json.loads((ROOT/'docs/oem-state-layout.json').read_text())['addresses']
spans=[(0xAA5C,4),(0xAA60,8),(0xAA6E,1),(0xABB6,2),(0xB03E,6),(0xB044,480),(0xB224,216),(0xAA68,6)]
addresses=[ram(a+i) for a,size in spans for i in range(size)]
assert len(addresses)==723
# Read the immutable native clear descriptors directly. Every selected byte
# survives the retained branch and is covered by the lost-history branch.
def ranges(cursor):
    out=[]
    while True:
        start=int.from_bytes(rom.data[cursor:cursor+4],'little')
        end=int.from_bytes(rom.data[cursor+4:cursor+8],'little')
        if not start:return out
        out.append((start,end+1));cursor+=8
normal=ranges(0x148C8);lost=ranges(0x148B8)
assert all(not any(a<=x<=b for a,b in normal) and any(a<=x<=b for a,b in lost) for x in addresses)

def seed(s):
    def byte(a,v):rom.mem.write8(ram(a),v)
    def word(a,v):rom.mem.write16(ram(a),v)
    d=s.events
    byte(0xAA5C,s.coolant.value[clt.index(0xAA5C)]);byte(0xAA5D,s.iat.captured)
    for a,v in [(0xAA5E,d.store.phases[0]),(0xAA5F,d.store.phases[6]),
                (0xAA60,d.scan_record),(0xAA61,d.scan_unused),(0xAA62,d.scan_event),
                (0xAA63,d.last_event),(0xAA64,d.store.count),(0xAA65,d.overflow),
                (0xAA66,d.context[1]),(0xAA67,d.warmup_count),(0xAA6E,s.mil.retained),
                (0xB042,d.store.active_demand),(0xB043,d.store.demand)]:byte(a,v)
    word(0xABB6,s.context.value[9]);word(0xB03E,s.coolant.value[clt.index(0xB03E)])
    word(0xB040,d.drive_count);word(0xB2FA,d.timestamp)
    for i,row in enumerate(d.store.records):
        for j,v in enumerate(row):byte(0xB044+24*i+j,v)
    for i,v in enumerate(d.live):word(0xB224+2*i,v)
    for i,v in enumerate(s.readiness.count):byte(0xAA68+i,v)
    byte(0xAA6D,s.readiness.pending)

cases=0
def call(name,s,p):
    global cases
    before_s=bytes(s);before_p=bytes(p)
    result=getattr(lib,'oem_history_'+name)(C.byref(s),p)
    if target:
        for i,b in enumerate(before_s):target.mem.write8(0x382000+i,b)
        for i,b in enumerate(before_p):target.mem.write8(0x382800+i,b)
        actual=target.call('oem_history_'+name,{8:0x2000,9:0xE0,10:0x2800,11:0xE0})&255
        assert actual==result,(name,'return')
        assert bytes(target.mem.read8(0x382000+i) for i in range(C.sizeof(s)))==bytes(s),(name,'state')
        assert bytes(target.mem.read8(0x382800+i) for i in range(768))==bytes(p),(name,'payload')
    cases+=1;return result

for case in range(200):
    s=Diagnostics.from_buffer_copy(bytes(rng.randrange(256) for _ in range(C.sizeof(Diagnostics))))
    s.events.store.count=case%21
    for row in s.events.store.records:row[0]=rng.randrange(107);row[5]=rng.randrange(38)
    s.coolant.value[clt.index(0xAA5C)] &=255
    p=Payload();seed(s)
    expected=hashlib.sha256(rom.data).digest()+bytes(rom.mem.read8(a) for a in addresses)+bytes([2])+bytes(12)
    if case==0:
        rom.invoke(0x325C8,stop_at=0x32608,limit=300000)
        assert expected[32:755]==bytes(rom.mem.read8(a) for a in addresses)
    assert call('encode',s,p)==1
    assert bytes(p)==expected,('native memory payload',case,[(i,a,b) for i,(a,b) in enumerate(zip(bytes(p),expected)) if a!=b][:12])
    restored=Diagnostics()
    # Decode rebinds aliases from their canonical owners. Seed the engine's
    # volatile run flags, not only the event manager's derived copy.
    restored.engine.run_flags=0x1234;restored.events.run_flags=0x1234
    restored.events.clear_request=0x5678
    restored.voltage.filter_high=0x9ABC
    restored.readiness.once=0x35;restored.readiness.supported=0x6D
    assert call('decode',restored,p)==1
    assert restored.events.run_flags==0x1234 and restored.events.clear_request==0x5678
    assert restored.engine.run_flags==0x1234
    assert restored.voltage.filter_high==0x9ABC
    assert restored.readiness.once==0x35 and restored.readiness.supported==0x6D
    out=Payload();assert call('encode',restored,out)==1 and bytes(out)==expected
    # Alias descriptors must refer to the restored live array immediately.
    assert restored.voltage.speed_descriptor==restored.events.live[0x68]
    assert restored.iat.descriptor==restored.events.live[0x5B]
    assert restored.mil.demand==restored.events.store.demand

for at,value in [(0,0),(31,0),(40,21),(53,107),(58,38),(755,1),(756,1),(767,1)]:
    bad=Payload.from_buffer_copy(expected);bad[40]=20;bad[at]=value
    unchanged=bytes(restored)
    assert call('decode',restored,bad)==0 and bytes(restored)==unchanged
# A valid old payload has no extension marker. Do not silently restore its
# absent monitor state as all-complete, or partially restore other retained data.
bad=Payload.from_buffer_copy(expected)
for at in range(749,768):bad[at]=0
unchanged=bytes(restored)
assert call('decode',restored,bad)==0 and bytes(restored)==unchanged
for invalid in ['count','event','config']:
    s=Diagnostics();s.events.store.count=21 if invalid=='count' else 1
    if invalid=='event':s.events.store.records[0][0]=107
    if invalid=='config':s.events.store.records[0][5]=38
    p=Payload(*([0x5A]*768));before=bytes(p)
    assert call('encode',s,p)==0 and bytes(p)==before
print(f'PASS {cases} history codec/retained-RAM checks'+
      ('; identical Keil-linked calls also passed' if target else ''))
