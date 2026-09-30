"""Generate standalone schema-5 XDFs for 3072-byte calibration files.

The two definitions share storage, but display the selected fueling load axis.
No live transport or flash programming is implied by an XDF.

Every item states its offset and the range the firmware accepts, so the XDF
can be read without the source. Units follow the firmware code that consumes
each field (src/*.c, src/calibration.c validation).
"""
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

# TunerPro numbers CATEGORY from 0 and refers to it from CATEGORYMEM as
# index+1 (see any TunerPro-authored XDF), so names are listed in order here.
CATEGORIES = [
    'Fuel', 'Fuel - start and warm-up', 'Fuel - acceleration', 'Injectors', 'Ignition',
    'Rev limit and fuel cut', 'Launch and anti-lag', 'Idle', 'Closed loop (STFT)',
    'Oxygen sensor and wideband', 'Sensors', 'Fan, pump, boost and gauge',
    'Engine state', 'Vehicle speed and gear', 'Feature switches', 'Axis breakpoints',
    'DTC switches', 'DTC switches - individual codes', 'DTC thresholds', 'TPS calibration', 'Knock',
]
CAT = {name: i + 1 for i, name in enumerate(CATEGORIES)}

# Events with a running standalone producer, and the descriptor subtypes that
# producer can raise (src/diagnostic_monitors.c). Other events have no
# producer, so their enable bits do nothing and are not offered as switches.
# Codes and names come from the ROM report table; see OEM-DTC-EVENT-MATRIX.md.
DTC_EVENTS = [
    # event, subtypes raised, name, detail
    (0x03, (1,), 'Misfire, multiple cylinders - level 1',
     'Crank interval deviation above "Misfire crank-window deviation".'),
    (0x08, (1,), 'Misfire, multiple cylinders - level 2',
     'Crank interval deviation above twice "Misfire crank-window deviation".'),
    (0x04, (1,), 'Misfire cylinder 1 - level 1', 'Needs established DEPHIA polarity.'),
    (0x09, (1,), 'Misfire cylinder 1 - level 2', 'Needs established DEPHIA polarity.'),
    (0x07, (1,), 'Misfire cylinder 2 - level 1', 'Needs established DEPHIA polarity.'),
    (0x0C, (1,), 'Misfire cylinder 2 - level 2', 'Needs established DEPHIA polarity.'),
    (0x05, (1,), 'Misfire cylinder 3 - level 1', 'Needs established DEPHIA polarity.'),
    (0x0A, (1,), 'Misfire cylinder 3 - level 2', 'Needs established DEPHIA polarity.'),
    (0x06, (1,), 'Misfire cylinder 4 - level 1', 'Needs established DEPHIA polarity.'),
    (0x0B, (1,), 'Misfire cylinder 4 - level 2', 'Needs established DEPHIA polarity.'),
    (0x0D, (4,), 'Crankshaft position sensor circuit',
     'Loss of an acquired 60-2 pattern.'),
    (0x4A, (8,), 'Crankshaft position range/performance',
     'Loss of an acquired 60-2 pattern (second OEM event).'),
    (0x1A, (1,), 'Cylinder phase (DEPHIA) - no capture',
     'Armed CC8 phase capture missing or timed out.'),
    (0x4B, (1,), 'Cylinder phase (DEPHIA) - implausible delay',
     'Captured delay outside the DEPHIA minimum/maximum.'),
    (0x1B, (1, 2, 4, 8), 'Throttle position sensor', ''),
    (0x43, (1, 2, 4, 8), 'MAP sensor', ''),
    (0x5B, (1, 2, 4, 8), 'Intake air temperature sensor', ''),
    (0x61, (1, 2, 4, 8), 'Coolant temperature sensor', ''),
    (0x65, (1, 2, 4, 8), 'System voltage', ''),
    (0x68, (1, 2, 8), 'Vehicle speed sensor', ''),
    (0x27, (1, 2), 'Fuel trim at its lean/rich limit',
     'STFT held at its limit for "Fuel-trim limit time".'),
    (0x2D, (1,), 'Upstream O2 sensor circuit (narrowband only)',
     'No switching during the activity window, or invalid signal.'),
    (0x45, (1, 2, 4, 8), 'Upstream O2 sensor signal (narrowband only)', ''),
    (0x3F, (1,), 'Upstream O2 slow response, lean to rich (narrowband only)', ''),
    (0x40, (1,), 'Upstream O2 slow response, rich to lean (narrowband only)', ''),
    (0x4D, (4, 8), 'Idle stepper position supervisor', ''),
    (0x56, (1, 2, 4, 8), 'Idle stepper driver (L9935)', ''),
    (0x50, (1,), 'Processor self-test', 'RAM pattern, arithmetic and control-flow test.'),
]

# Standalone meaning of individual subtypes, where it differs per code.
SENSOR_SUBTYPE = {1: 'out of range, high side', 2: 'out of range, low side',
                  4: 'no conversion, stale or misconfigured', 8: 'changes faster than its slew limit'}
SUBTYPE_DETAIL = {
    **{(e, s): d for e in (0x1B, 0x43, 0x5B, 0x61, 0x65) for s, d in SENSOR_SUBTYPE.items()},
    (0x68, 1): 'speed above "VSS maximum plausible speed" or a jump above 80 km/h',
    (0x68, 2): 'pulses lost after the vehicle was moving',
    (0x68, 8): 'implausible change between samples',
    (0x27, 1): 'trim held at its positive (lean) limit',
    (0x27, 2): 'trim held at its negative (rich) limit',
    (0x45, 1): 'signal at the upper rail', (0x45, 2): 'signal at the lower rail',
    (0x45, 4): 'no switching during the activity window', (0x45, 8): 'stale signal',
    (0x4D, 4): 'homing did not finish in time',
    (0x4D, 8): 'stepper bridges could not be switched off',
    (0x56, 1): 'no SPI response from the driver', (0x56, 2): 'repeated open-load response',
    (0x56, 4): 'driver diagnostic code 10b', (0x56, 8): 'driver diagnostic code 00b',
}


