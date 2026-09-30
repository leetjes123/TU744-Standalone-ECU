"""Explicit legacy LRE-B4 -> schema-4 migration, with a per-change report.

Supports the normalized 4112-byte LRW contract used by the supplied 1.6 basemap.
Uses the actual native firmware parser/validator, built by build.py oem-library.
Cranking conversion is a cold-soak reference (IAT=CLT, MAP=100 kPa), not an
equivalence claim over all sensor states. No ECU I/O is performed.
"""
from pathlib import Path
import argparse
import ctypes as C
import hashlib
import json
import os
import re
from tune_client import Client, frame
from artifacts import require

ROOT = Path(__file__).resolve().parents[1]


def native_client():
    lib = C.CDLL(str(ROOT/'build/oem'/('oem.dll' if os.name=='nt' else 'oem.so')))
    lib.bridge_exchange.argtypes = [C.c_char_p, C.c_uint16, C.c_void_p]
    lib.bridge_exchange.restype = C.c_uint16
    lib.bridge_reset()
    def exchange(payload):
        request = frame(payload)
        response = C.create_string_buffer(256)
        n = lib.bridge_exchange(request, len(request), response)
        b = response.raw[:n]
        if len(b)<3 or b[0]!=0x55 or len(b)!=b[1]+3 or sum(b[1:-1])&255 != b[-1]:
            raise RuntimeError('invalid native validator response')
        return b[2:-1]
    return Client(exchange)


def word(data, at, signed=False):
    return int.from_bytes(data[at:at+2], 'big', signed=signed)


def legacy_temperatures(source, name):
    text = source.read_text()
    body = re.search(rf'static const char {name}_table\[1024\] = \{{(.*?)\}};', text, re.S)
    if not body:
        raise ValueError(f'missing {name} legacy ADC table')
    values = [int(n) for n in re.findall(r'-?\d+', body[1])]
    require(len(values)==1024, 'legacy ADC table must have 1024 entries')
    if name == 'CLT':
        values = [min(127, n+26) for n in values]
    require(all(a>=b for a,b in zip(values,values[1:])), 'legacy ADC table is not monotonic')
    return values


def ntc_value(raw, pullup, resistances, temperatures):
    resistance = pullup*raw//(1024-raw)
    if resistance >= resistances[0]:
        return temperatures[0]
    for i,(a,b) in enumerate(zip(resistances,resistances[1:])):
        if resistance>=b:
            f=(a-resistance)*256//(a-b)
            return temperatures[i]+(temperatures[i+1]-temperatures[i])*f//256
    return temperatures[-1]


