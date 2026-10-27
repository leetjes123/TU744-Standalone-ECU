"""Assemble the local 0.0.7 package after builds and verification.

Run tools/package_ecu.py first (it writes release/TU744-v0.0.7/firmware),
with basemaps/TU5JP_1.6_8v_speed_density_v0.0.6.bin as the built-in tune.
The package carries Tuning Wizard 0.0.6 from build/wizard-0.0.6 and both
0.0.6 basemaps, unchanged (TU5JP 1.6 built in, TU3JP 1.4 to load with the Wizard).
"""
from pathlib import Path
import hashlib
import json
import shutil
from artifacts import verify_firmware

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.0.7'
WIZARD = '0.0.6'
BASEMAPS = '0.0.6'
OUT = ROOT/f'release/TU744-v{VERSION}'
verify_firmware(ROOT,ROOT/'build/stock-95080','stock-95080')
image_manifest=json.loads((OUT/'firmware/image-manifest.json').read_text())
assert image_manifest['firmware_version']==VERSION and image_manifest['calibration_schema']==5
assert hashlib.sha256((OUT/f'firmware/TU744_{VERSION}_M95080.bin').read_bytes()).hexdigest()==image_manifest['binary_sha256']
basemap=f'basemaps/TU5JP_1.6_8v_speed_density_v{BASEMAPS}.bin'
assert hashlib.sha256((ROOT/basemap).read_bytes()).hexdigest()==image_manifest['tune_sha256']
wizard=f'build/wizard-{WIZARD}/TuningWizard_{WIZARD}.exe'
tu3=f'basemaps/TU3JP_1.4_8v_speed_density_v{BASEMAPS}.bin'
files = {
    basemap: basemap,
    tu3: tu3,
    wizard:f'wizard/TuningWizard_{WIZARD}.exe',
    'wizard/OPERATOR_MANUAL.md':'wizard/OPERATOR_MANUAL.md',
    f'docs/RELEASE-{VERSION}.md':'README.md',
    'tools/migrate_schema5.py':'tools/migrate_schema5.py',
    'src/knock_defaults.inc':'src/knock_defaults.inc',
}
logs=[f'firmware-{VERSION}-native',f'firmware-{VERSION}-stock-native',f'keil-{VERSION}-stock-95080',
      f'wizard-{WIZARD}-tests',f'wizard-{WIZARD}-smoke']
not_run=['protocol (tests/test_protocol.py)',
         'keil knock detection and running windows (TU744_OEM_REPO)',
         'target simulations: audit, lifecycle, output-closure, foreground, engine, faults, timing, fuel, reset, flash, fwupdate, main, oem-scheduler (TU744_OEM_REPO)',
         'artifact verification report (tools/verify_artifacts.py, TU744_OEM_REPO)',
         'engine validation of the sync loss recovery window, the idle spark correction RPM limit and the service release after a tool session',
         'Tuning Wizard 0.0.6 logging against a real ECU (Dropped Frames, Sample Gap)']
for name in logs:
    path=ROOT/f'build/{name}.log'
    data=path.read_bytes()
    text=data.decode('utf-16' if data.startswith(b'\xff\xfe') else 'utf-8',errors='replace')
    assert not any(m in text for m in ('Traceback','Assertion failed','FAIL')), name
    files[f'build/{name}.log']=f'verification/{name}.log'
# Files from earlier packages that this one replaces.
for stale in []:
    if (OUT/stale).exists(): (OUT/stale).unlink()
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
