"""Create the ADX for the TU5JP plugin's validated, echo-free virtual frames."""
from pathlib import Path
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]

# TunerPro's identifier hash is part of the ADX reference format, not an
# arbitrary unique number. Verified against all 44 IDs in its supplied ELM327
# definition and the installed ScannerProModule.dll hash function (RVA 12A50).
# Interoperability evidence: docs/audits/tu5jp-tunerpro-connection-2026-09-23/.
_HASH_BYTES = bytes.fromhex(
    '0157310cb0b266a679c10654f9e62ca30ec5d5b5a155da5040ef18e2ec8e26c8'
    '6eb168678dfdff324d6551122d601fde196bbe4656edf02248f214d6f4e395eb6'
    '1ea39163cfa52afd0057fc76f3e87f8aea9d33a429a6ac3f5ab11bbb6b300f3'
    '8438944b80859e64827e5b0d99f6d8db7744df4e5358c9637a0b5c20887234'
    '0a8a1e30b79c233d1a8f4afb5e81a23f98aa0773a7f1ce0396373b97dc5a35'
    '17837dad0fee4f5f59106989e1e0d9a0257b7649029d2e74099186e4cfd4ca'
    'd745e51bbc437ca8fc2a041d6c15f713cd27cbe928ba93c6c09b21a4bf62cc'
    'a5b4754c8c24d2ac29369f08b9e871c4e72f927833411c90fedd5dbdc28b70'
    '2b476db8d1')


def identifier_hash(identifier):
    if not identifier or len(identifier)>15 or not identifier.isascii():
        raise ValueError('ADX IDs must be nonempty ASCII strings of at most 15 characters')
    value=0xdc379acf
    for byte in identifier.encode('ascii'):
        value ^= _HASH_BYTES[byte]
        value=((value<<5)|(value>>27))&0xffffffff
    return value


# Monitor packet (command 10, extension 3) fields worth watching while tuning.
VALUES=[
    ('RPM','Engine speed',0,16,'RPM',0,12000,'X',0,False),
    ('MAP','Manifold pressure',2,16,'kPa',0,600,'X',0,False),
    ('TPS','Throttle position',4,8,'%',0,100,'X',0,False),
    ('TPS_ADC','TPS raw ADC',54,16,'ADC counts',0,1023,'X',0,False),
    ('CLT','Coolant temperature',82,16,'C',-40,150,'X',0,True),
    ('IAT','Intake temperature',84,16,'C',-40,150,'X',0,True),
    ('BAT','Battery voltage',14,8,'V',0,25.5,'X/10',1,False),
    ('AFR','Wideband measured AFR',15,8,'AFR',0,25.5,'X/10',1,False),
    ('WB_LAMBDA','Wideband lambda (14.7 reference)',15,8,'lambda',0,1.735,'X/147',3,False),
    ('TARGET','Target AFR',16,8,'AFR',7,22,'X/10',1,False),
    ('TARGET_LAMBDA','Target lambda (14.7 reference)',16,8,'lambda',0.476,1.497,'X/147',3,False),
    ('O2_VOLTS','Oxygen input voltage',62,16,'V',0,5,'X*5/1023',3,False),
    ('TRIM','Fuel trim (STFT)',19,8,'%',50,150,'X*100/128',1,False),
    ('VE','VE',76,16,'VE %',0,1000,'X',0,False),
    ('PW','Injector pulse',6,16,'ms',0,25,'X/1000',3,False),
    ('ADV','Ignition advance',86,16,'deg BTDC',-20,50,'X/10',1,True),
    ('AE','Acceleration enrichment',78,16,'%',100,495,'X',0,False),
    ('WARM','Warm-up enrichment',17,8,'%',0,255,'X',0,False),
    ('ASE','After-start enrichment',18,8,'%',100,255,'X',0,False),
    ('IDLETARGET','Idle target',35,16,'RPM',0,2500,'X',0,False),
    ('IAC','Idle valve position',34,8,'steps',0,220,'X',0,False),
    ('SPEED','Vehicle speed',41,8,'km/h',0,255,'X',0,False),
    ('GEAR','Gear (0 = unknown)',42,8,'',0,5,'X',0,False),
    ('DTC_ACTIVE','DTCs active',92,8,'',0,20,'X',0,False),
    ('DTC_STORED','DTCs stored',91,8,'',0,20,'X',0,False),
]
# Named flags: engine state bytes 49/50, MIL byte 88 and the big-endian
# inhibit word at 89/90 (include/ecu.h INH_*). alarm colours the set state red.
FLAGS=[
    ('F_SYNC','Crank sync',49,0x01,'Yes','No',False),
    ('F_CRANK','Cranking',49,0x02,'Yes','No',False),
    ('F_LOOP','Closed loop',49,0x20,'Active','Off',False),
    ('F_DFCO','Overrun fuel cut',49,0x10,'Active','Off',False),
    ('F_REV','Rev limiter',50,0x01,'Limiting','Off',True),
    ('F_FUELCUT','Fuel cut',50,0x20,'Cut','Off',True),
    ('F_SPARKCUT','Spark cut',50,0x10,'Cut','Off',True),
    ('F_LAUNCH','Launch control',50,0x04,'Active','Off',False),
    ('F_ANTILAG','Anti-lag',50,0x08,'Active','Off',False),
    ('F_FAN','Radiator fan',49,0x40,'On','Off',False),
    ('F_PUMP','Fuel pump',49,0x80,'On','Off',False),
    ('F_MIL','Check-engine lamp',88,0x01,'On','Off',True),
    ('I_SYNC','Blocked: no crank sync',90,0x01,'Yes','No',True),
    ('I_CAL','Blocked: no valid tune',90,0x02,'Yes','No',True),
    ('I_SENSOR','Blocked: sensor',90,0x04,'Yes','No',True),
    ('I_STALE','Blocked: stale data',90,0x08,'Yes','No',True),
    ('I_SERVICE','Blocked: reset ECU after save',90,0x10,'Yes','No',True),
    ('I_DEADLINE','Blocked: timing deadline',90,0x20,'Yes','No',True),
    ('I_POWER','Blocked: power/key',90,0x40,'Yes','No',True),
    ('I_BOARD','Blocked: outputs not released',90,0x80,'Yes','No',True),
    ('I_OUTPUT','Blocked: output fault',89,0x01,'Yes','No',True),
]
NARROWBAND_FLAGS=[
    ('NB_LEAN','Narrowband: lean',93,0x01,'Lean','Off',False),
    ('NB_STOICH','Narrowband: stoich',93,0x02,'Stoich','Off',False),
    ('NB_RICH','Narrowband: rich',93,0x04,'Rich','Off',False),
]