def dtc_codes():
    """Event -> {subtype: (code, description)} from the ROM report table."""
    import csv
    table = {}
    path = ROOT / 'tunerpro/dtc_events.csv'
    for row in csv.DictReader(path.open()):
        table[int(row['event'], 16)] = {
            s: (row[f'subtype_{s}_code'], row[f'subtype_{s}_description']) for s in (1, 2, 4, 8)}
    return table


def text(parent, tag, value):
    ET.SubElement(parent, tag).text = str(value)


def data(parent, at, bits, rows=1, columns=1, signed=False):
    assert 0 <= at < at + bits // 8 * rows * columns <= 3072
    return ET.SubElement(parent, 'EMBEDDEDDATA', mmedtypeflags='0x01' if signed else '0x00',
                         mmedaddress=hex(at), mmedelementsizebits=str(bits),
                         mmedrowcount=str(rows), mmedcolcount=str(columns),
                         mmedmajorstridebits='0', mmedminorstridebits='0')


def math(parent, equation):
    ET.SubElement(ET.SubElement(parent, 'MATH', equation=equation), 'VAR', id='X')


def scale(parent, units, equation='X', decimals=0):
    text(parent, 'units', units)
    text(parent, 'decimalpl', decimals)
    text(parent, 'outputtype', 1 if decimals else 2)
    math(parent, equation)


def axis(parent, name, spec):
    """spec: (offset or None, count, bits, units, signed[, labels])."""
    at, count, bits, units, signed = spec[:5]
    node = ET.SubElement(parent, 'XDFAXIS', id=name, uniqueid='0x0')
    if at is None:
        ET.SubElement(node, 'EMBEDDEDDATA', mmedelementsizebits='8',
                      mmedmajorstridebits='-32', mmedminorstridebits='0')
        labels = spec[5] if len(spec) > 5 else [''] * count
        for i, label in enumerate(labels):
            ET.SubElement(node, 'LABEL', index=str(i), value=str(label))
        text(node, 'outputtype', 4)
    else:
        data(node, at, bits, columns=count, signed=signed)
        ET.SubElement(node, 'embedinfo', type='1')
        text(node, 'outputtype', 2)
    text(node, 'indexcount', count)
    text(node, 'units', units)
    text(node, 'decimalpl', 0)
    text(node, 'datatype', 0)
    text(node, 'unittype', 0)
    ET.SubElement(node, 'DALINK', index='0')
    math(node, 'X')


