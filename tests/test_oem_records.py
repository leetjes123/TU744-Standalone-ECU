"""Compiled record-manager C versus unchanged TU5JP ROM instruction execution.

Native records and phase identities are compared, not invented engineering-unit
fault thresholds. Physical monitor producers and retention IO remain separate.
"""
from pathlib import Path
import ctypes as C
from oem_types import Records,State
import hashlib
import os
import random
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from oem_repo import oem_repo
REPO = oem_repo()
sys.path.insert(0,str(REPO/'src'))
from c167re.emu.cpu.core import Cpu
from c167re.emu.mem.space import Device,Memory

data=(REPO/'bins/M744_C167_FULL.bin').read_bytes()
assert hashlib.sha256(data).hexdigest()=='5710015f7c5c066c860a1757fd893f305701608c25af8ff23bfcb4fd1e4837b3'
mem=Memory(fault_on_unmapped=True)
mem.attach(Device('flash',0x800000,len(data),bytearray(data),writable=False,mirrors=(0,)))
mem.attach(Device('ram',0x380000,0x8000,writable=True))
cpu=Cpu(mem)
lib=C.CDLL(str(ROOT/'build/oem'/('oem.dll' if os.name=='nt' else 'oem.so')))
# Optional third implementation: execute the actual Keil-linked instructions
# on the identical pre-call bytes, including LARGE far-pointer parameters.
# The native DLL result is still compared to the unchanged OEM image below.
if '--target' in sys.argv:
    import test_target as target
    native_lib=lib
    class TargetFunction:
        def __init__(self,name):
            self.name=name;self.native=getattr(native_lib,name)
        def __call__(self,*args):
            if self.name=='dtc_ingest_action':
                expected=self.native(*args)
                actual=target.call(self.name,{8:args[0],9:args[1]})&255
            else:
                state=args[0]._obj
                for i,b in enumerate(bytes(state)):target.mem.write8(0x382800+i,b)
                regs={8:0x2800,9:0xE0}
                if len(args)>1:regs[10]=args[1]
                if self.name=='oem_dtc_ingest':
                    target.mem.write16(0x383000,args[2]._obj.value)
                    regs.update({11:0x3000,12:0xE0})
                elif len(args)>2:regs[11]=args[2]
                expected=self.native(*args)
                actual=target.call(self.name,regs)&255
                observed=bytes(target.mem.read8(0x382800+i) for i in range(C.sizeof(state)))
                assert observed==bytes(state),(self.name,'Keil state',[(i,a,b) for i,(a,b) in enumerate(zip(observed,bytes(state))) if a!=b][:12])
                if self.name=='oem_dtc_ingest':
                    assert target.mem.read16(0x383000)==args[2]._obj.value
            if expected is not None:assert actual==expected,(self.name,'Keil return',actual,expected)
            return expected
        def __setattr__(self,name,value):
            if name in ('argtypes','restype'):setattr(self.native,name,value)
            else:object.__setattr__(self,name,value)
    class TargetLibrary:
        def __init__(self):self.functions={}
        def __getattr__(self,name):
            if name not in self.functions:self.functions[name]=TargetFunction(name)
            return self.functions[name]
    lib=TargetLibrary()
phase_addresses=[0xAA5E,0x951A,0x9519,0x952B,0x9593,0x9594,0xAA5F]
for name,args in [('remove',[C.c_uint8]),('phase',[C.c_uint16]),('aggregate',[])]:
    f=getattr(lib,'oem_dtc_'+name)
    f.argtypes=[C.POINTER(Records),*args];f.restype=C.c_uint8
lib.dtc_ingest_action.argtypes=[C.c_uint16,C.c_uint16]
lib.dtc_ingest_action.restype=C.c_uint8

def ram(a):return 0x380000+(a&0x3FFF)

def invoke(entry,registers=None,limit=20000):
    cpu.reset(ip=entry&65535,csp=0x80+(entry>>16));cpu.dpp=[0x204,0x205,0xE0,3]
    cpu.set_rw(0,0xE700)
    for r,v in (registers or {}).items():cpu.set_rw(r,v)
    stack=cpu.sp;callbacks=[]
    for _ in range(limit):
        if cpu.pc() in (0x86A1E6,0x86A636,0x86A7D2,0x86A92A):callbacks.append(cpu.pc())
        if mem.read16(cpu.pc())==0xDB and cpu.sp==stack:
            assert not cpu.traps and cpu.rw(0)==0xE700
            return callbacks
        cpu.step()
    raise AssertionError((hex(entry),hex(cpu.pc()),cpu.traps))

