"""Execute the actual Keil-linked functions with the repository's C167 CPU.

No peripheral accuracy is inferred from these isolated arithmetic/calibration
calls. C166 integer promotions and far-pointer behavior are exercised here.
"""
from pathlib import Path
import random
import re
import sys
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build'/('stock-95080' if '--stock-95080' in sys.argv else 'engine-experimental' if '--engine-experimental' in sys.argv else
                   ('uvision' if '--uvision' in sys.argv else 'c166'))
sys.path.insert(0,str(ROOT.parents[1]/'src'))
from c167re.emu.cpu.core import Cpu
from c167re.emu.mem.space import Device,Memory

image=bytearray(b'\xff'*0x80000);base=0
for line in (BUILD/'TU5JP.H86').read_text().splitlines():
    record=bytes.fromhex(line[1:]);assert sum(record)&255==0
    length,address,kind=record[0],int.from_bytes(record[1:3],'big'),record[3]
    if kind==0:image[base+address:base+address+length]=record[4:4+length]
    elif kind==2:base=int.from_bytes(record[4:6],'big')<<4
    elif kind==4:base=int.from_bytes(record[4:6],'big')<<16
symbols={name:int(address,16) for address,name in re.findall(r'^\s+([0-9A-F]{6})H\s+(\w+)\s+LABEL', (BUILD/'TU5JP.m66').read_text(),re.M)}
mem=Memory(fault_on_unmapped=True);mem.attach(Device('flash',0,len(image),image,writable=False));mem.attach(Device('ram',0x380000,0x8000,writable=True));cpu=Cpu(mem)
def call(name,registers,limit=200000):
    entry=symbols[name];cpu.reset(ip=entry&65535,csp=entry>>16);cpu.dpp=[0,1,0xE0,3];cpu.set_rw(0,0xBE00)
    for r,v in registers.items():cpu.set_rw(r,v&65535)
    start=cpu.sp
    for step in range(limit):
        if mem.read16(cpu.pc())==0xDB and cpu.sp==start:
            assert not cpu.traps and cpu.rw(0)==0xBE00,(name,cpu.traps)
            return cpu.rw(4)
        cpu.step()
    raise AssertionError((name,hex(cpu.pc()),cpu.traps))

count=0
for us in [0,1,20,100,5000,25000,*range(0,25001,37)]:
    assert call('us_ticks',{8:us})==(us*5+3)//4
    count+=1
call('cal_example',{8:0x2000,9:0xE0})
assert call('cal_validate',{8:0x2000,9:0xE0,10:0x2F00,11:0xE0})&255==1,hex(mem.read16(0x382F00))
for i in range(16):mem.write8(0x382480+i,10+i*10)
for temperature in range(-40,151):
    bounded=max(-30,min(120,temperature));index=min(14,(bounded+30)//10)
    fraction=(bounded+30-index*10)*256//10
    expected=10+10*index+10*fraction//256
    actual=call('table1',{8:0x2000,9:0xE0,10:0x480,11:temperature,12:0})
    assert actual==expected,(temperature,actual,expected)
    count+=1
for temperature in range(-40,151):
    assert call('table1',{8:0x2000,9:0xE0,10:0x4D0,11:temperature,12:1})==950
    count+=1
# Both paired injection positions must exist on a 60-2 wheel.
for phase in range(0,1800,60):
    mem.write8(0x382918,phase>>8);mem.write8(0x382919,phase&255)
    valid=call('cal_validate',{8:0x2000,9:0xE0,10:0x2F00,11:0xE0})&255
    assert valid==(phase<=1620),(phase,valid)
    count+=1
print(f'PASS {count+1} Keil-machine-code arithmetic/calibration checks')
