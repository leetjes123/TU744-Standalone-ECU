"""Assemble the local 0.0.2 review/bench package after builds and verification."""
from pathlib import Path
import hashlib
import json
import shutil
from artifacts import verify_firmware, verify_plugin

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'release/TU744-v0.0.2'
verify_firmware(ROOT,ROOT/'build/stock-95080','stock-95080')
verify_plugin(ROOT,ROOT/'build/tunerpro')
image_manifest=json.loads((OUT/'firmware/image-manifest.json').read_text())
assert image_manifest['firmware_version']=='0.0.2' and image_manifest['calibration_schema']==5
assert hashlib.sha256((OUT/'firmware/TU744_0.0.2_M95080.bin').read_bytes()).hexdigest()==image_manifest['binary_sha256']
keil=json.loads((ROOT/'build/keil-knock-detection/results.json').read_text())
assert keil['completion-model']['status']=='pass'
assert keil['production_manifest']['binary_sha256']==image_manifest['base_firmware_sha256']
windows=json.loads((ROOT/'build/knock-windows/results.json').read_text())
assert windows['hex_sha256']==hashlib.sha256((ROOT/'build/stock-95080/TU5JP.H86').read_bytes()).hexdigest()
assert len(windows['reports'])==14
files = {
    'basemaps/TU5JP_1.6_8v_speed_density_v0.0.2.bin': 'basemaps/TU5JP_1.6_8v_speed_density_v0.0.2.bin',
    'build/tunerpro/TU744.dll':'tunerpro/TU744.dll',
    'build/tunerpro/manifest.json':'tunerpro/build-manifest.json',
    'tunerpro/TU744_schema5_alpha_n.xdf':'tunerpro/TU744_schema5_alpha_n.xdf',
    'tunerpro/TU744_schema5_speed_density.xdf':'tunerpro/TU744_schema5_speed_density.xdf',
    'tunerpro/TU744_schema5.adx':'tunerpro/TU744_schema5.adx',
    'wizard/build/updated/bin/TuningWizard.exe':'wizard/TuningWizard.exe',
    'wizard/OPERATOR_MANUAL.md':'wizard/OPERATOR_MANUAL.md',
    'docs/KNOCK.md':'docs/KNOCK.md',
    'docs/PROTOCOL.md':'docs/PROTOCOL.md',
    'docs/LIVE-MONITOR.md':'docs/LIVE-MONITOR.md',
    'docs/audits/tu744-knock-2026-09-30/IMPLEMENTATION-PLAN.md':'docs/audits/tu744-knock-2026-09-30/IMPLEMENTATION-PLAN.md',
    'docs/audits/tu744-knock-2026-09-30/README.md':'docs/audits/tu744-knock-2026-09-30/README.md',
    'docs/RELEASE-0.0.2.md':'README.md',
    'tools/migrate_schema5.py':'tools/migrate_schema5.py',
    'src/knock_defaults.inc':'src/knock_defaults.inc',
    'tunerpro/README.md':'tunerpro/README.md',
    'build/keil-knock-detection/results.json':'verification/keil-adc.json',
    'build/knock-windows/results.json':'verification/running-windows.json',
    'build/verification-stock-95080.json':'verification/stock-build.json',
    'build/verification.json':'verification/development-build.json',
}
logs=['firmware-0.0.2-native','firmware-0.0.2-stock-native','knock-oem','knock-target',
      'knock-boot-002','knock-keil-boot-002','protocol-002','migration-002',
      'definitions-002','logging-test','wizard-ctest-002','tunerpro-002']
for name in logs:
    data=(ROOT/f'build/{name}.log').read_bytes()
    # PowerShell redirection can produce UTF-16 logs.
    text=data.decode('utf-16' if data.startswith(b'\xff\xfe') else 'utf-8',errors='replace')
    assert 'PASS' in text or '100% tests passed' in text, name
    assert 'Traceback' not in text and 'Assertion failed' not in text, name
    files[f'build/{name}.log']=f'verification/{name}.log'
for source,destination in files.items():
    target=OUT/destination; target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/source,target)
shutil.copyfile(ROOT/'docs/RELEASE-0.0.2.md', OUT/'docs/RELEASE-0.0.2.md')
# Rewrite repository-relative links for the package root.
p=OUT/'README.md'
s=p.read_text().replace('(KNOCK.md)','(docs/KNOCK.md)')
s += '\nNative Keil ADC completion is simulator-blocked. The completion-model lane and 13-window bench self-test pass; physical ADC, filter frequency and engine acceptance remain open. No ECU was flashed.\n'
p.write_text(s)
inventory=[]
for p in sorted(OUT.rglob('*')):
    if p.is_file() and p.name!='SHA256SUMS.txt':
        inventory.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(OUT).as_posix())
(OUT/'SHA256SUMS.txt').write_text('\n'.join(inventory)+'\n')
print(f'PASS local 0.0.2 package: {len(inventory)} files, current image and test identities verified')
print(OUT)