def generate(alpha_n=False):
    root = ET.Element('XDFFORMAT', version='1.70')
    header = ET.SubElement(root, 'XDFHEADER')
    text(header, 'flags', '0x1')
    text(header, 'fileversion', '5.1')
    mode = 'Alpha-N' if alpha_n else 'Speed-density'
    text(header, 'deftitle', f'TU744 schema 5 - {mode}')
    text(header, 'description', 'For 3072-byte schema-5 standalone calibration files only. '
         'Not an OEM ROM or full firmware image. Confirm LR 00 05 at 0x900 and the selected '
         'load mode. XDF does not select the ECU fueling mode (see Feature switches). '
         'Each description gives the offset and the range the ECU accepts; an update outside '
         'it is rejected and the previous tune stays active. See docs/FUELING.md and tunerpro/README.md.')
    text(header, 'author', 'TU744 project')
    ET.SubElement(header, 'BASEOFFSET', offset='0', subtract='0')
    ET.SubElement(header, 'DEFAULTS', datasizeinbits='8', sigdigits='2', outputtype='1',
                  signed='0', lsbfirst='0', float='0')
    ET.SubElement(header, 'REGION', type='0xFFFFFFFF', startaddress='0x0', size='0xC00',
                  regionflags='0x0', name='Schema-5 calibration', desc='3072 bytes, big endian')
    for i, label in enumerate(CATEGORIES):
        ET.SubElement(header, 'CATEGORY', index=hex(i), name=label)
    uid = 0x4000

    def item(kind, title, description, categories):
        nonlocal uid
        node = ET.SubElement(root, kind, uniqueid=hex(uid), flags='0x0')
        uid += 1
        text(node, 'title', title)
        text(node, 'description', description)
        if isinstance(categories, str):
            categories = [categories]
        for i, name in enumerate(categories):
            ET.SubElement(node, 'CATEGORYMEM', index=str(i), category=str(CAT[name]))
        return node

    def note(description, at, valid):
        parts = [description.strip()] if description else []
        parts.append(f'Offset 0x{at:03X}.')
        if valid:
            parts.append(f'ECU accepts {valid}.')
        return ' '.join(parts)

    blank = (None, 1, 8, '', False)
    rpm = (0x400, 16, 16, 'RPM', False)
    load = (0x440 if alpha_n else 0x420, 16, 16, 'TPS %' if alpha_n else 'kPa', False)
    temp = (0x460, 16, 16, 'C', True)
    voltage = (None, 8, 8, 'V', False, [4 + 2 * i for i in range(8)])

    def table(title, at, bits, units, x, y=blank, equation='X', decimals=0,
              low=0, high=255, category='Fuel', description='', valid=''):
        node = item('XDFTABLE', title, note(description, at, valid), category)
        axis(node, 'x', x); axis(node, 'y', y)
        z = ET.SubElement(node, 'XDFAXIS', id='z')
        data(z, at, bits, rows=y[1], columns=x[1])
        text(z, 'min', low); text(z, 'max', high)
        scale(z, units, equation, decimals)

    def scalar(title, at, bits, units, low, high, equation='X', decimals=0,
               category='Fuel', signed=False, description='', valid=None):
        node = item('XDFCONSTANT', title,
                    note(description, at, f'{low}..{high} {units}' if valid is None else valid),
                    category)
        data(node, at, bits, signed=signed)
        text(node, 'min', low); text(node, 'max', high)
        scale(node, units, equation, decimals)

    def flag(title, at, mask, category, description=''):
        node = item('XDFFLAG', title, note(description, at, '') + f' Bit mask 0x{mask:02X}.',
                    category)
        data(node, at, 8)
        text(node, 'mask', hex(mask))

    def breakpoints(title, at, count, bits, units, signed, low, high, users):
        node = item('XDFTABLE', title,
                    note(f'Strictly increasing. Used by: {users}.', at, f'{low}..{high} {units}'),
                    'Axis breakpoints')
        axis(node, 'x', (None, count, 8, 'point', False, list(range(1, count + 1))))
        axis(node, 'y', blank)
        z = ET.SubElement(node, 'XDFAXIS', id='z')
        data(z, at, bits, columns=count, signed=signed)
        text(z, 'min', low); text(z, 'max', high)
        scale(z, units)

    load_name = 'TPS' if alpha_n else 'MAP'
    # ---- Fuel
    table('Running VE', 0, 8, 'VE %', rpm, load,
          description=f'Volumetric efficiency for the air-charge fuel calculation. Columns: RPM; rows: {load_name}.')
    table('Target AFR', 0x200, 8, 'AFR', rpm, load, 'X/10', 1, 7, 22,
          description='Closed-loop target, and fuel scaling when "Use target AFR in fuel calculation" is on.',
          valid='7.0..22.0 AFR')
    flag('Use target AFR in fuel calculation', 0x8D0, 0x01, 'Fuel',
         'On: fuel is scaled by stoichiometric AFR / target AFR. Off: target AFR only feeds closed loop.')
    scalar('Stoichiometric AFR', 0x912, 16, 'AFR', 7, 22, 'X/10', 1, valid='7.0..22.0 AFR')
    flag('Alpha-N fueling (TPS load axis)', 0x5D4, 0x01, ['Fuel', 'Feature switches'],
         'On: TPS is the load axis and MAP is not used for fuel. Must match the XDF you opened. '
         'Stopped engine only.')
    # ---- Start and warm-up
    table('Cranking VE', 0x490, 16, 'VE %', temp, high=1000, category='Fuel - start and warm-up',
          description='Replaces running VE while cranking; not a pulsewidth. Includes MAP in speed-density mode.',
          valid='0..1000 %')
    table('Warm-up multiplier', 0x480, 8, '%', temp, category='Fuel - start and warm-up',
          description='100% is neutral. Running fuel only.')
    table('After-start multiplier', 0x4B0, 8, '%', temp, low=100, category='Fuel - start and warm-up',
          description='100% is neutral. Starts at this value and decays linearly to 100% over the after-start duration.',
          valid='100..255 %')
    table('After-start duration', 0x4C0, 8, 's', temp, equation='X/10', decimals=1, high=25.5,
          category='Fuel - start and warm-up')
    # ---- Acceleration
    table('Acceleration multiplier', 0x759, 8, '%', (0x740, 8, 16, 'TPS %/s', False),
          (0x750, 6, 8, 'Previous TPS %', False), low=100, category='Fuel - acceleration',
          description='100% is neutral. Columns: TPS rate; rows: throttle position before the change.',
          valid='100..255 %')
    table('Acceleration RPM modifier', 0x799, 8, '%', (0x789, 8, 16, 'RPM', False),
          category='Fuel - acceleration',
          description='Scales only the enrichment above 100%. 100% keeps it all, 0% disables it at this RPM.')
    scalar('Acceleration TPS-rate threshold', 0x756, 8, '%/s', 0, 255, category='Fuel - acceleration',
           description='Enrichment starts when the TPS rate exceeds this.')
    scalar('Acceleration qualification time', 0x758, 8, 'ms', 0, 490, '(X-1)*10',
           category='Fuel - acceleration', description='TPS rate must stay above the threshold this long.',
           valid='0..490 ms')
    scalar('Acceleration decay', 0x90A, 16, 'ms', 10, 5000, category='Fuel - acceleration')
    # ---- Injectors
    scalar('Required fuel per 720 degrees', 0x5DF, 16, 'us', 1, 20000, category='Injectors',
           description='Pulsewidth at 100% VE, 100 kPa (speed-density) and 0 C inlet air, per engine cycle. '
                       'Paired injection splits it into two events.')
    table('Injector dead time', 0x540, 16, 'us', voltage, high=5000, category='Injectors',
          description='Added after all fuel multipliers; not added when fuel is zero. Columns: battery volts.')
    scalar('Maximum injector duty', 0x8CF, 8, '%', 10, 95, category='Injectors')
    scalar('Injection phase', 0x918, 16, 'deg', 0, 162, 'X/10', 1, 'Injectors',
           description='Only multiples of 6 degrees are accepted. The paired event is 180 degrees later.')
    # ---- Ignition
    table('Ignition advance', 0x100, 8, 'deg BTDC', rpm, load, 'X*0.5-20', 1, -20, 50, 'Ignition',
          description=f'Columns: RPM; rows: {load_name}.', valid='-20..50 deg')
    table('Ignition dwell', 0x530, 16, 'us', voltage, low=500, high=6000, category='Ignition',
          description='Coil charge time. Columns: battery volts.')
    table('IAT ignition retard', 0x510, 8, 'deg', temp, equation='X/2', decimals=1, high=30,
          category='Ignition', description='Subtracted from advance. Columns: inlet air temperature.')
    scalar('Cranking advance', 0x91A, 16, 'deg BTDC', -10, 20, 'X/10', 1, 'Ignition', True)
    scalar('Trigger reference offset', 0x605, 16, 'deg', 0, 359.9, 'X/10', 1, 'Ignition',
           description='Crank angle between the trigger reference and TDC cylinder 1. Verify with a timing light.')
    flag('Use CC9 dwell feedback', 0x93A, 0x01, 'Ignition',
         'Enable only with a qualified CC9 input. Missing or invalid feedback falls back to calibrated dwell. '
         'Stopped engine only.')
    # ---- Rev limit and fuel cut
    scalar('Hard rev limit', 0x5DB, 16, 'RPM', 1500, 10000, category='Rev limit and fuel cut',
           description='Cut starts here and holds until RPM falls to the recovery RPM.')
    scalar('Rev-limit recovery', 0x5DD, 16, 'RPM', 1000, 9999, category='Rev limit and fuel cut',
           description='Must be below the hard rev limit.')
    flag('Rev limiter cuts fuel only', 0x7B4, 0x02, 'Rev limit and fuel cut',
         'Leave both "cuts ... only" switches off to cut fuel and spark. Also applies to the soft cut.')
    flag('Rev limiter cuts spark only', 0x7B4, 0x04, 'Rev limit and fuel cut',
         'Leave both "cuts ... only" switches off to cut fuel and spark. Also applies to the soft cut.')
    flag('Soft rev cut', 0x7B4, 0x01, 'Rev limit and fuel cut',
         'Progressive cut from the soft-cut start RPM up to the hard rev limit.')
    scalar('Soft-cut start RPM', 0x7B5, 16, 'RPM', 0, 9999, category='Rev limit and fuel cut',
           description='Must be below the hard rev limit.')
    scalar('Soft-cut maximum', 0x7B7, 8, '%', 0, 100, category='Rev limit and fuel cut',
           description='Cut fraction reached at the hard rev limit; rises linearly from 0 at the start RPM.')
    flag('Overrun fuel cut', 0x5D4, 0x02, ['Rev limit and fuel cut', 'Feature switches'],
         'Cuts fuel on closed throttle above the entry RPM and idle target plus exit margin, with valid TPS and coolant at least 60 C. No MAP gate.')
    scalar('Overrun entry RPM', 0x5E9, 16, 'RPM', 0, 10000, category='Rev limit and fuel cut')
    scalar('Overrun exit margin', 0x93E, 16, 'RPM', 0, 2000, category='Rev limit and fuel cut',
           description='Resume fuel at idle target plus this margin. 0 keeps the legacy 200 RPM margin. Firmware 0.0.2 or later; live tunable.')
    scalar('Overrun delay', 0x603, 8, 's', 0, 25.5, 'X/10', 1, 'Rev limit and fuel cut',
           description='Continuous eligibility before fuel cut. New basemap default 0.1 s; 0 means immediate.')
    scalar('Overboost cut MAP', 0x90E, 16, 'kPa', 100, 600, category=['Rev limit and fuel cut'],
           description='Fuel and spark are cut above this MAP. Boost control also stops here.')
    # ---- Launch and anti-lag
    flag('Launch control', 0x5D4, 0x04, ['Launch and anti-lag', 'Feature switches'],
         'Launch must still be armed from the tool each time. Stopped engine only.')
    scalar('Launch RPM limit', 0x5E7, 16, 'RPM', 1500, 10000, category='Launch and anti-lag',
           description='Hard launch limit. Must not exceed the hard rev limit.')
    scalar('Launch maximum speed', 0x7AF, 8, 'km/h', 1, 255, category='Launch and anti-lag',
           description='Moving launch is only permitted below this speed.')
    scalar('Launch minimum TPS', 0x8C5, 8, '%', 0, 100, category='Launch and anti-lag')
    scalar('Launch arm time', 0x930, 16, 'ms', 0, 30000, category='Launch and anti-lag',
           description='An arm expires after this. 0 disables arming.', valid='0 or 1000..30000 ms')
    flag('Launch cuts fuel only', 0x7B4, 0x10, 'Launch and anti-lag',
         'Leave both launch "cuts ... only" switches off to cut fuel and spark.')
    flag('Launch cuts spark only', 0x7B4, 0x20, 'Launch and anti-lag',
         'Leave both launch "cuts ... only" switches off to cut fuel and spark.')
    flag('Soft launch', 0x7B4, 0x08, 'Launch and anti-lag',
         'Progressive cut from the soft-launch start RPM up to the launch RPM limit.')
    scalar('Soft-launch start RPM', 0x932, 16, 'RPM', 0, 9999, category='Launch and anti-lag',
           valid='0 or 1000 RPM up to below the launch RPM limit')
    scalar('Soft-launch maximum cut', 0x934, 8, '%', 0, 100, category='Launch and anti-lag')
    flag('Anti-lag', 0x7B4, 0x40, 'Launch and anti-lag',
         'One-shot per arm, while launch is active. Requires launch control and anti-lag settings below.')
    flag('Anti-lag cuts spark instead of fuel', 0x7B4, 0x80, 'Launch and anti-lag',
         'Converts launch cuts to spark-only during anti-lag. Requires Anti-lag.')
    scalar('Anti-lag maximum time', 0x936, 16, 'ms', 0, 3000, category='Launch and anti-lag',
           valid='100..3000 ms when Anti-lag is on')
    scalar('Anti-lag maximum coolant', 0x938, 8, 'C', 0, 110, category='Launch and anti-lag',
           valid='60..110 C when Anti-lag is on')
    scalar('Anti-lag maximum intake air', 0x939, 8, 'C', 0, 80, category='Launch and anti-lag',
           valid='0..80 C when Anti-lag is on')
    scalar('Anti-lag ignition advance', 0x7B8, 8, 'deg BTDC', -20, 50, 'X*0.5-20', 1,
           'Launch and anti-lag')
    scalar('Anti-lag VE', 0x7B9, 8, 'VE %', 0, 255, category='Launch and anti-lag',
           description='Replaces running VE during anti-lag.', valid='1..255 % when Anti-lag is on')
    # ---- Idle
    scalar('Idle control mode', 0x5D8, 8, 'mode', 0, 2, category='Idle',
           description='0 = fixed steps at the idle step interval, 1 = PI stepper, 2 = spark only.')
    table('Idle target', 0x4D0, 16, 'RPM', temp, low=500, high=2500, category='Idle')
    table('Idle base position', 0x4F0, 8, 'steps', temp, high=220, category='Idle',
          valid='0 up to IAC maximum position')
    table('Idle start addition', 0x500, 8, 'steps', temp, high=220, category='Idle',
          description='Added while cranking, then decays over the idle start duration.',
          valid='0 up to IAC maximum position')
    table('Idle spark correction', 0x620, 8, 'deg', (0x610, 8, 16, 'RPM error', True),
          equation='X/2-20', decimals=1, low=-20, high=20, category='Idle',
          description='Columns: idle target minus actual RPM.')
    scalar('Idle start duration', 0x5EE, 8, 's', 0, 25.5, 'X/10', 1, 'Idle')
    scalar('Closed throttle threshold', 0x5E6, 8, '%', 0, 20, category=['Idle', 'Rev limit and fuel cut'],
           description='Shared idle and overrun threshold, inclusive and without debounce. Invalid TPS disables both.')
    scalar('Idle RPM deadband', 0x604, 8, 'RPM', 0, 255, category='Idle')
    scalar('Idle proportional gain', 0x630, 16, 'steps/RPM', 0, 1, 'X/10000', 4, 'Idle')
    scalar('Idle integral gain', 0x632, 16, 'steps/(RPM*s)', 0, .1, 'X/100000', 5, 'Idle')
    scalar('Idle step interval', 0x924, 16, 'ms', 20, 1000, category='Idle',
           description='Used by idle control mode 0.')
    scalar('IAC maximum position', 0x910, 16, 'steps', 1, 220, category='Idle')
    scalar('IAC home timeout', 0x91C, 16, 'ms', 3000, 10000, category='Idle')
    scalar('IAC homing steps', 0x93C, 16, 'steps', 0, 800, category='Idle',
           description='Closing steps driven at key-on to find the end stop; that point becomes position 0. '
                       '0 = 250 steps. Steps x 12 ms must fit in the IAC home timeout. Stopped engine only.',
           valid='0, or from IAC maximum position up to 800 steps')
    scalar('Fan idle feedforward', 0x8C0, 8, 'steps', 0, 255, category='Idle',
           description='Extra IAC steps while the fan runs. 0 disables the fan preload.')
    # ---- Closed loop
    flag('Short-term fuel trim', 0x5D4, 0x20, ['Closed loop (STFT)', 'Feature switches'],
         'Closed-loop correction using the oxygen sensor.')
    scalar('STFT maximum (lean correction)', 0x731, 8, '%', 100, 150, 'X/1.28', 1,
           'Closed loop (STFT)', valid='100.0..150.0 %')
    scalar('STFT minimum (rich correction)', 0x732, 8, '%', 50, 100, 'X/1.28', 1,
           'Closed loop (STFT)', valid='50.0..100.0 %')
    scalar('STFT update interval', 0x730, 8, 'ms', 10, 2550, 'X*10', category='Closed loop (STFT)')
    scalar('STFT delay after start', 0x8C7, 8, 's', 0, 255, category='Closed loop (STFT)')
    scalar('STFT minimum RPM', 0x7BA, 16, 'RPM', 0, 12000, category='Closed loop (STFT)')
    scalar('STFT maximum RPM', 0x7BC, 16, 'RPM', 0, 12000, category='Closed loop (STFT)')
    scalar('STFT minimum coolant', 0x7BE, 8, 'C', -40, 127, category='Closed loop (STFT)', signed=True)
    scalar('STFT maximum TPS', 0x7BF, 8, '%', 0, 100, category='Closed loop (STFT)',
           description='0 means no TPS limit.')
    scalar('Narrowband trim step', 0x8C2, 8, '%', 0.78, 12.5, 'X/1.28', 2, 'Closed loop (STFT)',
           description='Correction per update, towards the opposite side of the switching point.')
    scalar('Narrowband lean threshold', 0x8C3, 8, 'mV', 0, 995, 'X*5', category='Closed loop (STFT)',
           description='Below this the mixture is lean. Must be below the rich threshold.')
    scalar('Narrowband rich threshold', 0x8C4, 8, 'mV', 5, 1000, 'X*5', category='Closed loop (STFT)',
           description='Above this the mixture is rich.')
    scalar('Narrowband minimum target AFR', 0x8C8, 8, 'AFR', 0, 25.5, 'X/10', 1, 'Closed loop (STFT)',
           description='Narrowband trim only runs while target AFR is inside this window.')
    scalar('Narrowband maximum target AFR', 0x8C9, 8, 'AFR', 0, 25.5, 'X/10', 1, 'Closed loop (STFT)')
    scalar('Wideband trim gain', 0x90C, 16, '1/s', 0, 2, 'X/1000', 3, 'Closed loop (STFT)',
           description='Trim change per second for a 100% relative AFR error.')
    scalar('Wideband AFR deadband', 0x8C1, 8, 'AFR', 0, 25.5, 'X/10', 1, 'Closed loop (STFT)')
    scalar('Wideband maximum trim step', 0x8CD, 8, '%', 0, 199, 'X/1.28', 2, 'Closed loop (STFT)',
           description='Largest change per update. 0 means unlimited.')
    # ---- Oxygen sensor and wideband
    flag('Wideband oxygen sensor', 0x600, 0x01, 'Oxygen sensor and wideband',
         'Off: narrowband on AN6. On: 0-5 V wideband controller output on AN6; requires '
         '"Upstream heater relay fitted".')
    scalar('Wideband AFR at 0 V', 0x601, 8, 'AFR', 5, 24.9, 'X/10', 1, 'Oxygen sensor and wideband')
    scalar('Wideband AFR at 5 V', 0x602, 8, 'AFR', 5.1, 25, 'X/10', 1, 'Oxygen sensor and wideband')
    flag('Wideband signal qualification', 0x926, 0x01, 'Oxygen sensor and wideband',
         'Trim uses the wideband only after warm-up and continuous in-range samples.')
    scalar('Wideband warm-up time', 0x928, 16, 'ms', 0, 60000, category='Oxygen sensor and wideband',
           valid='10000..60000 ms when qualification is on')
    scalar('Wideband good-signal time', 0x92A, 16, 'ms', 0, 10000, category='Oxygen sensor and wideband',
           valid='500..10000 ms when qualification is on')
    scalar('Wideband minimum valid signal', 0x92C, 16, 'mV', 0, 4989, category='Oxygen sensor and wideband',
           valid='10 mV up to below the maximum, when qualification is on')
    scalar('Wideband maximum valid signal', 0x92E, 16, 'mV', 0, 4990, category='Oxygen sensor and wideband',
           valid='up to 4990 mV, above the minimum, when qualification is on')
    flag('Upstream heater relay fitted', 0x916, 0x01, 'Oxygen sensor and wideband',
         'The upstream heater output powers the heater (or wideband controller).')
    flag('Downstream heater relay fitted', 0x916, 0x02, 'Oxygen sensor and wideband')
    # ---- Sensors
    scalar('TPS closed ADC', 0x7B0, 16, 'ADC counts', 0, 923, category=['TPS calibration', 'Sensors'],
           description='Ignition on, engine stopped: release the pedal and use Capture closed in the TU744 controls panel. '
           'Raw counts are also shown in the ADX TPS calibration list. Apply both endpoints together, then Download BIN from Emulator. '
           'Save the ECU tune to flash to retain calibration after a key cycle.')
    scalar('TPS open ADC', 0x7B2, 16, 'ADC counts', 100, 1023, category=['TPS calibration', 'Sensors'],
           description='Ignition on, engine stopped: fully press the pedal and use Capture open in the TU744 controls panel. '
           'Release the pedal before Apply TPS. Open must be at least 100 counts above closed. '
           'Manual editing uses the raw ADC value, not TPS percent; upload both endpoints in one complete BIN if an intermediate pair is invalid.')
    table('MAP sensor transfer', 0x594, 16, 'kPa',
          (None, 16, 8, 'ADC', False, [round(i * 1023 / 15) for i in range(16)]), high=600,
          category='Sensors', description='Pressure at evenly spaced ADC counts. Must not decrease.',
          valid='0..600 kPa')
    scalar('Coolant thermistor pull-up', 0x550, 16, 'ohm', 1, 20000, category='Sensors')
    table('Coolant thermistor resistance', 0x552, 16, 'ohm', temp, low=1, high=65535, category='Sensors',
          description='Must decrease as temperature rises. Stopped engine only.')
    scalar('Intake thermistor pull-up', 0x572, 16, 'ohm', 1, 20000, category='Sensors')
    table('Intake thermistor resistance', 0x574, 16, 'ohm', temp, low=1, high=65535, category='Sensors',
          description='Must decrease as temperature rises. Stopped engine only.')
    for i, name in enumerate(['TPS', 'MAP', 'Coolant', 'Intake air', 'Oxygen sensor', 'Battery']):
        scalar(f'{name} filter', 0x628 + i, 8, '%', 0, 93.75, 'X/2.56', 1, 'Sensors',
               description='Weight kept from the previous filtered value on each new sample. 0 = unfiltered.',
               valid='0..93.8 %')
    scalar('Sensor freshness limit', 0x920, 16, 'ms', 20, 100, category='Sensors',
           description='An ADC sample older than this is flagged stale.')
    scalar('Minimum battery voltage', 0x91E, 16, 'V', 5, 12, 'X/1000', 2, 'Sensors',
           description='Below this the battery reading is out of range.')
    # ---- Fan, pump, boost, gauge
    scalar('Fan on temperature', 0x5E2, 8, 'C', 70, 120, category='Fan, pump, boost and gauge', signed=True)
    scalar('Fan off temperature', 0x5E3, 8, 'C', -40, 119, category='Fan, pump, boost and gauge',
           signed=True, description='Must be below the fan-on temperature.')
    scalar('Fuel pump prime', 0x5EC, 8, 's', 0, 10, 'X/10', 1, 'Fan, pump, boost and gauge')
    flag('Boost control', 0x5D4, 0x10, ['Fan, pump, boost and gauge', 'Feature switches'])
    table('Boost duty', 0x300, 8, '%', rpm, (0x440, 16, 16, 'TPS %', False), high=100,
          category='Fan, pump, boost and gauge', description='Columns: RPM; rows: TPS.')
    table('Coolant gauge high duty', 0x5F0, 8, '%', temp, high=100, category='Fan, pump, boost and gauge')
    # ---- Engine state
    scalar('Running threshold', 0x904, 16, 'RPM', 101, 2000, category='Engine state',
           description='Must exceed the cranking re-entry threshold.')
    scalar('Cranking re-entry threshold', 0x906, 16, 'RPM', 100, 1999, category='Engine state')
    scalar('Running qualification', 0x908, 16, 'ms', 100, 5000, category='Engine state')
    scalar('Plan age limit', 0x922, 16, 'ms', 20, 50, category='Engine state',
           description='Outputs are inhibited if the fuel/spark plan is older than this.')
    # ---- Vehicle speed and gear
    scalar('VSS pulses per metre', 0x914, 16, 'pulses/m', 1, 100, category='Vehicle speed and gear')
    scalar('Final drive ratio', 0x7A1, 16, ':1', 0.1, 10, 'X/100', 2, 'Vehicle speed and gear')
    for i in range(5):
        scalar(f'Gear {i + 1} ratio', 0x7A3 + 2 * i, 16, ':1', 0.1, 10, 'X/100', 2,
               'Vehicle speed and gear')
    scalar('Tyre circumference', 0x7AD, 16, 'mm', 500, 4000, category='Vehicle speed and gear')
    # ---- Axis breakpoints
    for args in [
        ('RPM axis', 0x400, 16, 16, 'RPM', False, 0, 12000, 'VE, AFR, ignition, boost'),
        ('MAP axis', 0x420, 16, 16, 'kPa', False, 0, 600, 'speed-density VE, AFR and ignition'),
        ('TPS axis', 0x440, 16, 16, '%', False, 0, 100, 'Alpha-N VE, AFR and ignition; boost'),
        ('Temperature axis', 0x460, 16, 16, 'C', True, -40, 150, 'all temperature tables'),
        ('AE TPS-rate axis', 0x740, 8, 16, '%/s', False, 0, 10000, 'acceleration multiplier'),
        ('AE previous-TPS axis', 0x750, 6, 8, '%', False, 0, 100, 'acceleration multiplier'),
        ('AE RPM axis', 0x789, 8, 16, 'RPM', False, 0, 12000, 'acceleration RPM modifier'),
        ('Idle RPM-error axis', 0x610, 8, 16, 'RPM', True, -3000, 3000, 'idle spark correction')]:
        breakpoints(*args)
    # ---- DTC switches
    codes = dtc_codes()
    for event, subtypes, name, detail in sorted(DTC_EVENTS, key=lambda e: (codes[e[0]][e[1][0]][0], e[0])):
        raised = sorted({codes[event][s][0] for s in subtypes})
        flag(f'{"/".join(raised)} {name}', 0x940 + event // 8, 1 << (event % 8), 'DTC switches',
             f'{detail} Event 0x{event:02X}. On: the monitor can set this DTC. '
             'Off: it is never raised, and an active one is recovered. Stopped engine only.'.strip())
    for event, subtypes, name, _ in DTC_EVENTS:
        if len(subtypes) < 2:
            continue
        for s in subtypes:
            code, description = codes[event][s]
            flag(f'{code} {description} ({SUBTYPE_DETAIL[event, s]})', 0x980 + event, s,
                 'DTC switches - individual codes',
                 f'Event 0x{event:02X}, subtype {s}. Only acts while the "{name}" switch is on. '
                 'Do not switch off every code of one event: the ECU treats an all-off event as all-on. '
                 'Use the event switch in "DTC switches" instead.')
    scalar('DTC fail samples', 0x94E, 8, 'samples', 0, 100, category='DTC thresholds',
           description='Consecutive failing results before a DTC is set. 0 = 3.')
    scalar('DTC pass samples', 0x94F, 8, 'samples', 0, 100, category='DTC thresholds',
           description='Consecutive passing results before a DTC recovers. 0 = 3.')
    scalar('Fuel-trim limit time', 0x950, 16, 'ms', 0, 60000, category='DTC thresholds',
           description='P0171/P0172 after STFT stays at its limit this long. 0 = 10000.',
           valid='0 or 1000..60000 ms')
    scalar('Narrowband activity window', 0x952, 16, 'ms', 0, 30000, category='DTC thresholds',
           description='P0134/P0130 when the narrowband does not switch within this window. 0 = 5000.',
           valid='0 or 1000..30000 ms')
    scalar('Narrowband slow-response time', 0x954, 16, 'ms', 0, 10000, category='DTC thresholds',
           description='P0133 when a lean/rich transition takes longer. 0 = 1500.',
           valid='0 or 100..10000 ms')
    scalar('Misfire crank-window deviation', 0x956, 8, '%', 0, 100, category='DTC thresholds',
           description='Level 1 misfire threshold; level 2 uses twice this. 0 = 35.')
    scalar('Misfire occurrences per 128 observations', 0x957, 8, 'samples', 0, 100,
           category='DTC thresholds', description='0 = 4.')
    scalar('VSS maximum plausible speed', 0x95B, 8, 'km/h', 0, 255, category='DTC thresholds',
           description='0 = 250.')
    for title, at, units, equation, default in [
            ('TPS slew limit', 0x964, '%/s', 'X/10', '3000'),
            ('MAP slew limit', 0x966, 'kPa/s', 'X', '3000'),
            ('Temperature slew limit', 0x968, 'C/s', 'X', '100'),
            ('Battery slew limit', 0x96A, 'mV/s', 'X', '50000')]:
        scalar(title, at, 16, units, 0, 6553.5 if equation != 'X' else 65535, equation,
               1 if equation != 'X' else 0, 'DTC thresholds',
               description=f'Faster change sets the range/performance code. 0 = {default} {units}.')
    scalar('DEPHIA capture timeout', 0x958, 16, 'ms', 0, 10000, category='DTC thresholds',
           description='P1327 when no phase capture arrives within this. 0 = 2000.')
    scalar('DEPHIA minimum delay', 0x95C, 16, 'T1 ticks', 0, 65535, category='DTC thresholds',
           description='0 with maximum and delta also 0 = defaults 66/168/18.')
    scalar('DEPHIA maximum delay', 0x95E, 16, 'T1 ticks', 0, 65535, category='DTC thresholds')
    scalar('DEPHIA direction delta', 0x960, 16, 'T1 ticks', 0, 65535, category='DTC thresholds')
    flag('DEPHIA cylinder-1 polarity inverted', 0x962, 0x01, 'DTC thresholds',
         'Establish by board waveform/cylinder testing before trusting per-cylinder misfire codes.')
    scalar('Output feedback fail count', 0x95A, 8, 'samples', 0, 100, category='DTC thresholds',
           description='Reserved until the P6.5/P6.6/P6.7 diagnostic transport is accepted.')
    scalar('Knock mode', 0xA00, 8, 'mode', 0, 2, category='Knock',
           description='0 = disabled, 1 = monitor only, 2 = global retard. Stopped engine only. Default disabled.')
    scalar('Knock filter band', 0xA01, 8, 'band', 0, 1, equation='X/16', category='Knock',
           description='0 = BF2 low, 1 = BF2 high (OEM default). Only two bands are switchable on this board; actual kHz depends on the fixed BF0/BF1/BF3 straps and IC clock. Stopped engine only.')
    scalar('Knock minimum RPM', 0xA02, 16, 'RPM', 600, 6000, category='Knock')
    scalar('Knock minimum coolant code', 0xA04, 8, 'OEM code', 5, 255, category='Knock',
           description='Native coolant code, with 5-code hysteresis. Temperature offset remains unresolved.')
    scalar('Knock sensor gain', 0xA06, 8, 'index', 0, 6, category='Knock',
           description='OEM initial gain index 4. Automatic mode uses this as the starting gain; manual mode holds it. Higher indexes increase gain. Stopped engine only.')
    scalar('Knock gain mode', 0xA10, 8, 'mode', 0, 1, category='Knock',
           description='0 = OEM automatic gain, 1 = manual gain. Default automatic. Stopped engine only.')
    scalar('Knock retard per event', 0xA11, 8, 'deg', 0.75, 12, equation='X*0.75', category='Knock',
           description='One global step for every eligible detected event. OEM starting value 3 degrees.')
    scalar('Knock maximum retard', 0xA12, 8, 'deg', 0.75, 12, equation='X*0.75', category='Knock',
           description='Global retard ceiling. OEM starting value 12 degrees.')
    scalar('Knock recovery speed', 0xA13, 16, '%', 25, 400, category='Knock',
           description='100% retains the OEM-derived quiet hold schedule. 200% halves the hold; recovery steps remain 0.75 degrees. Stopped engine only.')
    krpm = (0xA20, 16, 16, 'RPM', False)
    for title, at, unit, low, high, equation in [
        ('Knock window start',0xA40,'deg after reference',1.5,60,'X*0.75'),
        ('Knock window length',0xA50,'deg',9.75,60,'X*0.75'),
        ('Knock detection threshold',0xA60,'ratio',1,5,'X/16'),
        ('Knock minimum filling',0xA70,'%',0.75,191.25,'X*0.75')]:
        table(title,at,8,unit,krpm,equation=equation,decimals=2,low=low,high=high,
              category='Knock',description='Stopped engine only. See docs/KNOCK.md for standalone deviations and qualification.')
    ET.indent(root, space='  ')
    return ET.ElementTree(root)


