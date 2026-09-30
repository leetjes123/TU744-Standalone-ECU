"""Differential execution of compiled replacement C against unchanged OEM ROM.

This covers native routine contracts, NOT physical inputs/task integration or
the unported event-record lifecycle. DLL functions are the target C sources.
"""
from pathlib import Path
import ctypes as C
from oem_types import Coolant,Mil
import importlib.util
import json
import os
import random
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from oem_repo import oem_repo
REPO = oem_repo()
sys.path.insert(0,str(REPO/'src'))
spec=importlib.util.spec_from_file_location('native_rom_tests',REPO/'tests/test_tu5jp_fueling.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
mem,invoke=module.rom.__wrapped__()
lib=C.CDLL(str(ROOT/'build/oem'/('oem.dll' if os.name=='nt' else 'oem.so')))
layout=json.loads((ROOT/'docs/oem-state-layout.json').read_text())
addresses=layout['addresses'];flag_addresses=layout['flags']
byte_addresses={*range(0x9505,0x950F),0x9510,0xAA5C,0x8AFC,0x8AFD,0xF86C}
rng=random.Random(0x68E90)
cases=0
for entry,name in [(0x68C4A,'init'),(0x68E4E,'reset'),(0x696D6,'capture'),(0x68E90,'update')]:
    function=getattr(lib,'oem_coolant_'+name);function.argtypes=[C.POINTER(Coolant)];function.restype=C.c_uint16
    for case in range(1600):
        state=Coolant()
        for i,a in enumerate(addresses):
            v=rng.randrange(256 if a in byte_addresses else 65536)
            if a==0x95B4:v=(case%1024)|(0xA000 if case&1 else 0)
            if a in (0x8AFE,0x8B00,0x8B02,0x8B04,0x8AFC,0x8AFD,0x9BB8):v=rng.choice([0,1,2,5,6,200,255])
            state.value[i]=v
            (mem.write8 if a in byte_addresses else mem.write16)(a if a>=0xF000 else module.paged_ram(a),v)
        for i,a in enumerate(flag_addresses):state.flags[i]=rng.randrange(65536);mem.write16(a,state.flags[i])
        descriptor=function(C.byref(state))
        invoke(entry,stop_at=0x696BE if name=='update' else None,limit=5000)
        if name=='update':assert mem.read16(0xE6EE)==descriptor,(name,case,'descriptor')
        for i,a in enumerate(addresses):
            actual=(mem.read8 if a in byte_addresses else mem.read16)(a if a>=0xF000 else module.paged_ram(a))
            assert actual==state.value[i],(name,case,hex(a),actual,state.value[i])
        for i,a in enumerate(flag_addresses):assert mem.read16(a)==state.flags[i],(name,case,hex(a))
        cases+=1
mil_bytes=[0x952D,0xAA6E,0x8B40,0x8B42,0x8B41,0xB043]
mil_flags=[0xFD08,0xFD0E,0xFD12,0xFD5A,0xFD6A]
for entry,name in [(0x6C1B0,'update'),(0x6C334,'on'),(0x6C388,'off'),(0x6C3AA,'clear')]:
    function=getattr(lib,'oem_mil_'+name);function.argtypes=[C.POINTER(Mil)]
    for case in range(1600):
        s=Mil()
        for (field,_),a in zip(Mil._fields_,mil_bytes):setattr(s,field,rng.randrange(256));mem.write8(module.paged_ram(a),getattr(s,field))
        s.demand=case%4;mem.write8(module.paged_ram(0xB043),s.demand)
        s.lamp=case%2;mem.write16(0xFD6C,s.lamp<<8)
        for (field,_),a in zip(Mil._fields_[7:],mil_flags):setattr(s,field,rng.randrange(65536));mem.write16(a,getattr(s,field))
        function(C.byref(s));invoke(entry,limit=3000)
        for (field,_),a in zip(Mil._fields_,mil_bytes):assert mem.read8(module.paged_ram(a))==getattr(s,field),(name,case,field)
        assert bool(mem.read16(0xFD6C)&256)==bool(s.lamp),(name,case,'lamp')
        cases+=1
print(f'PASS {cases} compiled-C versus native-ROM routine calls (6400 coolant, 6400 steady-MIL contracts)')