def publish(s):
    for i,b in enumerate(bytes(s.records)):mem.write8(ram(0xB044)+i,b)
    for a,v in [(0xAA64,s.count),(0xB043,s.demand),(0xB042,s.active_demand),
                *zip(phase_addresses,s.phases)]:mem.write8(ram(a),v)

def compare(s,context):
    actual=bytes(mem.read8(ram(0xB044)+i) for i in range(480))
    assert actual==bytes(s.records),(context,'records',[(i,a,b) for i,(a,b) in enumerate(zip(actual,bytes(s.records))) if a!=b][:12])
    for a,v in [(0xAA64,s.count),(0xB043,s.demand),(0xB042,s.active_demand),
                *zip(phase_addresses,s.phases)]:assert mem.read8(ram(a))==v,(context,hex(a),mem.read8(ram(a)),v)

rng=random.Random(0x6AA60)
def state(count):
    s=Records();s.count=count;s.demand=rng.randrange(256);s.active_demand=rng.randrange(256)
    for i in range(7):s.phases[i]=rng.choice([0,1,2,255])
    for row in s.records:
        for i in range(24):row[i]=rng.randrange(256)
        row[0]=rng.randrange(107);row[5]=rng.randrange(38)
        for i in [1,6,7,8]:row[i]=rng.choice([0,1,2,3,254,255])
    return s

cases=0
for count in range(1,21):
    for slot in range(count):
        s=state(count);publish(s)
        assert lib.oem_dtc_remove(C.byref(s),slot)==1
        invoke(0x6A13C,{12:slot});compare(s,('remove',count,slot));cases+=1
for case in range(2400):
    s=state(case%21);publish(s);phase=rng.choice([*phase_addresses,0xBEEF])
    assert lib.oem_dtc_phase(C.byref(s),phase)==1
    invoke(0x6AB72,{12:phase});compare(s,('phase',case,hex(phase)));cases+=1
for case in range(2400):
    s=state(case%21);publish(s)
    assert lib.oem_dtc_aggregate(C.byref(s))==1
    invoke(0x6AA60);compare(s,('aggregate',case));cases+=1

# Trace which REAL native callbacks are reached. Keep the pool disabled/empty,
# so this probe tests the dispatcher's branching, not its callbacks' contracts.
mem.write8(ram(0xAA64),0);mem.write16(0xFD6C,0)
for old_bits in range(4):
    for incoming in range(8):
        for old_subtype in range(16):
            for subtype in range(16):
                old=old_bits|(old_subtype<<8)
                descriptor=(incoming&3)|((incoming&4)<<10)|(subtype<<8)
                mem.write16(ram(0xB2E6),old);mem.write16(0xE700,descriptor)
                callbacks=invoke(0x6A996,{12:0x61})
                mask=0
                for address in callbacks:mask|={0x86A1E6:1,0x86A636:2,0x86A7D2:4,0x86A92A:8}[address]
                assert lib.dtc_ingest_action(old,descriptor)==mask,(hex(old),hex(descriptor),callbacks)
                cases+=1
assert lib.dtc_ingest_action(0x0101,0x1203)==12
# Corrupt standalone storage cannot index arbitrary flash or overrun RAM.
s=state(20);s.count=21;before=bytes(s)
assert lib.oem_dtc_aggregate(C.byref(s))==0 and bytes(s)==before
s=state(1);s.records[0][5]=255;before=bytes(s)
assert lib.oem_dtc_phase(C.byref(s),0x951A)==0 and bytes(s)==before
context_addresses=[0x9305,0xAA66,0x92C2,0x9521,0x951C,0x951B,0x951D,0xF8AC,0x9528]
phase_word_fields=[(0xFD16,'run_flags'),(0x8B3C,'clock_divider'),(0x8B30,'drive_timer'),(0xB040,'drive_count')]
phase_byte_fields=[(0x950E,'coolant'),(0x952A,'warmup_start'),(0xAA67,'warmup_count')]
for name,args in [('assert',[C.c_uint16]),('recover',[C.c_uint16]),('complete',[]),
                  ('subtype',[C.c_uint16]),('ingest',[C.POINTER(C.c_uint16)])]:
    f=getattr(lib,'oem_dtc_'+name);f.argtypes=[C.POINTER(State),C.c_uint8,*args];f.restype=C.c_uint8

