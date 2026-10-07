"""Assemble the local 0.0.3 review/bench package after builds and verification.

Run tools/package_ecu.py first (it writes release/TU744-v0.0.3/firmware).
"""
from pathlib import Path
import hashlib
import json
import shutil
from artifacts import verify_firmware, verify_plugin

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.0.3'
OUT = ROOT/f'release/TU744-v{VERSION}'
verify_firmware(ROOT,ROOT/'build/stock-95080','stock-95080')
verify_plugin(ROOT,ROOT/'build/tunerpro')
image_manifest=json.loads((OUT/'firmware/image-manifest.json').read_text())
assert image_manifest['firmware_version']==VERSION and image_manifest['calibration_schema']==5
assert hashlib.sha256((OUT/f'firmware/TU744_{VERSION}_M95080.bin').read_bytes()).hexdigest()==image_manifest['binary_sha256']
basemap=f'basemaps/TU5JP_1.6_8v_speed_density_v{VERSION}.bin'
assert hashlib.sha256((ROOT/basemap).read_bytes()).hexdigest()==image_manifest['tune_sha256']
keil=json.loads((ROOT/'build/keil-knock-detection/results.json').read_text())
assert keil['completion-model']['status']=='pass'
assert keil['production_manifest']['binary_sha256']==image_manifest['base_firmware_sha256']
windows=json.loads((ROOT/'build/knock-windows/results.json').read_text())
assert windows['hex_sha256']==hashlib.sha256((ROOT/'build/stock-95080/TU5JP.H86').read_bytes()).hexdigest()
assert len(windows['reports'])==14
wizard=ROOT/'wizard/build/updated/bin/TuningWizard.exe'
assert VERSION.encode() in wizard.read_bytes(), 'Wizard executable is not the 0.0.3 build'
files = {
    basemap: basemap,
    'build/tunerpro/TU744.dll':'tunerpro/TU744.dll',
    'build/tunerpro/manifest.json':'tunerpro/build-manifest.json',
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
    'build/keil-knock-detection/results.json':'verification/keil-adc.json',
    'build/knock-windows/results.json':'verification/running-windows.json',
    'build/verification-stock-95080.json':'verification/stock-build.json',
    'build/verification.json':'verification/development-build.json',
}
logs=['firmware-0.0.3-native','firmware-0.0.3-stock-native','protocol-003','migration-003',
      'knock-oem','knock-target','knock-windows','keil-detection','knock-boot-003',
      'knock-keil-boot-003','definitions-003','logging-test-003','tunerpro-003','wizard-ctest-003',
      'iac-hold','scheduler-regressions',
      'target-audit','target-lifecycle','target-output-closure','target-foreground','target-engine',
      'target-faults','target-timing','target-fuel','target-reset','target-flash','target-fwupdate',
      'target-main','target-oem-scheduler']
# Testing was stopped before the full lane finished: package the logs that
# passed and list the others explicitly instead of implying they ran.
not_run=[]
# Lane summaries record exit status for tests that report JSON, not PASS lines.
lane=''.join((ROOT/f'build/{n}').read_text() for n in
             ('verification-0.0.3-summary.log','verification-0.0.3-rerun.log') if (ROOT/f'build/{n}').is_file())
for name in logs:
    path=ROOT/f'build/{name}.log'
    data=path.read_bytes() if path.is_file() else b''
    # PowerShell redirection can produce UTF-16 logs.
    text=data.decode('utf-16' if data.startswith(b'\xff\xfe') else 'utf-8',errors='replace')
    passed=('PASS' in text or '100% tests passed' in text or f'PASS {name}\n' in lane) and not any(
        marker in text for marker in ('Traceback','Assertion failed','FAIL','Set TU744_OEM_REPO'))
    if passed: files[f'build/{name}.log']=f'verification/{name}.log'
    else: not_run.append(name)
for source,destination in files.items():
    target=OUT/destination; target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/source,target)
shutil.copyfile(ROOT/f'docs/RELEASE-{VERSION}.md', OUT/f'docs/RELEASE-{VERSION}.md')
(OUT/'verification/NOT-RUN.txt').write_text(
    'Not run or not completed for this package (testing stopped on request):\n'+
    ''.join(f'- {name}\n' for name in not_run))
# Rewrite repository-relative links for the package root.
p=OUT/'README.md'
p.write_text(p.read_text().replace('(LIVE-MONITOR.md)','(docs/LIVE-MONITOR.md)').replace('(KNOCK.md)','(docs/KNOCK.md)'))
inventory=[]
for p in sorted(OUT.rglob('*')):
    if p.is_file() and p.name!='SHA256SUMS.txt':
        inventory.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(OUT).as_posix())
(OUT/'SHA256SUMS.txt').write_text('\n'.join(inventory)+'\n')
print(f'PASS local {VERSION} package: {len(inventory)} files, current image and test identities verified')
print(OUT)
