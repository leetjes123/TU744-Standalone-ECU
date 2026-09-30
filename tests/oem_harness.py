"""Shared unchanged-ROM and optional Keil-machine-code test support."""
from pathlib import Path
import ctypes as C
import hashlib
import os
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from oem_repo import oem_repo
REPO = oem_repo()
sys.path.insert(0,str(REPO/'src'))
from c167re.emu.cpu.core import Cpu
from c167re.emu.mem.space import Device,Memory

def ram(address):
    return address if address>=0xF000 else 0x380000+(address&0x3FFF)

class Rom:
    def __init__(self):
        self.data=(REPO/'bins/M744_C167_FULL.bin').read_bytes()
        assert hashlib.sha256(self.data).hexdigest()=='5710015f7c5c066c860a1757fd893f305701608c25af8ff23bfcb4fd1e4837b3'
        self.mem=Memory(fault_on_unmapped=True)
        self.mem.attach(Device('flash',0x800000,len(self.data),bytearray(self.data),writable=False,mirrors=(0,)))
        self.mem.attach(Device('ram',0x380000,0x8000,writable=True))
        self.cpu=Cpu(self.mem)

    def invoke(self,entry,registers=None,stop_at=None,limit=20000):
        cpu=self.cpu;mem=self.mem
        cpu.reset(ip=entry&65535,csp=0x80+(entry>>16));cpu.dpp=[0x204,0x205,0xE0,3]
        cpu.set_rw(0,0xE700)
        for r,v in (registers or {}).items():cpu.set_rw(r,v)
        stack=cpu.sp
        for _ in range(limit):
            if stop_at is not None and cpu.pc()==0x800000+stop_at:
                assert not cpu.traps
                return
            if mem.read16(cpu.pc())==0xDB and cpu.sp==stack:
                assert not cpu.traps and cpu.rw(0)==0xE700,(hex(entry),cpu.traps)
                return
            cpu.step()
        raise AssertionError((hex(entry),hex(cpu.pc()),cpu.traps))

def library():
    native=C.CDLL(str(ROOT/'build/oem'/('oem.dll' if os.name=='nt' else 'oem.so')))
    if '--target' not in sys.argv:
        return native
    import test_target as target

    class Function:
        def __init__(self,name):
            self.name=name;self.native=getattr(native,name)
        def __setattr__(self,name,value):
            if name in ('argtypes','restype'):setattr(self.native,name,value)
            else:object.__setattr__(self,name,value)
        def __call__(self,*args):
            state=args[0]._obj
            for i,b in enumerate(bytes(state)):target.mem.write8(0x382800+i,b)
            regs={8:0x2800,9:0xE0}
            # C166 passes at most five words in R8..R12; subsequent scalar
            # arguments are on the user stack at the call boundary.
            for register,value in enumerate(args[1:],10):
                if register <= 12: regs[register]=value
                else: target.mem.write16(0x383E00+2*(register-13),value & 65535)
            expected=self.native(*args)
            result=target.call(self.name,regs)
            actual=bytes(target.mem.read8(0x382800+i) for i in range(C.sizeof(state)))
            assert actual==bytes(state),(self.name,'Keil state',[(i,a,b) for i,(a,b) in enumerate(zip(actual,bytes(state))) if a!=b][:12])
            if self.native.restype is not None:
                # C166 returns an unsigned char in RL4; RH4 is unspecified.
                if self.native.restype is C.c_uint8:result &= 255
                assert result==expected,(self.name,'Keil return',result,expected)
            return expected

    class Library:
        def __init__(self):self.functions={}
        def __getattr__(self,name):
            if name not in self.functions:self.functions[name]=Function(name)
            return self.functions[name]
    return Library()