def publish_state(s):
    publish(s.store)
    for i,v in enumerate(s.live):mem.write16(ram(0xB224+2*i),v)
    mem.write16(0xFD6C,s.gate);mem.write16(ram(0xB2FA),s.timestamp)
    mem.write8(ram(0xAA63),s.last_event);mem.write8(ram(0xAA65),s.overflow)
    for a,v in zip(context_addresses,s.context):mem.write8(a if a>=0xF000 else ram(a),v)
    for a,v in [(0x8B36,s.clear_request),(0x8B38,s.clear_inverse),(0x8B3A,s.clear_mode)]:mem.write16(ram(a),v)
    mem.write16(0xFD14,s.startup_flags)
    mem.write8(ram(0xAA62),s.scan_event);mem.write8(ram(0x8B32),s.clear_previous)
    for a,v in [(0xAA60,s.scan_record),(0xAA61,s.scan_unused),(0x8B33,s.clear_wait),(0x8B34,s.lock_wait)]:mem.write8(ram(a),v)
    for a,field in phase_word_fields:mem.write16(a if a>=0xF000 else ram(a),getattr(s,field))
    for a,field in phase_byte_fields:mem.write8(ram(a),getattr(s,field))

def compare_state(s,context):
    compare(s.store,context)
    for i,v in enumerate(s.live):assert mem.read16(ram(0xB224+2*i))==v,(context,'live',i,hex(mem.read16(ram(0xB224+2*i))),hex(v))
    assert mem.read16(0xFD6C)==s.gate,(context,'gate')
    assert mem.read16(ram(0xB2FA))==s.timestamp
    assert mem.read8(ram(0xAA63))==s.last_event,(context,'last-event')
    assert mem.read8(ram(0xAA65))==s.overflow,(context,'overflow')
    for a,v in [(0x8B36,s.clear_request),(0x8B38,s.clear_inverse),(0x8B3A,s.clear_mode)]:assert mem.read16(ram(a))==v,(context,hex(a))
    assert mem.read16(0xFD14)==s.startup_flags,(context,'startup flags')
    assert mem.read8(ram(0xAA62))==s.scan_event,(context,'scan event')
    assert mem.read8(ram(0x8B32))==s.clear_previous,(context,'clear previous')
    for a,v in [(0xAA60,s.scan_record),(0xAA61,s.scan_unused),(0x8B33,s.clear_wait),(0x8B34,s.lock_wait)]:assert mem.read8(ram(a))==v,(context,hex(a))
    for a,v in zip(context_addresses,s.context):assert mem.read8(a if a>=0xF000 else ram(a))==v,(context,'context',hex(a))
    for a,field in phase_word_fields:assert mem.read16(a if a>=0xF000 else ram(a))==getattr(s,field),(context,field)
    for a,field in phase_byte_fields:assert mem.read8(ram(a))==getattr(s,field),(context,field)

for name,entry in [('assert',0x6A1E6),('recover',0x6A636),('complete',0x6A7D2),
                   ('subtype',0x6A92A),('ingest',0x6A996)]:
    for case in range(1200):
        s=State();s.store=state(case%21)
        for i in range(107):s.live[i]=rng.randrange(65536)
        s.gate=rng.randrange(65536)|32
        if case%7==0:s.gate&=~32
        s.timestamp=rng.randrange(65536);s.last_event=rng.randrange(107);s.overflow=rng.choice([0,0x55,255])
        for i in range(9):s.context[i]=rng.randrange(256)
        event=rng.randrange(107)
        if s.store.count and case%2:event=s.store.records[rng.randrange(s.store.count)][0]
        descriptor=rng.randrange(65536)
        publish_state(s);mem.write16(0xE700,descriptor)
        if name=='ingest':
            d=C.c_uint16(descriptor);assert lib.oem_dtc_ingest(C.byref(s),event,C.byref(d))==1
        elif name=='complete':expected=lib.oem_dtc_complete(C.byref(s),event)
        else:expected=getattr(lib,'oem_dtc_'+name)(C.byref(s),event,descriptor)
        invoke(entry,{12:event});compare_state(s,(name,case,event))
        if name=='ingest':assert mem.read16(0xE700)==d.value,(name,case,'caller descriptor')
        else:assert cpu.rw(4)==expected,(name,case,'return',cpu.rw(4),expected)
        cases+=1

