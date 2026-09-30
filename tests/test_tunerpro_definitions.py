"""Check shipped definitions and boot a packed tune using the linked C167 code."""
from pathlib import Path
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from pack_stock_tune import pack
import tunerpro_definition
for mode in ['speed_density', 'alpha_n']:
    generated = ET.tostring(tunerpro_definition.generate(mode == 'alpha_n').getroot())
    shipped = ET.tostring(ET.parse(ROOT/f'tunerpro/TU744_schema5_{mode}.xdf').getroot())
    assert generated == shipped, f'{mode} XDF is stale; run tools/tunerpro_definition.py'
header = (ROOT/'tunerpro/dtc_table.hpp').read_text(encoding='utf-8')
assert header == tunerpro_definition.dtc_header(), 'dtc_table.hpp is stale; run tools/tunerpro_definition.py'
# The client's P-code table matches the ROM report table for every switchable event.
from tune_client import DTC_CODES
_codes = tunerpro_definition.dtc_codes()
assert set(DTC_CODES) == {e for e, *_ in tunerpro_definition.DTC_EVENTS}
for _event, _row in DTC_CODES.items():
    assert _row == tuple(_codes[_event][s][0] for s in (1, 2, 4, 8)), hex(_event)

for mode in ['speed_density', 'alpha_n']:
    root = ET.parse(ROOT/f'tunerpro/TU744_schema5_{mode}.xdf').getroot()
    ids = [n.get('uniqueid') for n in root if n.get('uniqueid')]
    assert len(ids) == len(set(ids))
    assert root.find('XDFHEADER/DEFAULTS').get('lsbfirst') == '0'
    for node in root.findall('.//EMBEDDEDDATA'):
        if 'mmedaddress' not in node.attrib: continue
        start = int(node.get('mmedaddress'), 0)
        length = int(node.get('mmedelementsizebits'))//8
        length *= int(node.get('mmedrowcount'))*int(node.get('mmedcolcount'))
        assert 0 <= start < start+length <= 3072
    tables = {n.findtext('title'): n for n in root.findall('XDFTABLE')}
    for title, address, bits in [('Running VE',0,8), ('Cranking VE',0x490,16),
                                  ('After-start multiplier',0x4b0,8),
                                  ('Acceleration multiplier',0x759,8)]:
        z = tables[title].find("XDFAXIS[@id='z']")
        assert int(z.find('EMBEDDEDDATA').get('mmedaddress'),0) == address
        assert int(z.find('EMBEDDEDDATA').get('mmedelementsizebits')) == bits
        assert z.find('MATH').get('equation') == 'X'
    axis = tables['Running VE'].find("XDFAXIS[@id='y']/EMBEDDEDDATA")
    assert int(axis.get('mmedaddress'),0) == (0x420 if mode=='speed_density' else 0x440)
    # TunerPro: CATEGORY index is 0-based, CATEGORYMEM category is index+1.
    names = {int(c.get('index'),16)+1: c.get('name') for c in root.find('XDFHEADER').findall('CATEGORY')}
    assert sorted(names) == list(range(1, len(names)+1))
    member = lambda n: [names[int(m.get('category'))] for m in n.findall('CATEGORYMEM')]
    assert all(member(n) for n in root if n.tag != 'XDFHEADER')
    assert 'Ignition' in member(tables['Ignition advance'])
    # DTC switches follow diagnostic_monitor_enabled/subtype_enabled bit layout.
    import tunerpro_definition as definition
    codes = definition.dtc_codes()
    flags = [n for n in root.findall('XDFFLAG') if 'DTC switches' in member(n)]
    events = {}
    for n in flags:
        at = int(n.find('EMBEDDEDDATA').get('mmedaddress'),0)
        mask = int(n.findtext('mask'),16)
        assert bin(mask).count('1') == 1
        event = int(re.search(r'Event 0x([0-9A-F]{2})', n.findtext('description'))[1], 16)
        if member(n) == ['DTC switches']:
            assert (at, mask) == (0x940 + event//8, 1 << event%8)
            events[event] = n.findtext('title')
        else:
            assert at == 0x980 + event and mask in (1, 2, 4, 8)
            assert n.findtext('title').startswith(codes[event][mask][0] + ' ')
    for event, subtypes, _, _ in definition.DTC_EVENTS:
        raised = sorted({codes[event][s][0] for s in subtypes})
        assert events[event].startswith('/'.join(raised) + ' ')
    # The basemap enables exactly the events offered as switches, and every
    # value it holds is displayable within the XDF limits.
    basemap = (ROOT/'basemaps/TU5JP_1.6_8v_speed_density_v0.0.2.bin').read_bytes()
    enabled = {e for e in range(107) if basemap[0x940 + e//8] >> (e % 8) & 1}
    assert enabled == set(events), enabled ^ set(events)
    for n in root.findall('XDFCONSTANT'):
        e = n.find('EMBEDDEDDATA'); at = int(e.get('mmedaddress'),0)
        raw = int.from_bytes(basemap[at:at+int(e.get('mmedelementsizebits'))//8], 'big',
                             signed=e.get('mmedtypeflags') == '0x01')
        # Equations are this project's generated arithmetic, not external input.
        value = eval(n.find('MATH').get('equation').replace('X', f'({raw})'))
        assert float(n.findtext('min'))-1e-9 <= value <= float(n.findtext('max'))+1e-9, n.findtext('title')

root = ET.parse(ROOT/'tunerpro/TU744_schema5.adx').getroot()
assert int(root.findtext('ADXHEADER/objectcount')) == len(root)-1
packets = {n.get('idhash'): int(n.findtext('packetsize')) for n in root.findall('ADXCLISTENPACKET')}
for node in root.findall('ADXVALUE'):
    assert int(node.findtext('packetoffset'),0) + int(node.findtext('sizeinbits'))//8 <= packets[node.findtext('parentcmdidhash')]
for node in root.findall('ADXCSENDCOMMAND'):
    frame = bytes.fromhex(node.findtext('bytestring'))
    assert len(frame)==4 and frame[:2]==b'\xaa\x01' and frame[3]==(frame[2]+1)&255
    assert frame[2] in (0x10,0x25,0x2b)
values = {n.get('id'):n for n in root.findall('ADXVALUE')}
assert values['ADV'].findtext('flags')=='0x00000001'
assert int(values['CLT'].findtext('packetoffset'), 0) == 82
assert int(values['IAT'].findtext('packetoffset'), 0) == 84
assert values['CLT'].findtext('sizeinbits') == '16'
assert values['IAT'].findtext('sizeinbits') == '16'
assert values['PW'].find('MATH').get('equation')=='X/1000'
assert values['VE'].findtext('sizeinbits')=='16'
assert values['AE'].findtext('sizeinbits')=='16'

if '--stock-95080' not in sys.argv: sys.argv.append('--stock-95080')
import test_target as target
probe_dir=ROOT/'build/tunerpro-definitions'
probe_dir.mkdir(exist_ok=True)
probe=probe_dir/'layout.c'
probe.write_text('#include "ecu.h"\n#include <stddef.h>\nconst u16 layout[]={offsetof(Ecu,cal)+offsetof(Calibration,valid)};\n')
keil=Path(os.environ.get('KEIL_C166',str(Path.home()/'AppData/Local/Keil_v5/C166')))
result=subprocess.run([str(keil/'BIN/C166.EXE'),str(probe),'LARGE','MOD167','SRC',
                       f'INCDIR({ROOT/"include"})'],cwd=probe_dir,capture_output=True,text=True)
assert result.returncode==0 and '0 WARNING(S),  0 ERROR(S)' in result.stdout,result.stdout
offset=int(re.search(r'\bDW\s+([0-9A-F]+)H',probe.with_suffix('.SRC').read_text())[1],16)
ecu=int(re.search(r'^\s+([0-9A-F]{6})H\s+ecu\s+VAR',(target.BUILD/'TU5JP.m66').read_text(),re.M)[1],16)
target.call('cal_example', {8:0x2000,9:0xe0})
tune = bytes(target.mem.read8(0x382000+i) for i in range(3072))
folder=ROOT/'build/stock-95080'
firmware=(folder/'TU5JP_STOCK95080_EXPERIMENTAL.bin').read_bytes()
manifest=json.loads((folder/'manifest.json').read_text())
packed=pack(firmware,tune,manifest)
assert packed[:0x50000]==firmware[:0x50000] and packed[0x50c20:]==firmware[0x50c20:]
target.image[:] = packed
target.call('ecu_init',{8:1})
target.call('storage_load',{},limit=2000000)
# Check boot admission and read through the compiler's far-pointer return ABI.
assert target.mem.read8(ecu+offset)==1
target.call('cal_active',{})
address=(target.cpu.rw(5)<<14)+(target.cpu.rw(4)&0x3fff)
assert bytes(target.mem.read8(address+i) for i in range(3072))==tune
target.image[0x50020] ^= 1
target.call('ecu_init',{8:1})
target.call('storage_load',{},limit=2000000)
assert target.mem.read8(ecu+offset)==0
for bad in [tune[:-1], tune[:0x903]+b'\x03'+tune[0x904:]]:
    try: pack(firmware,bad,manifest)
    except ValueError: pass
    else: raise AssertionError('invalid tune format accepted')
try: pack(packed,tune,manifest)
except ValueError: pass
else: raise AssertionError('modified firmware accepted')
print('PASS XDF/ADX bounds, fuel units, framed logging and packed-tune C167 boot/CRC rejection')
