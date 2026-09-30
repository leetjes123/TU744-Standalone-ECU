"""Bundle identified experimental firmware/plugin artifacts; no SDK or tune."""
from pathlib import Path
import hashlib
import json
import tempfile
import zipfile
from artifacts import require, sha256, verify_firmware, verify_plugin

ROOT = Path(__file__).resolve().parents[1]


def package(root, output):
    stock = root / 'build/stock-95080'
    plugin = root / 'build/tunerpro'
    firmware_manifest, _ = verify_firmware(root, stock, 'stock-95080')
    plugin_manifest = verify_plugin(root, plugin)
    files = [root / 'README.md', plugin / 'TU744.dll', plugin / 'manifest.json',
             stock / 'TU5JP_STOCK95080_EXPERIMENTAL.bin', stock / 'manifest.json',
             root / 'tools/pack_stock_tune.py', root / 'tunerpro/TU744_schema5_speed_density.xdf',
             root / 'tunerpro/TU744_schema5_alpha_n.xdf',
             root / 'tunerpro/TU744_schema5.adx', root / 'tunerpro/README.md',
             *sorted((root / 'docs').glob('*.md'))]
    # Read once: the archive and its hash inventory use the same byte snapshot.
    payloads = {p.relative_to(root).as_posix(): p.read_bytes() for p in files}
    report = {name: hashlib.sha256(data).hexdigest() for name, data in payloads.items()}
    require(report['build/tunerpro/TU744.dll'] ==
            plugin_manifest['artifacts_sha256']['TU744.dll'], 'plugin changed during packaging')
    require(report['build/stock-95080/TU5JP_STOCK95080_EXPERIMENTAL.bin'] ==
            firmware_manifest['binary_sha256'], 'firmware changed during packaging')
    require(json.loads(payloads['build/tunerpro/manifest.json']) == plugin_manifest and
            json.loads(payloads['build/stock-95080/manifest.json']) == firmware_manifest,
            'build manifest changed during packaging')
    verify_firmware(root, stock, 'stock-95080')
    verify_plugin(root, plugin)
    metadata = dict(profile='stock-95080', experimental=True, production_release_accepted=False,
                    hardware_validated=False, engine_validated=False,
                    firmware_manifest_sha256=report['build/stock-95080/manifest.json'],
                    plugin_manifest_sha256=report['build/tunerpro/manifest.json'])
    payloads['PACKAGE.json'] = (json.dumps(metadata, indent=2) + '\n').encode()
    report['PACKAGE.json'] = hashlib.sha256(payloads['PACKAGE.json']).hexdigest()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix='.tu5jp-package-', suffix='.zip', dir=output.parent, delete=False) as temp:
        temporary = Path(temp.name)
    try:
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as archive:
            for name, data in payloads.items():
                archive.writestr(name, data)
            archive.writestr('SHA256.json', json.dumps(report, indent=2) + '\n')
        with zipfile.ZipFile(temporary) as archive:
            require(archive.testzip() is None, 'archive CRC failure')
            for name, digest in report.items():
                require(hashlib.sha256(archive.read(name)).hexdigest() == digest, f'archive hash failure: {name}')
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return report


if __name__ == '__main__':
    output = ROOT / 'build/TU5JP_stock95080_TunerPro_experimental.zip'
    try:
        report = package(ROOT, output)
    except (OSError, ValueError, KeyError) as error:
        raise SystemExit(f'FAIL: {error}') from error
    print(output)
    print('SHA256 ' + sha256(output))
    print(f'PASS {len(report)} packaged files verified; experimental only, no SDK and no base map')