# Retained mixed-event history reaches full/class-limited pools naturally.
s=State();s.gate=32
for i in range(9):s.context[i]=rng.randrange(256)
publish_state(s)
for step in range(2400):
    event=rng.choice([0x61,0x1B,0x20,0x25,0x2F,0x2B,0x4C,0x43,0x62,0x63,0x64,0x65,*range(3,13)])
    s.timestamp=step;mem.write16(ram(0xB2FA),step)
    for i,a in enumerate(phase_addresses):
        s.store.phases[i]=int(step%(i+2)==0);mem.write8(ram(a),s.store.phases[i])
    value=int(s.live[event]);value=(value&~0xF03)|rng.choice([0,2,3])|(rng.choice([1,2,4,8])<<8)
    d=C.c_uint16(value);mem.write16(0xE700,value)
    lib.oem_dtc_ingest(C.byref(s),event,C.byref(d));invoke(0x6A996,{12:event})
    compare_state(s,('trajectory',step))
    assert mem.read16(0xE700)==d.value
    if step%3==0:
        lib.oem_dtc_aggregate(C.byref(s.store));invoke(0x6AA60);compare_state(s,('aggregate trajectory',step))
        cases+=1
    cases+=1
lib.oem_dtc_age.argtypes=[C.POINTER(State)];lib.oem_dtc_age.restype=C.c_uint8
for case in range(1500):
    s=State();s.store=state(case%21)
    for i in range(107):s.live[i]=rng.randrange(65536)
    s.gate=rng.randrange(65536)&~64
    if case%4==0:s.gate|=64
    publish_state(s)
    assert lib.oem_dtc_age(C.byref(s))==1
    invoke(0x6B252);compare_state(s,('age',case));cases+=1
    before=bytes(s)
    assert lib.oem_dtc_age(C.byref(s))==1 and bytes(s)==before
    invoke(0x6B252);compare_state(s,('age repeated',case));cases+=1
lib.oem_dtc_clear_worker.argtypes=[C.POINTER(State)];lib.oem_dtc_clear_worker.restype=C.c_uint8
for case in range(1200):
    s=State();s.store=state(case%21)
    for i in range(107):s.live[i]=rng.randrange(65536)
    s.gate=rng.randrange(65536)|32
    if case%7==0:s.gate&=~32
    s.overflow=rng.randrange(256);s.last_event=rng.randrange(107)
    s.clear_request=rng.choice([0,1,105,106,107,65535,rng.randrange(107)])
    if case%4==0:s.clear_request=106
    s.clear_inverse=rng.randrange(65536)
    s.clear_mode=rng.choice([0,1,2,65535])
    publish_state(s)
    assert lib.oem_dtc_clear_worker(C.byref(s))==int(bool(s.gate&32))
    invoke(0x6B4D8);compare_state(s,('clear worker',case));cases+=1
for name in ['cycle_begin','clear_all','clear_emissions','clear_event']:
    f=getattr(lib,'oem_dtc_'+name)
    f.argtypes=[C.POINTER(State)]+([C.c_uint8] if name=='clear_event' else [])
    f.restype=C.c_uint8
