"""Assemble the local 0.0.8 package after builds and verification.

Run tools/package_ecu.py first (it writes release/TU744-v0.0.8/firmware),
with basemaps/TU5JP_1.6_8v_speed_density_v0.0.8.bin as the built-in tune.
The package carries Tuning Wizard 0.0.7 from build/wizard-0.0.7 and both
0.0.8 basemaps (TU5JP 1.6 built in, TU3JP 1.4 to load with the Wizard).
"""
from pathlib import Path
import hashlib
import json
import shutil
from artifacts import verify_firmware

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.0.8'
WIZARD = '0.0.7'
BASEMAPS = '0.0.8'
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
    'src/knock_defaults.inc':'src/knock_defaults.inc',
}
logs=[f'firmware-{VERSION}-native',f'firmware-{VERSION}-stock-native',f'keil-{VERSION}-stock-95080',
      f'wizard-{WIZARD}-tests',f'wizard-{WIZARD}-smoke']
not_run=['target simulations of the final binary (the run on the previous build passed; the final change only removed the idle speed condition and floor)',
         'bench and engine validation of the cranking dwell, coil watchdog, stall recovery and the new idle settings',
         'Tuning Wizard 0.0.7 against a real ECU (inhibit banner, live frame 5, developer tab)',
         'Keil uVision native knock ADC completion (simulator-blocked in the installed version; the completion model passes)',
         'Known failing, not caused by 0.0.8: tests/test_lifecycle_target.py expects wideband policy 0 to disable the wideband; the firmware has treated policy 0 as direct (trusted when the sample is valid) since 0.0.2',
         'Known failing, not caused by 0.0.8: the synthetic fixture tune of tests/test_foreground_target.py fails validation (cal_valid 0, also on 0.0.5-0.0.7); foreground runs boot the release binary with --image instead']
for name in logs:
    path=ROOT/f'build/{name}.log'
    data=path.read_bytes()
    text=data.decode('utf-16' if data.startswith(b'\xff\xfe') else 'utf-8',errors='replace')
    assert not any(m in text for m in ('Traceback','Assertion failed','FAIL')), name
    files[f'build/{name}.log']=f'verification/{name}.log'
# Files from earlier packages that this one replaces.
for stale in ['verification/simulations-0.0.8.log']:
    if (OUT/stale).exists(): (OUT/stale).unlink()
for source,destination in files.items():
    target=OUT/destination; target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/source,target)
(OUT/'verification/NOT-RUN.txt').write_text(
    'Not run or known failing for this package:\n'+''.join(f'- {name}\n' for name in not_run))
inventory=[]
for p in sorted(OUT.rglob('*')):
    if p.is_file() and p.name!='SHA256SUMS.txt':
        inventory.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(OUT).as_posix())
(OUT/'SHA256SUMS.txt').write_text('\n'.join(inventory)+'\n')
print(f'PASS local {VERSION} package: {len(inventory)} files')
print(OUT)
