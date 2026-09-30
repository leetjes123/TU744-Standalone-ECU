"""Package already-built desktop and experimental firmware artifacts; no flashing."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT.parent
name = 'TuningWizard-0.0.2-Windows-x64-experimental'
files = {
    'TuningWizard.exe': ROOT / 'build/updated/bin/TuningWizard.exe',
    'docs/OPERATOR_MANUAL.md': ROOT / 'OPERATOR_MANUAL.md',
    'docs/PORTING-PLAN.md': ROOT / 'PORTING-PLAN.md',
    'docs/VALIDATION.md': ROOT / 'VALIDATION.md',
    'firmware/TU5JP_STOCK95080_EXPERIMENTAL.bin': FW / 'build/stock-95080/TU5JP_STOCK95080_EXPERIMENTAL.bin',
    'firmware/build-manifest.json': FW / 'build/stock-95080/manifest.json',
}
manifest = {
    'application': 'Tuning Wizard', 'version': '0.0.2',
    'live_frame_version': 3, 'live_frame_bytes': 44,
    'sync_loss_counter': 'u16 saturated at 65535',
    'hardware_validated': False, 'engine_validated': False,
    'calibration_included': False,
    'restore_tune': 'Migrate saved schema-4 tunes with tools/migrate_schema5.py first. After flashing, open the schema-5 tune, Write active tune, Save tune to ECU flash, then key-cycle.',
    'sha256': {n: hashlib.sha256(p.read_bytes()).hexdigest() for n, p in files.items()},
}
fw_manifest = json.loads(files['firmware/build-manifest.json'].read_text())
assert fw_manifest['profile'] == 'stock-95080'
assert fw_manifest['calibration_included'] is False
assert fw_manifest['source_tree_unchanged_during_build'] is True
assert manifest['sha256']['firmware/TU5JP_STOCK95080_EXPERIMENTAL.bin'] == fw_manifest['binary_sha256']
for relative, digest in fw_manifest['source_sha256'].items():
    assert hashlib.sha256((FW / relative).read_bytes()).hexdigest() == digest, relative
for relative, digest in fw_manifest['build_inputs_sha256'].items():
    assert hashlib.sha256((FW / relative).read_bytes()).hexdigest() == digest, relative
out = ROOT / 'dist'
out.mkdir(exist_ok=True)
archive = out / (name + '.zip')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for n, p in files.items():
        z.write(p, n)
    z.writestr('manifest.json', json.dumps(manifest, indent=2) + '\n')
(out / (name + '.sha256')).write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n')
print(archive)
