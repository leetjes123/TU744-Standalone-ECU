"""Assemble the local 0.0.4 package after builds and verification.

Run tools/package_ecu.py first (it writes release/TU744-v0.0.4/firmware).
"""
from pathlib import Path
import hashlib
import json
import shutil
from artifacts import verify_firmware

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.0.4'
OUT = ROOT/f'release/TU744-v{VERSION}'
verify_firmware(ROOT,ROOT/'build/stock-95080','stock-95080')
image_manifest=json.loads((OUT/'firmware/image-manifest.json').read_text())
assert image_manifest['firmware_version']==VERSION and image_manifest['calibration_schema']==5
assert hashlib.sha256((OUT/f'firmware/TU744_{VERSION}_M95080.bin').read_bytes()).hexdigest()==image_manifest['binary_sha256']
basemap=f'basemaps/TU5JP_1.6_8v_speed_density_v{VERSION}.bin'
assert hashlib.sha256((ROOT/basemap).read_bytes()).hexdigest()==image_manifest['tune_sha256']
wizard=ROOT/'wizard/build/updated/bin/TuningWizard.exe'
assert VERSION.encode() in wizard.read_bytes(), f'Wizard executable is not the {VERSION} build'
# The plugin source is unchanged since 0.0.3; without the SDK it is carried over.
previous=ROOT/'release/TU744-v0.0.3'
files = {
    basemap: basemap,
    'release/TU744-v0.0.3/tunerpro/TU744.dll':'tunerpro/TU744.dll',
    'release/TU744-v0.0.3/tunerpro/build-manifest.json':'tunerpro/build-manifest.json',
    'tunerpro/TU744_schema5_alpha_n.xdf':'tunerpro/TU744_schema5_alpha_n.xdf',
    'tunerpro/TU744_schema5_speed_density.xdf':'tunerpro/TU744_schema5_speed_density.xdf',
    'tunerpro/TU744_schema5.adx':'tunerpro/TU744_schema5.adx',
    'tunerpro/README.md':'tunerpro/README.md',
    'wizard/build/updated/bin/TuningWizard.exe':'wizard/TuningWizard.exe',
    'wizard/OPERATOR_MANUAL.md':'wizard/OPERATOR_MANUAL.md',
    'docs/KNOCK.md':'docs/KNOCK.md',
    'docs/PROTOCOL.md':'docs/PROTOCOL.md',
    'docs/LIVE-MONITOR.md':'docs/LIVE-MONITOR.md',
    f'docs/RELEASE-{VERSION}.md':'README.md',
    'tools/migrate_schema5.py':'tools/migrate_schema5.py',
    'src/knock_defaults.inc':'src/knock_defaults.inc',
}
logs=['firmware-0.0.4-native','firmware-0.0.4-stock-native','wizard-ctest-004','definitions-004']
not_run=['protocol (tests/test_protocol.py)','tunerpro plugin rebuild and tests (SDK unavailable)',
         'keil knock detection and running windows (TU744_OEM_REPO)',
         'target simulations: audit, lifecycle, output-closure, foreground, engine, faults, timing, fuel, reset, flash, fwupdate, main, oem-scheduler (TU744_OEM_REPO)',
         'artifact verification report (tools/verify_artifacts.py, TU744_OEM_REPO)',
         'engine validation of the faster IAC step rate']
for name in logs:
    path=ROOT/f'build/{name}.log'
    data=path.read_bytes()
    text=data.decode('utf-16' if data.startswith(b'\xff\xfe') else 'utf-8',errors='replace')
    assert not any(m in text for m in ('Traceback','Assertion failed','FAIL')), name
    files[f'build/{name}.log']=f'verification/{name}.log'
for source,destination in files.items():
    target=OUT/destination; target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/source,target)
shutil.copyfile(ROOT/f'docs/RELEASE-{VERSION}.md', OUT/f'docs/RELEASE-{VERSION}.md')
(OUT/'verification/NOT-RUN.txt').write_text(
    'Not run for this package:\n'+''.join(f'- {name}\n' for name in not_run))
p=OUT/'README.md'
p.write_text(p.read_text().replace('(LIVE-MONITOR.md)','(docs/LIVE-MONITOR.md)').replace('(KNOCK.md)','(docs/KNOCK.md)'))
inventory=[]
for p in sorted(OUT.rglob('*')):
    if p.is_file() and p.name!='SHA256SUMS.txt':
        inventory.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(OUT).as_posix())
(OUT/'SHA256SUMS.txt').write_text('\n'.join(inventory)+'\n')
print(f'PASS local {VERSION} package: {len(inventory)} files')
print(OUT)
