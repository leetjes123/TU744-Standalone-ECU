"""Fail-closed build identity and Intel HEX checks, including under python -O.

Hashes bind local artifacts to inputs; they are not signatures, program-memory
integrity checks on an ECU, or evidence of hardware/engine acceptance.
"""
from pathlib import Path
import hashlib
import json

PROFILES = {
    'c166': (0x70000, False, 8192, 'eeprom', None),
    'engine-experimental': (0x70000, True, 8192, 'eeprom', 'TU5JP_ENGINE_EXPERIMENTAL.bin'),
    'stock-95080': (0x50000, True, 1024, 'nor-flash', 'TU5JP_STOCK95080_EXPERIMENTAL.bin'),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def hashes(root, paths):
    return {p.relative_to(root).as_posix(): sha256(p) for p in sorted(paths)}


def source_hashes(root):
    return hashes(root, [p for directory in ['src', 'include', 'target/c167']
                         for p in (root / directory).iterdir()
                         if p.suffix.lower() in ['.c', '.h', '.a66', '.inc']])


def build_input_hashes(root):
    return hashes(root, [root / name for name in
                        ['tools/build.py', 'tools/artifacts.py', 'TU5JP.uvproj']])


def plugin_source_hashes(root):
    return hashes(root, [*(root / 'src').glob('*.c'), *(root / 'include').glob('*.h'),
                         *(root / 'src').glob('*.inc'),
                         *[root / 'tunerpro' / name for name in
                           ['plugin.cpp', 'protocol.hpp', 'dtc_table.hpp', 'progress.hpp', 'plugin.rc', 'plugin.def', 'test_plugin.cpp', 'test_host.hpp']],
                         root / 'tests/hal_fake.c', root / 'tests/protocol_bridge.c',
                         root / 'tools/build_tunerpro.py', root / 'tools/artifacts.py'])


def programmed(path, limit=0x70000):
    """Parse all records, rejecting bad shapes, unknown types and missing EOF."""
    base, written, ended = 0, {}, False
    for line_number, line in enumerate(Path(path).read_text().splitlines(), 1):
        where = f'{path}:{line_number}'
        require(line.startswith(':'), f'{where}: missing record marker')
        require(not ended, f'{where}: record after EOF')
        require(len(line) % 2 == 1 and all(c in '0123456789ABCDEFabcdef' for c in line[1:]),
                f'{where}: invalid hexadecimal record')
        record = bytes.fromhex(line[1:])
        require(len(record) >= 5, f'{where}: short record')
        length, offset, kind = record[0], int.from_bytes(record[1:3], 'big'), record[3]
        require(len(record) == length + 5, f'{where}: length mismatch')
        require(sum(record) & 255 == 0, f'{where}: checksum mismatch')
        payload = record[4:-1]
        if kind == 0:
            require(offset + length <= 0x10000, f'{where}: record crosses address segment')
            for i, value in enumerate(payload):
                address = base + offset + i
                require(address < limit and not 0xE000 <= address < 0x10000,
                        f'{where}: forbidden flash address {address:#x}')
                require(address not in written, f'{where}: overlapping data at {address:#x}')
                written[address] = value
        elif kind in (1, 2, 3, 4, 5):
            require(offset == 0 and length == {1: 0, 2: 2, 3: 4, 4: 2, 5: 4}[kind],
                    f'{where}: malformed control record')
            if kind == 1:
                ended = True
            elif kind in (2, 4):
                base = int.from_bytes(payload, 'big') << (4 if kind == 2 else 16)
        else:
            raise ValueError(f'{where}: unsupported record type {kind}')
    require(ended, f'{path}: missing EOF')
    require(all(at in written for at in range(4)), f'{path}: missing reset vector')
    return written


def binary_image(written):
    image = bytearray(b'\xff' * 0x80000)
    for at, value in written.items():
        image[at] = value
    return bytes(image)


def verify_firmware(root, build, profile):
    require(profile in PROFILES, f'unknown profile: {profile}')
    limit, enabled, eeprom, storage, binary = PROFILES[profile]
    manifest = json.loads((build / 'manifest.json').read_text())
    for key, expected in dict(manifest_schema=2, profile=profile, engine_outputs_enabled=enabled,
                              eeprom_bytes=eeprom, calibration_storage=storage,
                              hardware_validated=False, engine_validated=False,
                              exact_oem_dtc_parity=False, calibration_included=False,
                              source_tree_unchanged_during_build=True).items():
        require(type(manifest.get(key)) is type(expected) and manifest[key] == expected,
                f'{profile}: unexpected {key}')
    require(manifest.get('source_sha256') == source_hashes(root),
            f'{profile}: stale or incomplete firmware source identity')
    require(manifest.get('build_inputs_sha256') == build_input_hashes(root),
            f'{profile}: stale or incomplete build input identity')
    names = ['TU5JP.H86', 'TU5JP.m66', 'firmware.lnp', 'build.log'] + ([binary] if binary else [])
    require(manifest.get('artifacts_sha256') == hashes(build, [build / name for name in names]),
            f'{profile}: artifact hash mismatch or incomplete manifest')
    require(set(manifest.get('toolchain_sha256', {})) ==
            {'BIN/C166.EXE', 'BIN/A166.EXE', 'BIN/L166.EXE', 'BIN/OH166.EXE', 'LIB/C167L.LIB'},
            f'{profile}: missing compiler/linker/runtime identity')
    require(all(isinstance(digest, str) and len(digest) == 64 and
                all(c in '0123456789abcdef' for c in digest)
                for digest in manifest['toolchain_sha256'].values()),
            f'{profile}: malformed toolchain hashes')
    written = programmed(build / 'TU5JP.H86', limit)
    require('0 WARNING(S),  0 ERROR(S)' in (build / 'TU5JP.m66').read_text(),
            f'{profile}: linker not warning/error clean')
    if binary:
        image = (build / binary).read_bytes()
        require(image == binary_image(written), f'{profile}: binary differs from linked HEX')
        require(manifest.get('binary_sha256') == sha256(build / binary) and
                manifest.get('binary_bytes') == len(image), f'{profile}: binary identity mismatch')
    return manifest, written


def verify_plugin(root, build):
    manifest = json.loads((build / 'manifest.json').read_text())
    require(manifest.get('manifest_schema') == 1 and manifest.get('plugin_tests_passed') is True,
            'plugin: missing successful build/test identity')
    require(manifest.get('source_sha256') == plugin_source_hashes(root),
            'plugin: stale or incomplete source identity')
    require(manifest.get('artifacts_sha256') == hashes(build, [build / 'TU744.dll',
                                                            build / 'build.log']),
            'plugin: DLL/log identity mismatch')
    require(bool(manifest.get('sdk_sha256')) and bool(manifest.get('toolchain_sha256')),
            'plugin: missing SDK/toolchain identity')
    return manifest
