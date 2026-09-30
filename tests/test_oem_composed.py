"""Shared native RAM identities across sensors, DTC records, phases and MIL.

The oracle executes consecutive original routines, including their real event
ingestion. State is seeded/read through address bindings, not a Python model of
the replacement's binding or transition logic.
"""
import ctypes as C
import json
import random
import sys
from oem_harness import ROOT,Rom,ram,library
from oem_types import Diagnostics, Adc
from vss_input_layout import words as vss_words, octets as vss_octets
from digital_layout import words as digital_words, octets as digital_octets, address

rom=Rom();mem=rom.mem;lib=library();rng=random.Random(0x145DA)
lib.oem_adc_publish.argtypes=[C.POINTER(Adc)];lib.oem_adc_publish.restype=None
lib.oem_diagnostics_adc.argtypes=[C.POINTER(Diagnostics),C.c_uint16,C.c_uint8,C.c_uint8]
lib.oem_diagnostics_adc.restype=None
bindings=[]
def field(path,address,width=1):bindings.append((tuple(path.split('.')),address,width))
def array(path,addresses,width=1):
    for i,a in enumerate(addresses):bindings.append(((*path.split('.'),i),a,width))
coolant=json.loads((ROOT/'docs/oem-state-layout.json').read_text())
clt_bytes={*range(0x9505,0x950F),0x9510,0xAA5C,0x8AFC,0x8AFD,0xF86C}
for i,a in enumerate(coolant['addresses']):bindings.append((('coolant','value',i),a,1 if a in clt_bytes else 2))
array('coolant.flags',coolant['flags'],2)
for name,a in [('descriptor',0xB2DA),('coolant_descriptor',0xB2E6),('status',0x8B18),('filter',0x8B16),('startup_flags',0xFD14),('run_flags',0xFD16)]:field('iat.'+name,a,2)
for name,a in [('adc',0x9208),('raw',0x950F),('filtered',0x9510),('captured',0xAA5D),('pass_count',0x8B14),('fail_count',0x8B15),('coolant',0x950E)]:field('iat.'+name,a)
for name,a in [('run_flags',0xFD16),('rotation_flags',0xFD6A),('count_a',0x9616),('running_count',0x9618)]:field('engine.'+name,a,2)
for name,a in [('coolant',0x950E),('iat',0x9510),('speed',0xF8AC)]:field('engine.'+name,a)
layout=json.loads((ROOT/'docs/oem-input-layout.json').read_text())
array('context.value',layout['context_words'],2)
field('context.coolant',0x9507);field('context.vehicle_speed',0x9201)
array('context.output',layout['context_output'])
for i in range(20):
    for j in range(24):bindings.append((('events','store','records',i,j),0xB044+24*i+j,1))
array('events.live',range(0xB224,0xB2FA,2),2)
for name,a in [('count',0xAA64),('demand',0xB043),('active_demand',0xB042)]:field('events.store.'+name,a)
array('events.store.phases',[0xAA5E,0x951A,0x9519,0x952B,0x9593,0x9594,0xAA5F])
array('events.context',[0x9305,0xAA66,0x92C2,0x9521,0x951C,0x951B,0x951D,0xF8AC,0x9528])
for name,a in [('gate',0xFD6C),('timestamp',0xB2FA),('clear_request',0x8B36),('clear_inverse',0x8B38),('clear_mode',0x8B3A),('startup_flags',0xFD14),('run_flags',0xFD16),('clock_divider',0x8B3C),('drive_timer',0x8B30),('drive_count',0xB040)]:field('events.'+name,a,2)
for name,a in [('last_event',0xAA63),('overflow',0xAA65),('scan_event',0xAA62),('clear_previous',0x8B32),('scan_record',0xAA60),('scan_unused',0xAA61),('clear_wait',0x8B33),('lock_wait',0x8B34),('coolant',0x950E),('warmup_start',0x952A),('warmup_count',0xAA67)]:field('events.'+name,a)
for name,a in [('state',0x952D),('retained',0xAA6E),('prove_count',0x8B40),('prove_flags',0x8B42),('flash_count',0x8B41),('demand',0xB043)]:field('mil.'+name,a)
for name,a in [('fd08',0xFD08),('fd0e',0xFD0E),('fd12',0xFD12),('fd5a',0xFD5A),('fd6a',0xFD6A)]:field('mil.'+name,a,2)
for name,a in [('descriptor',0xB2EE),('speed_descriptor',0xB2F4),('status',0x8A8A),('run_flags',0xFD16),
               ('fraction',0x8A90),('filter_high',0x8A92),('scaled',0x9B84),('filtered',0x9B82)]:field('voltage.'+name,a,2)