def convert(original, calculations, extension):
    if len(original)!=4112 or original[0x8cf] not in range(50,101) or original[0x8d0]>1:
        raise ValueError('expected normalized 4096+16 byte LRE-B4 calibration; private fuel scale unsupported')
    data=bytearray(original[:3072])
    changes=[]
    details={}
    def change(at, values, reason):
        values=bytes(values)
        before=bytes(data[at:at+len(values)])
        data[at:at+len(values)]=values
        changes.append(dict(offset=f'0x{at:03x}', length=len(values), before=before.hex(),
                            after=values.hex(), reason=reason))
    def words(at, values, reason):
        change(at,b''.join((v&65535).to_bytes(2,'big') for v in values),reason)
    # The legacy lookup indexes RPM rows/load columns; schema 4 indexes load
    # rows/RPM columns. All four maps require a transpose, even constant maps.
    for base in (0,0x100,0x200,0x300):
        change(base,[original[base+16*r+l] for l in range(16) for r in range(16)],
               'Transpose RPM-row/load-column storage to schema-4 load-row/RPM-column storage.')
    require(original[0x200:0x300] == bytes([147])*256, 'cranking reference requires explicit AFR mapping')
    temps=[word(original,0x460+2*i,True) for i in range(16)]
    required=word(original,0x5df)
    crank=[word(original,0x490+2*i) for i in range(16)]
    ve=[round(pw*200*(27315+t*100)/(required*27315)) for pw,t in zip(crank,temps)]
    words(0x490,ve,'Cranking us -> effective VE%; cold-soak reference IAT=CLT, MAP=100 kPa, target AFR=14.7; nearest whole percent.')
    realized=[((required*27315//(27315+t*100))*v//100)//2 for t,v in zip(temps,ve)]
    details['cranking_reference']=dict(coolant_and_intake_c=temps, map_kpa=100,
        legacy_us_before_deadtime=crank, converted_ve_percent=ve,
        converted_us_before_deadtime=realized, error_us=[a-b for a,b in zip(realized,crank)])
    if max(original[0x4b0:0x4c0])>155:
        raise ValueError('after-start multiplier cannot represent legacy excess >155%')
    change(0x4b0,[100+n for n in original[0x4b0:0x4c0]],'After-start excess percent -> total multiplier; decay duration retained.')
    for at,name in ((0x550,'CLT'),(0x572,'IAT')):
        if word(original,at) not in (0,65535):
            continue
        values=legacy_temperatures(calculations,name)
        adcs=[]
        for t in temps:
            best=min(abs(v-t) for v in values)
            indices=[i for i,v in enumerate(values) if abs(v-t)==best]
            adcs.append(round(sum(indices)/len(indices)))
        pullup=1000
        resistances=[max(1,round(pullup*a/(1024-a))) for a in adcs]
        if max(resistances)>65535 or not all(a>b for a,b in zip(resistances,resistances[1:])):
            raise ValueError('NTC fit cannot be represented')
        words(at,[pullup]+resistances,'Represent the active legacy fallback ADC curve on the existing 16-point temperature axis. 1000 ohm is a mathematical ratio scale, not a measured board resistor.')
        errors=[ntc_value(i,pullup,resistances,temps)-max(temps[0],min(temps[-1],v)) for i,v in enumerate(values)]
        details[name+'_curve']=dict(adc_knots=adcs, synthetic_pullup_ohms=pullup,
            max_error_against_axis_clamped_legacy_c=max(map(abs,errors)),
            max_full_range_error_c=max(abs(ntc_value(i,pullup,resistances,temps)-v) for i,v in enumerate(values)),
            original_min_c=min(values),original_max_c=max(values), output_min_c=temps[0],output_max_c=temps[-1])
    # New schema fields use explicit firmware defaults, not invented legacy values.
    change(0x900,extension[0x900:],'New schema fields: current firmware defaults, listed verbatim; engine tables above are retained/converted separately.')
    change(0x5d4,[original[0x5d4]&0x37],'Remove deprecated idle-mode bit 3; preserve active strategy flags.')
    change(0x5d9,[5],'New SSC waveform fixes inter-step drive timing at 5 ms (legacy tune requested 6 ms).')
    change(0x5ec,[original[0x5ec]*10],'Fuel-pump prime seconds -> 100 ms units.')
    change(0x603,[original[0x603]*10],'DFCO delay seconds -> 100 ms units.')
    trigger=word(original,0x605)
    if trigger in (0,65535): trigger=word(original,4096)
    words(0x605,[(3600-trigger*5)%3600], 'Legacy fire=(720-(advance_halfdeg+offset_halfdeg))/2; schema fire=trigger10-advance10. Preserve angle with complemented reference, not direct scaling.')
    axis=[word(original,0x610+2*i,True) for i in range(8)]
    advances=[int.from_bytes(bytes([v]),'big',signed=True) for v in original[0x620:0x628]]
    words(0x610,[-v for v in reversed(axis)],'Reverse/negate RPM-error axis: legacy RPM-target, schema target-RPM.')
    change(0x620,[v+40 for v in reversed(advances)],'Reverse idle ignition values with the axis; signed half-degrees -> biased encoding (raw*0.5-20).')
    words(0x634,[0],'D term is not implemented in schema 4; legacy idle mode 0 does not use the stored D=2.000.')
    require(original[0x740:0x7a1] == bytes(0x61), 'nonzero legacy additive AE needs an explicit reference conversion')
    # Zero additive enrichment has an exact neutral multiplier conversion.
    words(0x740,[50,100,200,400,600,800,1000,1500],'Replace unused zero AE axis with strictly increasing legacy default breakpoints; enrichment remains neutral.')
    change(0x750,[0,10,25,50,75,90],'Unused AE TPS axis -> valid legacy default breakpoints.')
    change(0x758,[1],'Legacy runtime clamps zero detect duration to one 10 ms tick.')
    change(0x759,[100]*48,'Zero additive AE -> neutral 100% multiplier.')
    words(0x789,[500,1000,2000,3000,4000,5000,6000,7000],'Unused zero AE RPM axis -> valid legacy default breakpoints.')
    change(0x799,[100]*8,'AE RPM modifier is immaterial to neutral enrichment; use 100%.')
    words(0x910,[original[4096+5]],'IAC travel copied from the supplied EEPROM settings.')
    words(0x91a,[(original[4096+8]-40)*5],'EEPROM cranking timing: biased half-degrees -> signed tenths; 70 raw -> 15 degrees.')
    # The supplied legacy firmware actually uses fixed T0=29/57, ignoring its
    # 360-degree injection-angle table. New scheduler has an exact 180-degree
    # pair spacing and requires both anchors on real teeth; default 27/57.
    details['injection_phase']=dict(legacy_one_based_teeth=[29,57],
        legacy_zero_based_degrees=[168,336], schema_zero_based_degrees=[162,342],
        delta_degrees=[-6,6], unused_legacy_angle_cells=[word(original,0x8d1+2*i) for i in range(4)])
    # The supplied file selects narrowband. Preserve that choice and its
    # existing two heater outputs; a physical Spartan installation is a
    # separate equipment setting, not inferred from a filename.
    change(0x916,[3],'Legacy commands both O2 heater outputs in running mode; record equipment flags. Supplied narrowband selection remains unchanged.')
    details['semantic_differences']=[
        'Cranking reference assumes IAT=CLT and MAP=100 kPa; other sensor combinations change fuel. Dead time is preserved separately.',
        'Legacy temp_corrected received whole Celsius where its formula expected centi-Celsius. Schema 4 applies physical IAT density. Required fuel and VE values are preserved, so running fuel is not identical away from 0 C.',
        'Legacy crank-enrichment hold is replaced by qualified engine-state transition (650/450 rpm, 300 ms); after-start and AE decay policies differ.',
        'Schema 4 has fixed 180-degree injection-pair spacing; old active 168-degree spacing cannot be represented exactly.',
        'Sensor curves are approximated at the existing 16 temperature knots and clamp at -29/120 C; original fallback curves extended beyond those endpoints.',
        'Legacy stored idle gains are retained except unused D; controller, dwell-feedback and diagnostic policies are those of the new firmware.',
        'Supplied file selects narrowband (0x600=0). No wideband equipment conversion is inferred.'
    ]
    return bytes(data),dict(changes=changes,details=details)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('input',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--legacy-source',type=Path,required=True)
    a=p.parse_args()
    original=a.input.read_bytes()
    client=native_client()
    converted,report=convert(original,a.legacy_source/'calculations.c',client.read_tune())
    try: client.activate(converted)
    except RuntimeError as e:
        raise RuntimeError(f'{e}; validator status: {client.status()}') from e
    require(client.read_tune()==converted, 'native converted calibration readback mismatch')
    report.update(input=str(a.input),input_bytes=len(original),input_sha256=hashlib.sha256(original).hexdigest(),
        output=str(a.output),output_bytes=len(converted),output_sha256=hashlib.sha256(converted).hexdigest(),
        validation='Actual native C protocol begin/write/commit validation and exact readback passed',
        legacy_source_sha256={name:hashlib.sha256((a.legacy_source/name).read_bytes()).hexdigest()
                              for name in ('calculations.c','main.c','memloc.c','eeprom.c','ignition.c','TriggerWheel.c')},
        engine_calibration_validation=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_bytes(converted)
    a.output.with_suffix('.conversion.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('changes','legacy_source_sha256')},indent=2))


if __name__=='__main__': main()
