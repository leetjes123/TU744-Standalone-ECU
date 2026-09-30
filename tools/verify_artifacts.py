"""Verify development artifacts without optimizable assertions. No release approval."""
from pathlib import Path
import argparse
import json
import re
from artifacts import PROFILES, programmed, require, sha256, verify_firmware

ROOT = Path(__file__).resolve().parents[1]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=PROFILES, default='c166')
    parser.add_argument('--uvision', action='store_true', help='also require identical uVision programmed bytes')
    parser.add_argument('--legacy-root', type=Path, help='override original standalone source location')
    parser.add_argument('--wizard-root', type=Path, help='override original Wizard source location')
    parser.add_argument('--skip-legacy', action='store_true',
                        help='explicitly record original-source isolation as NOT checked (portable checkout)')
    args = parser.parse_args(argv)
    out = ROOT / 'build'
    report_path = out / ('verification.json' if args.profile == 'c166' else f'verification-{args.profile}.json')
    report_path.unlink(missing_ok=True)
    require(not args.uvision or args.profile == 'c166', 'uVision comparison requires the c166 profile')
    require(not args.skip_legacy or not (args.legacy_root or args.wizard_root),
            'cannot combine skipped legacy verification with legacy paths')
    checked = {}
    if not args.skip_legacy:
        from oem_repo import oem_repo
        REPO = oem_repo()
        original = json.loads((REPO / 'docs/audits/tu5jp-standalone-2026-09-18/manifest.json').read_text())
        legacy = args.legacy_root or Path(original['standalone_root'])
        wizard = args.wizard_root or legacy.parent / 'LemonRacingWizard'
        for field, root in [('source_files', legacy), ('wizard_files', wizard)]:
            for name, metadata in original[field].items():
                require(sha256(root / name) == metadata['sha256'], f'original source changed: {root / name}')
            checked[field] = len(original[field])
    board = (ROOT / 'target/c167/board.c').read_text() + (ROOT / 'target/c167/knock_hw.c').read_text()
    irqs = re.findall(r'(\w+IC)\s*=\s*IRQ\((\d+),\s*(\d+)\)', board)
    require(bool(irqs), 'no IRQ assignments found')
    require(len({(level, group) for _, level, group in irqs}) == len(irqs), 'duplicate interrupt priority')
    require(len({name for name, _, _ in irqs}) == len(irqs), 'duplicate interrupt assignment')
    require('#define BOARD_RELEASED 0' in (ROOT / 'target/c167/board.h').read_text(),
            'default development output gate changed')
    build = out / args.profile
    manifest, written = verify_firmware(ROOT, build, args.profile)
    map_text = (build / 'TU5JP.m66').read_text()
    buffer = re.search(r'^\s+([0-9A-F]{6})H\s+SYMBOL\s+VAR\s+\S+\s+\S+\s+capture_buffer\b', map_text, re.M)
    require(buffer is not None, 'missing PEC capture buffer')
    buffer_at = int(buffer.group(1), 16)
    require(0xF600 <= buffer_at and buffer_at + 180 <= 0xF800,
            'PEC capture buffer outside reserved always-visible IRAM')
    vectors = {'capture_isr': 0x1F, 'engine_work_isr': 0x41,
               'coil0_isr': 0x16, 'coil1_isr': 0x14,
               'spark0_isr': 0x10, 'spark1_isr': 0x11,
               'injector0_isr': 0x45, 'injector1_isr': 0x3C,
               'injector2_isr': 0x44, 'injector3_isr': 0x37,
               'knock_reference_isr': 0x12, 'knock_window_isr': 0x30}
    for handler, vector in vectors.items():
        require(re.search(r'^' + handler + r'\s+' + str(vector) + r'\s', map_text, re.M) is not None,
                f'wrong/missing linked C167 vector for {handler}')
    size = re.search(r'Program Size:.*', (build / 'build.log').read_text())
    require(size is not None, 'missing linked program size')
    report = dict(profile=args.profile, board_released=manifest['engine_outputs_enabled'],
                  production_release_accepted=False, exact_oem_dtc_parity=False,
                  original_source_isolation='not_checked' if args.skip_legacy else 'verified',
                  original_files_unchanged=checked, irq_assignments=irqs, program_size=size.group(),
                  capture_buffer=hex(buffer_at), scheduler_vectors=vectors,
                  hex_sha256=sha256(build / 'TU5JP.H86'), highest_flash_byte=hex(max(written)),
                  source_sha256=manifest['source_sha256'], build_inputs_sha256=manifest['build_inputs_sha256'],
                  toolchain_sha256=manifest['toolchain_sha256'])
    if args.uvision:
        uvhex = out / 'uvision/TU5JP.H86'
        require(programmed(uvhex) == written, 'uVision programmed bytes differ')
        uvlog = (out / 'uvision/build.log').read_text()
        require('0 Error(s), 0 Warning(s).' in uvlog, 'uVision warnings/errors')
        uvsize = re.search(r'Program Size:.*', uvlog)
        require(uvsize is not None and size.group() == uvsize.group(), 'uVision program size differs')
        report.update(uvision_hex_sha256=sha256(uvhex), uvision_programmed_bytes_identical=len(written))
        print(f'PASS all {len(written)} programmed flash bytes identical between command-line and uVision builds')
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    if args.skip_legacy:
        print('NOT CHECKED: original standalone/Wizard isolation (--skip-legacy)')
    else:
        print('PASS original standalone/Wizard source hashes')
    print(f'PASS {args.profile} source/build/artifact identities, IRQ priorities and linked flash ranges')
    print('Development artifact verification only; production release is NOT accepted.')
    print(size.group())


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        raise SystemExit(f'FAIL: {error}') from error