for name,a in [('adc',0x9209),('voltage',0x94E8),('scaled_byte',0x94E9),('filtered_byte',0x94EA),
               ('delay',0x8A8C),('fail_count',0x8A8D),('pass_count',0x8A8E),('divider',0x8A8F),('vehicle_speed',0x9201)]:field('voltage.'+name,a)
for name,a in [('descriptor',0xB2F4),('source_a_descriptor',0xB254),('source_b_descriptor',0xB256),
               ('status',0x8178),('fd06',0xFD06),('fd08',0xFD08),('fd18',0xFD18),('fd52',0xFD52),
               ('fd5e',0xFD5E),('speed',0x95AA),('condition_speed',0x95A4)]:field('vss.'+name,a,2)
for name,a in [('source',0x94A3),('source_a_status',0x9477),('source_b_status',0x947D),
               ('fail_count',0x8174),('pass_count',0x8175),('source_count',0x8176),
               ('coolant',0x950E),('engine_speed',0xF8AC),('load',0xF86E)]:field('vss.'+name,a)
for name,a in vss_words.items():field('vss_input.'+name,a,2)
for name,a in vss_octets.items():field('vss_input.'+name,a)
for name,a in digital_words.items():field('digital.'+name,a,2)
for name,a in digital_octets.items():field('digital.'+name,a)
array('readiness.descriptor',[0xB282,0xB2E2,0xB2DE,0xB2CA,0xB2AE,0xB2AC,0xB2A0,0xB2A4,0xB2A2,0xB27E,0xB27A],2)
for name,a in [('fd02',0xFD02),('config_a',0x959C),('config_b',0x959E),('startup_flags',0xFD14)]:field('readiness.'+name,a,2)
array('readiness.count',range(0xAA68,0xAA6D))
for name,a in [('supported',0x952C),('pending',0xAA6D),('once',0x8B3E)]:field('readiness.'+name,a)

def member(s,path):
    for part in path:s=s[part] if isinstance(part,int) else getattr(s,part)
    return s
def read_state():
    s=Diagnostics()
    for path,a,width in bindings:
        value=(mem.read8 if width==1 else mem.read16)(address(a));parent=member(s,path[:-1]);last=path[-1]
        if isinstance(last,int):parent[last]=value
        else:setattr(parent,last,value)
    s.mil.lamp=(mem.read16(0xFD6C)>>8)&1
    return s
def compare(s,context):
    for path,a,width in bindings:
        value=(mem.read8 if width==1 else mem.read16)(address(a));expected=member(s,path)
        assert value==expected,(context,path,hex(a),value,expected)
    assert s.mil.lamp==(mem.read16(0xFD6C)>>8)&1,(context,'lamp')

sequences={'sensors':[0x697CE,0x68E90],'capture':[0x69970,0x696D6],
           'context':[0x6B8BA],'engine':[0x329D6],'demand':[0x69F9A,0x6BE52,0x6AA60,0x6C1B0],
           'voltage_base':[0x66422],'voltage':[0x664A2],'vss':[0x2A01E],
           'vss_input':[0x29CCC],'digital':[0x726CE,0x2B8E8],'digital_aux':[0x2BA18],
           'readiness':[0x6BEDA]}
for name in sequences:
    f=getattr(lib,'oem_diagnostics_'+name);f.argtypes=[C.POINTER(Diagnostics)]
    f.restype=C.c_uint8 if name in ('sensors','demand','voltage','vss') else None
