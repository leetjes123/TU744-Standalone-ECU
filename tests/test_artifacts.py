"""Negative release-tool checks, repeated under normal Python and python -O."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from artifacts import (PROFILES, binary_image, build_input_hashes, hashes,
                       plugin_source_hashes, programmed, source_hashes,
                       verify_firmware, verify_plugin)
from package_tunerpro import package


def record(kind, at=0, payload=b''):
    data = bytes([len(payload)]) + at.to_bytes(2, 'big') + bytes([kind]) + payload
    return ':' + (data + bytes([-sum(data) & 255])).hex().upper()


RESET = record(0, 0, b'\xfa\x00\x00\x02')
EOF = record(1)


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ['src/core.c', 'src/storage_flash.inc', 'include/ecu.h', 'target/c167/board.c',
                     'tools/build.py', 'tools/artifacts.py', 'tools/build_tunerpro.py',
                     'TU5JP.uvproj', 'README.md', 'tools/pack_stock_tune.py',
                     'tunerpro/README.md', 'tunerpro/TU744_schema4.adx',
                     'tunerpro/TU744_schema4_speed_density.xdf', 'tunerpro/TU744_schema4_alpha_n.xdf',
                     'tunerpro/plugin.cpp', 'tunerpro/protocol.hpp', 'tunerpro/plugin.rc',
                     'tunerpro/plugin.def', 'tunerpro/test_plugin.cpp', 'tunerpro/progress.hpp',
                     'tunerpro/test_host.hpp', 'tunerpro/dtc_table.hpp',
                     'tests/hal_fake.c', 'tests/protocol_bridge.c', 'docs/RELEASE.md']:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('test fixture\n')
        for profile, (_, enabled, eeprom, storage, binary) in PROFILES.items():
            build = self.root / 'build' / profile
            build.mkdir(parents=True)
            (build / 'TU5JP.H86').write_text(RESET + '\n' + EOF + '\n')
            (build / 'TU5JP.m66').write_text('0 WARNING(S),  0 ERROR(S)\n')
            (build / 'firmware.lnp').write_text('test fixture\n')
            (build / 'build.log').write_text('Program Size: test fixture\n')
            image = binary_image(programmed(build / 'TU5JP.H86'))
            names = ['TU5JP.H86', 'TU5JP.m66', 'firmware.lnp', 'build.log']
            if binary:
                (build / binary).write_bytes(image)
                names.append(binary)
            manifest = dict(manifest_schema=2, profile=profile, engine_outputs_enabled=enabled,
                            eeprom_bytes=eeprom, calibration_storage=storage, hardware_validated=False,
                            engine_validated=False, exact_oem_dtc_parity=False, calibration_included=False,
                            source_tree_unchanged_during_build=True,
                            source_sha256=source_hashes(self.root), build_inputs_sha256=build_input_hashes(self.root),
                            artifacts_sha256=hashes(build, [build / name for name in names]),
                            toolchain_sha256={name: '0' * 64 for name in
                                             ['BIN/C166.EXE', 'BIN/A166.EXE', 'BIN/L166.EXE',
                                              'BIN/OH166.EXE', 'LIB/C167L.LIB']})
            if binary:
                manifest.update(binary_sha256=hashlib.sha256(image).hexdigest(), binary_bytes=len(image))
            (build / 'manifest.json').write_text(json.dumps(manifest))
        plugin = self.root / 'build/tunerpro'
        plugin.mkdir()
        (plugin / 'TU744.dll').write_bytes(b'fixture DLL')
        (plugin / 'build.log').write_text('fixture tests passed\n')
        (plugin / 'manifest.json').write_text(json.dumps(dict(
            manifest_schema=1, plugin_tests_passed=True, source_sha256=plugin_source_hashes(self.root),
            sdk_sha256={'ITPPlugin.h': '0' * 64}, toolchain_sha256={'cl.exe': '0' * 64},
            artifacts_sha256=hashes(plugin, [plugin / 'TU744.dll', plugin / 'build.log']))))
        self.build = self.root / 'build/stock-95080'

    def mutate_manifest(self, fn, build=None):
        path = (build or self.build) / 'manifest.json'
        data = json.loads(path.read_text())
        fn(data)
        path.write_text(json.dumps(data))

    def test_valid_profiles_and_package(self):
        for profile in PROFILES:
            with self.subTest(profile=profile):
                verify_firmware(self.root, self.root / 'build' / profile, profile)
        verify_plugin(self.root, self.root / 'build/tunerpro')
        report = package(self.root, self.root / 'candidate.zip')
        self.assertIn('build/tunerpro/manifest.json', report)
        self.assertIn('PACKAGE.json', report)

    def test_hex_rejects_malformed_records(self):
        cases = {
            'checksum': RESET[:-2] + 'FF\n' + EOF,
            'length': ':05000000FA0000020A\n' + EOF,
            'missing_eof': RESET,
            'empty': EOF,
            'after_eof': RESET + '\n' + EOF + '\n' + RESET,
            'unknown_type': RESET + '\n' + record(6) + '\n' + EOF,
            'bad_control_length': RESET + '\n' + record(2, payload=b'\0') + '\n' + EOF,
            'bad_control_address': RESET + '\n' + record(4, 1, b'\0\0') + '\n' + EOF,
            'duplicate': RESET + '\n' + RESET + '\n' + EOF,
            'sfr_window': RESET + '\n' + record(0, 0xE000, b'\0') + '\n' + EOF,
            'cal_sector': RESET + '\n' + record(4, payload=b'\0\5') + '\n' + record(0, payload=b'\0') + '\n' + EOF,
            'segment_crossing': RESET + '\n' + record(0, 0xFFFF, b'\0\0') + '\n' + EOF,
        }
        path = self.root / 'invalid.hex'
        for name, content in cases.items():
            with self.subTest(case=name):
                path.write_text(content + '\n')
                with self.assertRaises(ValueError):
                    programmed(path, 0x50000)

    def test_source_changed(self):
        (self.root / 'src/core.c').write_text('different\n')
        with self.assertRaisesRegex(ValueError, 'firmware source identity'):
            verify_firmware(self.root, self.build, 'stock-95080')

    def test_source_added(self):
        (self.root / 'src/new.c').write_text('new\n')
        with self.assertRaisesRegex(ValueError, 'firmware source identity'):
            verify_firmware(self.root, self.build, 'stock-95080')

    def test_source_inventory_omitted(self):
        self.mutate_manifest(lambda m: m.update(source_sha256={}))
        with self.assertRaisesRegex(ValueError, 'firmware source identity'):
            verify_firmware(self.root, self.build, 'stock-95080')

    def test_build_recipe_changed(self):
        (self.root / 'tools/build.py').write_text('different flags\n')
        with self.assertRaisesRegex(ValueError, 'build input identity'):
            verify_firmware(self.root, self.build, 'stock-95080')

    def test_artifact_changed(self):
        (self.build / 'TU5JP.H86').write_text('corrupt\n')
        with self.assertRaisesRegex(ValueError, 'artifact hash mismatch'):
            verify_firmware(self.root, self.build, 'stock-95080')

    def test_matching_hash_does_not_bypass_hex_rules(self):
        path = self.build / 'TU5JP.H86'
        path.write_text(RESET + '\n')
        self.mutate_manifest(lambda m: m['artifacts_sha256'].update({'TU5JP.H86': hashlib.sha256(path.read_bytes()).hexdigest()}))
        with self.assertRaisesRegex(ValueError, 'missing EOF'):
            verify_firmware(self.root, self.build, 'stock-95080')

    def test_binary_must_match_hex(self):
        name = PROFILES['stock-95080'][4]
        path = self.build / name
        image = bytearray(path.read_bytes())
        image[0x50000] = 0
        path.write_bytes(image)
        digest = hashlib.sha256(image).hexdigest()
        def mutation(m):
            m['artifacts_sha256'][name] = digest
            m['binary_sha256'] = digest
        self.mutate_manifest(mutation)
        with self.assertRaisesRegex(ValueError, 'binary differs'):
            verify_firmware(self.root, self.build, 'stock-95080')

    def test_production_claim_rejected(self):
        self.mutate_manifest(lambda m: m.update(hardware_validated=True))
        with self.assertRaisesRegex(ValueError, 'hardware_validated'):
            verify_firmware(self.root, self.build, 'stock-95080')

    def test_missing_toolchain_rejected(self):
        self.mutate_manifest(lambda m: m.update(toolchain_sha256={}))
        with self.assertRaisesRegex(ValueError, 'compiler/linker/runtime'):
            verify_firmware(self.root, self.build, 'stock-95080')

    def test_plugin_source_drift_rejects_package(self):
        output = self.root / 'candidate.zip'
        output.write_bytes(b'previous archive')
        (self.root / 'tunerpro/protocol.hpp').write_text('changed wire contract\n')
        with self.assertRaisesRegex(ValueError, 'plugin: stale'):
            package(self.root, output)
        self.assertEqual(output.read_bytes(), b'previous archive')

    def test_plugin_dll_drift_rejects_package(self):
        (self.root / 'build/tunerpro/TU744.dll').write_bytes(b'wrong DLL')
        with self.assertRaisesRegex(ValueError, 'DLL/log identity mismatch'):
            package(self.root, self.root / 'candidate.zip')

    def test_missing_definition_rejects_package(self):
        (self.root / 'tunerpro/TU744_schema4_alpha_n.xdf').unlink()
        with self.assertRaises(FileNotFoundError):
            package(self.root, self.root / 'candidate.zip')

    def test_failed_plugin_test_rejects_package(self):
        self.mutate_manifest(lambda m: m.update(plugin_tests_passed=False), self.root / 'build/tunerpro')
        with self.assertRaisesRegex(ValueError, 'successful build/test identity'):
            package(self.root, self.root / 'candidate.zip')

    def test_optimized_subprocess_rejects_corruption(self):
        # Actually launch -O: source inspection alone cannot establish this.
        self.mutate_manifest(lambda m: m.update(source_sha256={}))
        code = ('import sys; from pathlib import Path; sys.path.insert(0, sys.argv[1]); '
                'from artifacts import verify_firmware; root=Path(sys.argv[2]); '
                'verify_firmware(root, root / "build/stock-95080", "stock-95080")')
        result = subprocess.run([sys.executable, '-O', '-c', code, str(TOOLS), str(self.root)],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('firmware source identity', result.stderr)

    def test_project_regeneration_without_private_checkout(self):
        shutil.copyfile(TOOLS / 'project.py', self.root / 'tools/project.py')
        shutil.copyfile(TOOLS.parent / 'TU5JP.uvproj', self.root / 'TU5JP.uvproj')
        command = [sys.executable, str(self.root / 'tools/project.py')]
        first = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        project = (self.root / 'TU5JP.uvproj').read_bytes()
        self.assertIn(b'core.c', project)
        second = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
        self.assertEqual((self.root / 'TU5JP.uvproj').read_bytes(), project)


if __name__ == '__main__':
    unittest.main()