def dtc_header():
    """C++ table the plugin uses to name stored DTC records."""
    codes = dtc_codes()
    quote = lambda value: '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'
    lines = ['// Generated by tools/tunerpro_definition.py from the ROM report table',
             '// (tunerpro/dtc_events.csv). Do not edit.',
             '#pragma once', 'namespace tu5jp {',
             'struct DtcEvent { unsigned char event; const char* code[4]; const char* text[4]; };',
             'inline const DtcEvent dtc_events[]={']
    for event, _, _, _ in sorted(DTC_EVENTS):
        row = [codes[event][s] for s in (1, 2, 4, 8)]
        lines.append(f'    {{0x{event:02X},{{{",".join(quote(c) for c, _ in row)}}},'
                     f'{{{",".join(quote(t) for _, t in row)}}}}},')
    lines += ['};', '}', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    out = ROOT / 'tunerpro'
    out.mkdir(exist_ok=True)
    (out / 'dtc_table.hpp').write_text(dtc_header(), encoding='utf-8', newline='\n')
    print(out / 'dtc_table.hpp')
    for alpha_n in (False, True):
        name = 'TU744_schema5_' + ('alpha_n' if alpha_n else 'speed_density') + '.xdf'
        tree = generate(alpha_n)
        tree.write(out / name, encoding='utf-8', xml_declaration=True)
        print(out / name)