for name,entry in [('cycle_begin',0x6B1AA),('clear_all',0x6BA14),
                   ('clear_emissions',0x6BA7C),('clear_event',0x6BB22)]:
    for case in range(800):
        s=State();s.store=state(case%21)
        for i in range(107):s.live[i]=rng.randrange(65536)
        s.gate=rng.randrange(65536);s.startup_flags=rng.randrange(65536)
        s.scan_event=rng.randrange(256);s.clear_previous=rng.randrange(256)
        s.clear_request=rng.choice([0,0,106,rng.randrange(107)])
        s.clear_inverse=(s.clear_request^65535) if case%3 else rng.randrange(65536)
        s.clear_mode=rng.randrange(65536)
        publish_state(s)
        # Empty, runnable OEM RTOS event mailbox. The unmodified 0x0F08 send
        # executes fully; no scheduler/task switch is needed at this priority.
        mem.write16(0xE008,65535);mem.write16(0xE02A,0xE100)
        mem.write16(0xE108,0);mem.write16(0xE000,0);mem.write16(0xE05A,65535)
        mem.write8(ram(0x813E),0)
        event=rng.randrange(107)
        args=[C.byref(s)]+([event] if name=='clear_event' else [])
        expected=getattr(lib,'oem_dtc_'+name)(*args)
        invoke(entry,{12:event});compare_state(s,(name,case))
        if name!='cycle_begin':
            assert cpu.rw(4)==expected,(name,case,'admission result')
            assert mem.read8(ram(0x813E))==expected,(name,case,'RTOS send count')
            assert mem.read16(0xE108)==(0x4702 if expected else 0)
        cases+=1
lib.oem_dtc_maintain.argtypes=[C.POINTER(State)];lib.oem_dtc_maintain.restype=C.c_uint8
for case in range(1800):
    s=State();s.store=state(case%21)
    for i in range(107):s.live[i]=rng.randrange(65536)
    s.gate=rng.randrange(65536);s.startup_flags=rng.randrange(65536)
    for field in ['scan_event','scan_record','scan_unused','clear_previous','clear_wait','lock_wait']:
        setattr(s,field,rng.randrange(256))
    s.overflow=rng.choice([0,0x55,255]);s.last_event=rng.randrange(107)
    s.clear_request=rng.choice([0,0,106,rng.randrange(65536)])
    s.clear_inverse=(s.clear_request^65535) if case%3 else rng.randrange(65536)
    s.clear_mode=rng.randrange(65536)
    for i in range(9):s.context[i]=rng.randrange(256)
    # Exercise the counted-tail alias into live[0]/live[1] at a full pool.
    if s.store.count==20 and case%2:
        s.scan_event=96;s.live[0]=97;s.live[1]=0;s.live[97]=0x1003
    publish_state(s)
    mem.write16(0xE008,65535);mem.write16(0xE02A,0xE100)
    mem.write16(0xE108,0);mem.write16(0xE000,0);mem.write16(0xE05A,65535)
    mem.write8(ram(0x813E),0)
    assert lib.oem_dtc_maintain(C.byref(s))==1
    invoke(0x6AD34);compare_state(s,('maintain',case));cases+=1
for name,entry in [('drive_init',0x69F8C),('drive_update',0x69F9A),('drive_clear',0x69FDE),
                   ('warmup_init',0x6BE44),('warmup_update',0x6BE52),('warmup_clear',0x6BEB2),('clock',0x6BE28)]:
    f=getattr(lib,'oem_dtc_'+name);f.argtypes=[C.POINTER(State)];f.restype=None if name=='clock' else C.c_uint8
    for case in range(320):
        s=State();s.store=state(case%21)
        s.gate=rng.randrange(65536);s.startup_flags=rng.randrange(65536)
        s.clear_request=rng.choice([0,106,107]);s.run_flags=rng.randrange(65536)
        s.timestamp=rng.choice([0,1,65534,65535])
        s.clock_divider=rng.choice([0,1,358,359,360,361,65535])
        s.drive_count=rng.choice([0,1,65534,65535]);s.drive_timer=rng.randrange(65536)
        if case%2:
            delay=int.from_bytes(data[0x1D894:0x1D896],'little')
            s.drive_timer=rng.choice([0,max(0,delay-1),delay,min(65535,delay+1)])
        s.coolant=rng.choice([0,128,130,131,132,155,156,157,158,159,160,161,255,rng.randrange(256)])
        s.warmup_start=rng.choice([0,128,130,131,132,155,156,157,158,159,160,161,255,rng.randrange(256)])
        s.warmup_count=rng.choice([0,1,254,255])
        s.store.phases[2]=rng.choice([0,0,1,255]);s.store.phases[3]=rng.choice([0,0,1,255])
        publish_state(s);f(C.byref(s));invoke(entry);compare_state(s,(name,case));cases+=1
print(f'PASS {cases} OEM record/dispatch comparisons including retained mixed-event history'+
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
