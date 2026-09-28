"""Verify migrated map indexing and cranking values in the linked C167 code."""
from pathlib import Path
import json
import sys

import test_fuel_target as fuel
target=fuel.target
ROOT=target.ROOT.parents[1]
folder=ROOT/'docs/audits/tu5jp-output-closure-2026-09-23/calibration'
original=(folder/'LRE-B4-1.6Basemap-original.bin').read_bytes()
converted=(folder/'LRE-B4-1.6Basemap-schema4.bin').read_bytes()
report=json.loads((folder/'LRE-B4-1.6Basemap-schema4.conversion.json').read_text())

def word(b,at,signed=False): return int.from_bytes(b[at:at+2],'big',signed=signed)

fuel.setup()
for i,value in enumerate(converted): target.mem.write8(fuel.cal+i,value)
assert target.call('cal_validate',{**fuel.pointer(fuel.cal,8),10:0x2f00,11:0xe0})&255 == 1
checks=0
for base in (0,0x100,0x200,0x300):
    axis=0x440 if base==0x300 else 0x420
    for rpm_index in range(16):
        for load_index in range(16):
            target.mem.write16(0x383e00,axis)
            actual=target.call('table2',{**fuel.pointer(fuel.cal,8),10:base,
                11:word(original,0x400+2*rpm_index),12:word(original,axis+2*load_index)})&255
            assert actual==original[base+16*rpm_index+load_index], (base,rpm_index,load_index,actual)
            checks+=1

target.mem.write16(fuel.rotation+fuel.layout['Rotation.rpm'],200)
fuel.sensor('map',100); fuel.sensor('battery',12000)
crank=report['details']['cranking_reference']
for temperature, expected in zip(crank['coolant_and_intake_c'],crank['converted_us_before_deadtime']):
    fuel.sensor('clt',temperature);fuel.sensor('iat',temperature)
    fuel.fuel(0,expected+word(original,0x548))
    checks+=1

# Check angular reference against the legacy arithmetic for every representable
# advance, including wrap. This catches the dangerous direct-scale conversion.
for advance in range(-40,101):
    old=(720-word(original,0x605)-advance)%720
    new=(word(converted,0x605)-advance*5)%3600
    assert new==old*5
    checks+=1
assert word(converted,0x91a,True)==(original[4104]-40)*5
assert converted[0x4b0:0x4c0]==bytes(100+n for n in original[0x4b0:0x4c0])
assert converted[0x530:0x550]==original[0x530:0x550]
assert converted[0x759:0x789]==bytes([100])*48
assert [word(converted,0x610+2*i,True) for i in range(8)]==[-word(original,0x610+2*i,True) for i in reversed(range(8))]
print(f'PASS {checks} migrated calibration checks: 1024 linked map cells, 16 linked cranking points, 141 angular references; original dwell/deadtime retained')