cases=0
for name,entries in sequences.items():
    for case in range(400):
        for _,a,width in bindings:(mem.write8 if width==1 else mem.write16)(address(a),rng.randrange(256 if width==1 else 65536))
        mem.write8(ram(0xAA64),case%21)
        for slot in range(20):
            mem.write8(ram(0xB044+24*slot),rng.choice([0x5B,0x61,rng.randrange(107)]))
            mem.write8(ram(0xB049+24*slot),rng.randrange(38))
        for a in [0x8B14,0x8B15,0x8AFC,0x8AFD]:mem.write8(ram(a),rng.choice([0,1,5,20,255]))
        for a in [0x8AFE,0x8B00,0x8B02,0x8B04]:mem.write16(ram(a),rng.choice([0,1,5,20,300]))
        mem.write8(ram(0x9208),case%256);mem.write16(ram(0x95B4),0xA000|rng.randrange(1024))
        s=read_state();getattr(lib,'oem_diagnostics_'+name)(C.byref(s))
        for entry in entries:rom.invoke(entry)
        compare(s,(name,case));cases+=1

# Retain the independent C and ROM states across a shared sensor/fault history.
# Inputs are synthetic native values; this is not an assertion about the board
# front end or omitted task producers. No post-call state is copied from oracle
# to replacement. The divider phases below follow 28F3A/28D9A.
for _,a,width in bindings:(mem.write8 if width==1 else mem.write16)(address(a),0)
mem.write16(ram(0x95B4),0xA200);mem.write8(ram(0x9208),90)
for entry in [0x69740,0x68C4A,0x329C8,0x663C0,0x69F8C,0x6BE44,0x6C334,0x72696,0x6C116]:
    rom.invoke(entry)
s=read_state()

def input_value(address,value,width=1):
    (mem.write8 if width==1 else mem.write16)(ram(address),value)
    if address==0xFD6C:
        s.mil.lamp=(value>>8)&1  # bit alias of the externally supplied word
    for path,a,w in bindings:
        if a==address:
            assert w==width
            parent=member(s,path[:-1]);last=path[-1]
            if isinstance(last,int):parent[last]=value
            else:setattr(parent,last,value)

input_value(0xFD6C,0x20,2)
seen=set();releases=[1,2,4,10,20];reload=[2,5,10,20,100]
for tick in range(2400):
    phase=tick//400
    adc=Adc()
    for i in range(16):adc.scan[i]=((15-i)<<12)|((tick+i*73)&1023)
    adc.scan[4]=0xB000|([90,0,90,255,90,90][phase]*4)|(tick&3)
    adc.scan[5]=0xA000|[512,512,1023,0,512,512][phase]
    adc.scan[10]=0x5000|([127,35,80,160,127,127][phase]*4)|(tick&3)
    for i in range(16):mem.write16(0xF7B0+2*i,adc.scan[i])
    lib.oem_adc_publish(C.byref(adc))
    lib.oem_diagnostics_adc(C.byref(s),adc.input[4],adc.iat,adc.battery)
    rom.invoke(0x2C188)
    compare(s,('retained ADC binding',tick));cases+=1
    input_value(0xFD6A,0x40 if tick>=30 else 0,2)
    input_value(0xF8AC,0 if tick<30 else 50)
    input_value(0xF8AE,0 if tick<30 else 8000,2)
    input_value(0x9201,30+(tick%100))
    input_value(0x9ACE,5000+(tick%1000),2)
    input_value(0x9AD0,6000+(tick%1000),2)
    input_value(0x94A3,3 if 600<=tick<1400 else 0)
    input_value(0xFD5E,0x2000,2)
    if tick==1200:input_value(0x8A8C,0)
    input_value(0xFFC8,16 if tick<2100 else 0,2)
    input_value(0xFFCC,8 if tick%80<40 else 0,2)
    names=['digital','voltage_base','context','engine']
    for i in range(5):
        releases[i]-=1
        if releases[i]==0:
            releases[i]=reload[i]
            if i==1:names.append('vss_input')
            if i==2:names.extend(['digital_aux','sensors','vss','voltage'])
            elif i==3:names.extend(['demand','readiness'])
            elif i==4:names.append('capture')
    for name in names:
        result=getattr(lib,'oem_diagnostics_'+name)(C.byref(s))
        if name in ('sensors','demand','voltage','vss'):assert result==1
        for entry in sequences[name]:rom.invoke(entry)
        compare(s,('retained',tick,name));cases+=1
    if s.events.live[0x5B]&1:seen.add('IAT fault')
    if s.events.live[0x61]&1:seen.add('coolant fault')
    if s.events.live[0x65]&1:seen.add('voltage fault')
    if s.events.live[0x68]&1:seen.add('vehicle-speed fault')
    if name=='voltage' and (s.voltage.status&2) and not s.voltage.delay:
        if s.events.live[0x68]&1:
            assert not (s.voltage.status&16)
            seen.add('high voltage inhibited by speed fault')
        elif s.voltage.status&16:seen.add('high voltage enabled after speed recovery')
    if s.events.store.count:seen.add('record allocation')
