"""Assemble the local 0.0.6 package after builds and verification.

Run tools/package_ecu.py first (it writes release/TU744-v0.0.6/firmware).
Firmware-only release: the Tuning Wizard 0.0.4 executable is carried over
unchanged from the 0.0.4 package.
"""
from pathlib import Path
import hashlib
import json
import shutil
from artifacts import verify_firmware

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.0.6'
WIZARD = '0.0.4'
OUT = ROOT/f'release/TU744-v{VERSION}'
verify_firmware(ROOT,ROOT/'build/stock-95080','stock-95080')
image_manifest=json.loads((OUT/'firmware/image-manifest.json').read_text())
assert image_manifest['firmware_version']==VERSION and image_manifest['calibration_schema']==5
assert hashlib.sha256((OUT/f'firmware/TU744_{VERSION}_M95080.bin').read_bytes()).hexdigest()==image_manifest['binary_sha256']
basemap=f'basemaps/TU5JP_1.6_8v_speed_density_v{VERSION}.bin'
assert hashlib.sha256((ROOT/basemap).read_bytes()).hexdigest()==image_manifest['tune_sha256']
wizard=f'release/TU744-v{WIZARD}/wizard/TuningWizard_{WIZARD}.exe'
files = {
    basemap: basemap,
    wizard:f'wizard/TuningWizard_{WIZARD}.exe',
    'wizard/OPERATOR_MANUAL.md':'wizard/OPERATOR_MANUAL.md',
    f'docs/RELEASE-{VERSION}.md':'README.md',
    'tools/migrate_schema5.py':'tools/migrate_schema5.py',
    'src/knock_defaults.inc':'src/knock_defaults.inc',
}
logs=['firmware-0.0.6-native','firmware-0.0.6-stock-native']
not_run=['protocol (tests/test_protocol.py)',
         'keil knock detection and running windows (TU744_OEM_REPO)',
         'target simulations: audit, lifecycle, output-closure, foreground, engine, faults, timing, fuel, reset, flash, fwupdate, main, oem-scheduler (TU744_OEM_REPO)',
         'artifact verification report (tools/verify_artifacts.py, TU744_OEM_REPO)',
         'Wizard CTest (Wizard unchanged from 0.0.4; see the 0.0.4 package)',
         'foreground target simulation: run, fails with cal_valid 0 (0.0.5 fails the same way)',
         'Keil timing measurement of the OEM stepper call rate (assumed ~1 ms ADC completion)',
         'engine validation of stepper drive current, step rate, reversal wait and re-sync']
for name in logs:
    path=ROOT/f'build/{name}.log'
    data=path.read_bytes()
    text=data.decode('utf-16' if data.startswith(b'\xff\xfe') else 'utf-8',errors='replace')
    assert not any(m in text for m in ('Traceback','Assertion failed','FAIL')), name
    files[f'build/{name}.log']=f'verification/{name}.log'
for source,destination in files.items():
    target=OUT/destination; target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/source,target)
(OUT/'verification/NOT-RUN.txt').write_text(
    'Not run for this package:\n'+''.join(f'- {name}\n' for name in not_run))
inventory=[]
for p in sorted(OUT.rglob('*')):
    if p.is_file() and p.name!='SHA256SUMS.txt':
        inventory.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(OUT).as_posix())
(OUT/'SHA256SUMS.txt').write_text('\n'.join(inventory)+'\n')
print(f'PASS local {VERSION} package: {len(inventory)} files')
print(OUT)
