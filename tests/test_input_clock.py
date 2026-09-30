"""Interrupt-boundary clock publication using the actual Keil-linked ISRs.

A higher-priority health tick (level 10) is modeled as one atomic clock
update when IEN is set and PSW.ILVL is below 10, as C167 arbitration permits. This checks the emitted exclusion around the two-word timestamp;
it does not model interrupt latency, ADC conversion timing or VSS electronics.
The C166 compiler supplies struct offsets; host ABI offsets are never assumed.
"""
from pathlib import Path
import os
import re
import subprocess
import test_target as target

root=target.ROOT;out=root/'build/input-clock';out.mkdir(parents=True,exist_ok=True)
keil=Path(os.environ.get('KEIL_C166',str(Path.home()/'AppData/Local/Keil_v5/C166')))
probe=out/'layout.c'
probe.write_text('#include "ecu.h"\n#include <stddef.h>\nconst u16 input_layout[]={'+
    ','.join(['offsetof(Ecu,milliseconds)','offsetof(Ecu,adc)','sizeof(AdcSample)',
              'offsetof(AdcSample,stamp)','offsetof(AdcSample,result)',
              'offsetof(Ecu,vss_count)','offsetof(Ecu,vss_edge_stamp)','sizeof(Ecu)'])+'};\n')
result=subprocess.run([str(keil/'BIN/C166.EXE'),str(probe),'LARGE','MOD167','SRC',
                       f'INCDIR({root/"include"})'],cwd=out,capture_output=True,text=True)
(out/'build.log').write_text(result.stdout+result.stderr)
assert result.returncode==0 and '0 WARNING(S),  0 ERROR(S)' in result.stdout,result.stdout
layout=[int(v,16) for v in re.findall(r'\bDW\s+([0-9A-F]+)H',probe.with_suffix('.SRC').read_text())]
assert len(layout)==8,layout
clock,adc,stride,stamp,word,vss_count,vss_stamp,size=layout
listing=(target.BUILD/'TU5JP.m66').read_text()
base=int(re.search(r'^\s+([0-9A-F]{6})H\s+ecu\s+VAR',listing,re.M)[1],16)
cpu=target.cpu;mem=target.mem

class TornClock(AssertionError):pass

def write_clock(value):
    mem.write16(base+clock,value&65535);mem.write16(base+clock+2,value>>16)

def read32(address):return mem.read16(address)|(mem.read16(address+2)<<16)

def invoke(name,old,inject_at=None):
    for i in range(size):mem.write8(base+i,0)
    entry=target.symbols[name]
    cpu.reset(ip=entry&65535,csp=entry>>16)
    cpu.dpp=[0,1,0xE0,3];cpu.set_rw(0,0xBE00)
    cpu.psw=(8 if name=='adc_isr' else 5)<<12|0x0800
    write_clock(old);mem.write16(0xFEA0,0xB2A5)
    stack=cpu.sp;pending=False;delivered=False;deferred=False
    for step in range(1000):
        if step==inject_at:pending=True
        if pending:
            if cpu.psw&0x0800 and (cpu.psw>>12)<10:
                write_clock((old+1)&0xFFFFFFFF);pending=False;delivered=True
            else:deferred=True
        if mem.read16(cpu.pc())==0x88FB and cpu.sp==stack:
            assert not cpu.traps and cpu.rw(0)==0xBE00,(name,cpu.traps)
            at=base+adc+11*stride if name=='adc_isr' else base
            captured=read32(at+(stamp if name=='adc_isr' else vss_stamp))
            if captured not in (old,(old+1)&0xFFFFFFFF):
                raise TornClock((name,inject_at,hex(captured)))
            if name=='adc_isr':assert mem.read16(at+word)==0xB2A5
            else:assert mem.read16(base+vss_count)==1
            assert inject_at is None or delivered,(name,inject_at,'tick never delivered')
            return step,captured,deferred
        cpu.step()
    raise AssertionError((name,hex(cpu.pc()),cpu.traps))

count=0;deferred=0
for name in ['adc_isr','vss_isr']:
    for old in [0x0000FFFF,0xFFFFFFFF]:
        length,_,_=invoke(name,old);seen=set()
        for point in range(length):
            _,captured,waited=invoke(name,old,point)
            seen.add(captured);deferred+=waited;count+=1
        assert seen=={old,(old+1)&0xFFFFFFFF},(name,old,seen)
assert deferred>0,'no masked clock update was exercised'
# Negative control: suppress only hal_lock's MOV PSW,R4 (the ILVL raise) in a
# private in-memory copy of the linked image. Both readers must then expose a
# torn value at some boundary. The development HEX and the OEM reference image
# are never modified.
start=target.symbols['hal_lock']
sites=[p for p in range(start,start+0x20,2) if bytes(target.image[p:p+4])==b'\xf6\xf4\x10\xff']
assert len(sites)==1,sites
at=sites[0];saved=bytes(target.image[at:at+4]);failed=set()
try:
    target.image[at:at+4]=b'\xcc\x00\xcc\x00'  # NOP NOP
    for name in ['adc_isr','vss_isr']:
        length,_,_=invoke(name,0x0000FFFF)
        for point in range(length):
            try:invoke(name,0x0000FFFF,point)
            except TornClock:
                failed.add(name);break
finally:target.image[at:at+4]=saved
assert failed=={'adc_isr','vss_isr'},('negative control',failed)
print(f'PASS {count} Keil ISR clock-publication interleavings ({deferred} deferred ticks); both negative controls detected')