def make():
    root=ET.Element('ADXFORMAT',version='1.01')
    header=ET.SubElement(root,'ADXHEADER')
    def text(parent,tag,value): ET.SubElement(parent,tag).text=str(value)
    text(header,'guid','dc3a23b4-7faa-4217-bf46-619cde442154')
    text(header,'flags','0x10000')
    count=ET.SubElement(header,'objectcount')
    text(header,'userversion','7.1');text(header,'author','TU744 project')
    text(header,'desc','TU744 0.0.1, schema 4, monitor extension 3. Requires TU744.dll 0.5 or later '
         'and matching firmware; the plugin validates framing and checksums. '
         'Lambda displays use a fixed 14.7 AFR reference; wideband readings require '
         'a configured, warmed-up wideband controller.')
    text(header,'baud',19200)
    ET.SubElement(header,'DEPENDSONPLUGIN',guid='265a1534-e71f-4b6f-8932-71936121d003',
                  filename='TU744.dll',desc='TU744 schema-4 logging, monitor extension 3',minvermajor='0',minverminor='5')
    ET.SubElement(header,'DEFAULTS',datasizeinbits='8',sigdigits='2',outputtype='3',
                  baud='0',signed='0',lsbfirst='0',float='0')
    text(header,'monitorcmd','POLL')
    def item(kind,name,title):
        return ET.SubElement(root,kind,id=name,idhash=hex(identifier_hash(name)),title=title)
    # One command per cycle: extension 2 carries inhibits and DTC counts, so
    # status (25) and private faults (2B) no longer slow the logging rate.
    macro=item('ADXMACRO','POLL','Live engine data');macro.set('repeatcount','1')
    for suffix in ['_SEND','_REPLY']:
        ET.SubElement(macro,'NODE',commandID='MONITOR'+suffix,repeatcount='1')
    send=item('ADXCSENDCOMMAND','MONITOR_SEND','MONITOR request')
    # No ADX checksum/echo processing: the plugin handles physical framing.
    ET.SubElement(send,'bytestring',size='0x4').text='AA011011'
    # Bit 0 publishes packet data/rate; bit 2 allows paired send/listen.
    # With only bit 2 the host accepts replies but leaves values/rate at 0.
    receive=item('ADXCLISTENPACKET','MONITOR_REPLY','MONITOR response');receive.set('flags','0x00000005')
    text(receive,'listentimeout',2500);text(receive,'packetbodylength',98+3)
    text(receive,'packetoffsetinbody',2);text(receive,'packetsize',98)
    parent=receive.get('idhash')
    engine=[]
    for name,title,at,bits,unit,low,high,equation,decimals,signed in VALUES:
        n=item('ADXVALUE',name,title)
        if signed:text(n,'flags','0x00000001')
        text(n,'parentcmdidhash',parent);text(n,'units',unit)
        text(n,'packetoffset',hex(at));text(n,'sizeinbits',bits)
        ET.SubElement(n,'range',low=str(low),high=str(high))
        ET.SubElement(n,'alarms',low=str(low),high=str(high))
        text(n,'digcount',decimals);text(n,'outputtype',3)
        ET.SubElement(ET.SubElement(n,'MATH',equation=equation),'VAR',varID='X',type='native')
        engine.append(n.get('idhash'))
    status=[]
    for name,title,at,mask,true,false,alarm in NARROWBAND_FLAGS+FLAGS:
        n=item('ADXBITMASK',name,title)
        text(n,'parentcmdidhash',parent)
        text(n,'truestring',true);text(n,'falsestring',false)
        ET.SubElement(n,'ALARMONSET',color='0x000000FF' if alarm else '0x0000FF00')
        ET.SubElement(n,'ALARMONNOTSET',color='0x0000FF00')
        text(n,'packetoffset',f'0x{at:02X}');text(n,'operand',f'0x{mask:08X}')
        text(n,'bitop','AND');text(n,'result',f'0x{mask:08X}')
        (engine if name.startswith('NB_') else status).append(n.get('idhash'))
    tps_refs=[hex(identifier_hash(n)) for n in ['RPM','TPS_ADC','TPS']]
    for name,title,refs in [('ENGINE','Engine',engine),('STATUS','Status and blocks',status),
                            ('TPS_CAL','TPS calibration - engine stopped',tps_refs)]:
        view=item('ADXLISTVIEW',name,title)
        text(view,'entrycount',len(refs))
        for ref in refs:ET.SubElement(view,'ADXLVENTRY',entrytype='0',itemidhash=ref)
    count.text=str(len(root)-1)
    ET.indent(root,space='  ')
    return ET.ElementTree(root)


if __name__=='__main__':
    path=ROOT/'tunerpro/TU744_schema4.adx'
    make().write(path,encoding='utf-8',xml_declaration=True)
    print(path)