assert seen=={'IAT fault','coolant fault','voltage fault','vehicle-speed fault','record allocation',
             'high voltage inhibited by speed fault','high voltage enabled after speed recovery'},seen

# The implemented projection of clear task29620 must reset producers while
# request/live bit7 are still present. The full task includes other producers
# which are not silently replaced by no-op implementations here.
clear_entries = [(0x29624, 0x69FDE), (0x29628, 0x6BEB2), (0x29638, 0x68E4E),
                 (0x2965C, 0x6C3AA), (0x296A8, 0x697A4), (0x296AC, 0x2A298),
                 (0x296B4, 0x6677C), (0x296C0, 0x6B4D8), (0x296C4, 0x6C18E)]
for site, entry in clear_entries:
    assert rom.data[site:site+4] == bytes([0xDA, 0x80+(entry >> 16), entry & 255, (entry >> 8) & 255])
clear = lib.oem_diagnostics_clear_ported
clear.argtypes = [C.POINTER(Diagnostics)]
clear.restype = C.c_uint8
for case in range(2400):
    for _, a, width in bindings:
        (mem.write8 if width == 1 else mem.write16)(address(a), rng.randrange(256 if width == 1 else 65536))
    mem.write8(ram(0xAA64), case % 21)
    for slot in range(20):
        mem.write8(ram(0xB044+24*slot), rng.randrange(107))
        mem.write8(ram(0xB049+24*slot), rng.randrange(38))
    request = 106 if case % 3 != 2 else 1+(case//3) % 105
    mem.write16(ram(0x8B36), request)
    mem.write16(ram(0x8B38), request ^ 65535)
    mem.write16(ram(0x8B3A), case % 3 if request == 106 else rng.randrange(65536))
    mem.write16(0xFD6C, mem.read16(0xFD6C) | 32)
    s = read_state()
    assert clear(C.byref(s)) == 1
    for _, entry in clear_entries:
        rom.invoke(entry)
    compare(s, ('ported clear', case))
    assert s.events.clear_request == 0 and s.events.clear_inverse == 65535
    # A duplicate delivery must not clear a later MIL state or reset filters.
    before = bytes(s)
    assert clear(C.byref(s)) == 0 and bytes(s) == before
    cases += 2

# Standalone validation is deliberately stronger than the native worker.
# Check rejection before any alias refresh or partial producer reset.
for failure in ('gate', 'complement', 'empty', 'request', 'mode', 'count', 'event', 'config'):
    s = read_state()
    s.events.gate |= 32
    s.events.clear_request = 106
    s.events.clear_inverse = 0xFF95
    s.events.clear_mode = 0
    s.events.store.count = 1
    s.events.store.records[0][0] = 0x61
    s.events.store.records[0][5] = 0
    if failure == 'gate': s.events.gate &= ~32
    elif failure == 'complement': s.events.clear_inverse ^= 1
    elif failure == 'empty': s.events.clear_request, s.events.clear_inverse = 0, 65535
    elif failure == 'request': s.events.clear_request, s.events.clear_inverse = 107, 107 ^ 65535
    elif failure == 'mode': s.events.clear_mode = 2
    elif failure == 'count': s.events.store.count = 21
    elif failure == 'event': s.events.store.records[0][0] = 107
    elif failure == 'config': s.events.store.records[0][5] = 38
    before = bytes(s)
    assert clear(C.byref(s)) == 0 and bytes(s) == before, failure
    cases += 1
print(f'PASS {cases} composed OEM sensor, event, operating-state and MIL sequences'+
      ('; identical Keil-linked calls also passed' if '--target' in sys.argv else ''))
