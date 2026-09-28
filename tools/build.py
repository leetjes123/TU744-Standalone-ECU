"""Reproducible, local-only C166 or native build. Never touches the legacy tree."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from artifacts import (PROFILES, binary_image, build_input_hashes, hashes,
                       programmed, require, source_hashes)

ROOT = Path(__file__).resolve().parents[1]
KEIL = Path(os.environ.get('KEIL_C166', str(Path.home() / 'AppData/Local/Keil_v5/C166')))

def run(args, cwd, log):
    result = subprocess.run([str(a) for a in args], cwd=cwd, capture_output=True, text=True)
    log.write(result.stdout + result.stderr)
    if result.returncode or re.search(r'\*\*\* ERROR', result.stdout + result.stderr):
        print(result.stdout + result.stderr)
        raise SystemExit(result.returncode or 1)

def native(stock=False):
    out = ROOT / ('build/stock-native' if stock else 'build/native')
    out.mkdir(parents=True, exist_ok=True)
    sources = [*sorted((ROOT / 'src').glob('*.c')), ROOT / 'tests/hal_fake.c', ROOT / 'tests/test_core.c', ROOT / 'tests/test_lifecycle.c', ROOT / 'tests/test_faults.c']
    if stock:
        sources = [*sorted((ROOT / 'src').glob('*.c')), ROOT / 'tests/hal_fake.c', ROOT / 'tests/test_stock_storage.c', ROOT / 'tests/test_lifecycle.c']
    else:
        sources.append(ROOT / 'tests/test_audit_regressions.c')
    if os.name == 'nt':
        vc = Path(os.environ.get('VCVARS64', r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'))
        # The batch is generated only from absolute workspace/toolchain paths.
        command = '@echo off\ncall "' + str(vc) + '" >nul\ncl /nologo /W4 /WX /std:c11 /Zi /Od /I"' + str(ROOT / 'include') + '" '
        if stock: command += '/DSTOCK_95080=1 '
        command += ' '.join('"' + str(s) + '"' for s in sources) + ' /Fe:core_tests.exe\n'
        (out / 'compile.cmd').write_text(command)
        args = ['cmd.exe', '/d', '/c', str(out / 'compile.cmd')]
    else:
        args = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g', *(['-DSTOCK_95080=1'] if stock else []), '-I', ROOT / 'include', *sources, '-o', out / 'core_tests']
    with (out / 'build.log').open('w') as log:
        run(args, out, log)
        run([out / ('core_tests.exe' if os.name == 'nt' else 'core_tests')], out, log)
        if os.name == 'nt':
            (out/'ssc.cmd').write_text('@echo off\ncall "'+str(vc)+'" >nul\ncl /nologo /W4 /WX /std:c11 /Od /I"'+str(ROOT/'include')+'" "'+str(ROOT/'tests/test_ssc.c')+'" "'+str(ROOT/'src/iac_hold.c')+'" /Fe:ssc_tests.exe\n')
            if stock:
                p = out/'ssc.cmd'
                p.write_text(p.read_text().replace('cl /nologo', 'cl /nologo /DSTOCK_95080=1'))
            run(['cmd.exe','/d','/c',out/'ssc.cmd'],out,log)
        else:
            run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*(['-DSTOCK_95080=1'] if stock else []),'-I',ROOT/'include',ROOT/'tests/test_ssc.c',ROOT/'src/iac_hold.c','-o',out/'ssc_tests'],out,log)
        run([out/('ssc_tests.exe' if os.name=='nt' else 'ssc_tests')],out,log)
        engine_sources = [ROOT/'tests/test_engine_outputs.c', ROOT/'src/safety.c',
                          ROOT/'src/rotation.c', ROOT/'src/math.c', ROOT/'src/oem_timing.c',
                          ROOT/'src/oem_ignition.c']
        if os.name == 'nt':
            (out/'engine.cmd').write_text('@echo off\ncall "'+str(vc)+'" >nul\n'
                'cl /nologo /W4 /WX /std:c11 /Od /I"'+str(ROOT/'include')+'" '+
                ' '.join('"'+str(s)+'"' for s in engine_sources)+' /Fe:engine_tests.exe\n')
            run(['cmd.exe','/d','/c',out/'engine.cmd'],out,log)
        else:
            run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                 '-I',ROOT/'include',*engine_sources,'-o',out/'engine_tests'],out,log)
        run([out/('engine_tests.exe' if os.name=='nt' else 'engine_tests')],out,log)
    print((out / 'build.log').read_text())

def keil(experimental=False, stock=False):
    out = ROOT / ('build/stock-95080' if stock else ('build/engine-experimental' if experimental else 'build/c166'))
    out.mkdir(parents=True, exist_ok=True)
    sources = [*sorted((ROOT / 'src').glob('*.c')), *sorted((ROOT / 'target/c167').glob('*.c'))]
    before = source_hashes(ROOT)
    inputs_before = build_input_hashes(ROOT)
    toolchain = hashes(KEIL, [KEIL / name for name in
                       ['BIN/C166.EXE', 'BIN/A166.EXE', 'BIN/L166.EXE', 'BIN/OH166.EXE', 'LIB/C167L.LIB']])
    # A failed/interrupted rebuild must not leave a previously valid manifest.
    (out / 'manifest.json').unlink(missing_ok=True)
    with (out / 'build.log').open('w') as log:
        for source in sources:
            defines = ['DEFINE(BOARD_RELEASED=1,STOCK_95080=1)'] if stock else (['DEFINE(BOARD_RELEASED=1)'] if experimental else [])
            run([KEIL/'BIN/C166.EXE', source, 'LARGE', 'MOD167', 'DEBUG', 'OPTIMIZE(4,SPEED)', *defines,
                 f'INCDIR({ROOT / "include"},{ROOT / "target/c167"})', f'OBJECT({source.stem}.obj)', f'PRINT({source.stem}.lst)'], out, log)
        assembly=sorted((ROOT/'target/c167').glob('*.A66'))
        for source in assembly:
            run([KEIL/'BIN/A166.EXE', source, 'SET(LARGE)', f'OBJECT({source.stem}.obj)', f'PRINT({source.stem}.lst)'], out, log)
        objects = [s.stem+'.obj' for s in assembly] + [s.stem+'.obj' for s in sources]
        ranges = '0X0-0XDFFF,0X10000-0X4FFFF' if stock else '0X0-0XDFFF,0X10000-0X6FFFF'
        classes = [f'{c} ({ranges})' for c in ('FCODE','FCONST','HCONST','XCONST')]
        classes += ['ICODE (0X0-0XDFFF)', 'NCONST (0X4000-0X7FFF)']
        classes += [f'{c} (0X380000-0X383FFF)' for c in ('NDATA','NDATA0','FDATA','FDATA0','HDATA','HDATA0','XDATA','XDATA0')]
        # PEC buffers must remain in always-visible IRAM. Startup leaves the
        # optional XRAM/XBUS disabled; reserve the region below system stack.
        classes += [f'{c} (0XF600-0XF7FF)' for c in ('SDATA','SDATA0')]
        classes += [f'{c} (0XF600-0XFDFF)' for c in ('IDATA','IDATA0')]
        command = ','.join(objects)+' TO TU5JP PRINT(TU5JP.m66) CLASSES ('+', '.join(classes)+') CINITTAB (0X10000-'+('0X4FFFF' if stock else '0X6FFFF')+')\n'
        (out/'firmware.lnp').write_text(command)
        run([KEIL/'BIN/L166.EXE', '@firmware.lnp'], out, log)
        run([KEIL/'BIN/OH166.EXE', 'TU5JP', 'H167'], out, log)
    profile = out.name
    limit, enabled, eeprom, storage, binary = PROFILES[profile]
    image = binary_image(programmed(out / 'TU5JP.H86', limit))
    require('0 WARNING(S),  0 ERROR(S)' in (out / 'TU5JP.m66').read_text(), 'linker warnings/errors')
    require(source_hashes(ROOT) == before and build_input_hashes(ROOT) == inputs_before,
            'source/build inputs changed during build; rebuild required')
    require(hashes(KEIL, [KEIL / name for name in toolchain]) == toolchain,
            'toolchain changed during build; rebuild required')
    metadata = dict(manifest_schema=2, profile=profile, engine_outputs_enabled=enabled,
                    eeprom_bytes=eeprom, calibration_storage=storage,
                    hardware_validated=False, engine_validated=False,
                    exact_oem_dtc_parity=False, calibration_included=False,
                    source_tree_unchanged_during_build=True,
                    limitations='See docs/EXPERIMENTAL.md and docs/RELEASE.md.',
                    source_sha256=before, build_inputs_sha256=inputs_before,
                    toolchain_sha256=toolchain)
    artifacts = ['TU5JP.H86', 'TU5JP.m66', 'firmware.lnp', 'build.log']
    if binary:
        (out / binary).write_bytes(image)
        artifacts.append(binary)
        metadata.update(binary_sha256=hashlib.sha256(image).hexdigest(), binary_bytes=len(image))
        print('OUTPUT-ENABLED EXPERIMENTAL IMAGE; hardware/engine acceptance incomplete: '+str(out / binary))
    metadata['artifacts_sha256'] = hashes(out, [out / name for name in artifacts])
    (out / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print((out / 'build.log').read_text())

def uvision():
    if os.name != 'nt':
        raise SystemExit('The uVision build requires Windows.')
    out = ROOT / 'build/uvision'
    out.mkdir(parents=True, exist_ok=True)
    log = out / 'build.log'
    log.unlink(missing_ok=True)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    # This exact batch invocation is checked against the command-line HEX.
    # -j0 is required for the unattended invocation used in this workspace.
    result = subprocess.run([str(KEIL.parent / 'UV4/UV4.exe'), '-r', str(ROOT / 'TU5JP.uvproj'),
                             '-j0', '-o', str(log)], cwd=ROOT, startupinfo=startup, timeout=60)
    if result.returncode:
        raise SystemExit(result.returncode)
    print(log.read_text())
    subprocess.run([sys.executable, str(ROOT / 'tools/verify_artifacts.py'), '--uvision'], check=True)

def oem(run_tests=True):
    out=ROOT/'build/oem';out.mkdir(parents=True,exist_ok=True)
    sources=[*sorted((ROOT/'src').glob('*.c')),ROOT/'tests/hal_fake.c',ROOT/'tests/protocol_bridge.c']
    exports=['oem_angle_split','oem_angle_refine','oem_ignition_segment','oem_dwell_update','oem_dwell_stock_update',
             'oem_adc_publish','oem_diagnostics_adc',
             'oem_coolant_update','oem_coolant_init','oem_coolant_capture','oem_coolant_reset',
             'oem_mil_update','oem_mil_on','oem_mil_off','oem_mil_clear','dtc_report_word','dtc_ingest_action',
             'oem_dtc_remove','oem_dtc_phase','oem_dtc_aggregate',
             'oem_dtc_assert','oem_dtc_recover','oem_dtc_complete','oem_dtc_subtype','oem_dtc_ingest',
             'oem_dtc_age','oem_dtc_clear_worker',
             'oem_dtc_cycle_begin','oem_dtc_clear_all','oem_dtc_clear_emissions','oem_dtc_clear_event',
             'oem_dtc_maintain',
             'oem_dtc_drive_init','oem_dtc_drive_update','oem_dtc_drive_clear',
             'oem_dtc_warmup_init','oem_dtc_warmup_update','oem_dtc_warmup_clear','oem_dtc_clock',
             'oem_iat_init','oem_iat_reset','oem_iat_update','oem_iat_capture',
             'oem_engine_init','oem_engine_update','oem_context_update',
             'oem_voltage_init','oem_voltage_reset','oem_voltage_base','oem_voltage_filter',
             'oem_voltage_alternate','oem_voltage_update','oem_diagnostics_voltage_base','oem_diagnostics_voltage',
             'oem_vss_reset','oem_vss_update','oem_diagnostics_vss',
             'oem_vss_input_init','oem_vss_input_capture','oem_vss_input_update','oem_diagnostics_vss_input',
             'oem_digital_init','oem_digital_filter','oem_digital_publish','oem_digital_aux',
             'oem_diagnostics_digital','oem_diagnostics_digital_aux',
             'oem_history_encode','oem_history_validate','oem_history_decode','oem_diagnostics_bind',
             'oem_rotation_threshold','oem_rotation_reset','oem_rotation_capture',
             'oem_rotation_period','oem_rotation_speed',
             'oem_cadence_init','oem_cadence_step','oem_cadence_rejected',
             'iac_hold_start','iac_hold_stop','iac_hold_fail','iac_hold_watch','iac_hold_complete',
             'oem_diagnostics_sensors','oem_diagnostics_capture','oem_diagnostics_context',
             'oem_diagnostics_engine','oem_diagnostics_demand','oem_diagnostics_clear_ported',
             'oem_readiness_init','oem_readiness_update','oem_readiness_clear','oem_diagnostics_readiness',
             'reset_capture_raw','reset_capture_marker','bridge_reset','bridge_exchange','bridge_sync_losses','fake_firmware_updates DATA']
    if os.name=='nt':
        vc=Path(os.environ.get('VCVARS64',r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'))
        (out/'oem.def').write_text('EXPORTS\n'+'\n'.join(exports)+'\n')
        (out/'compile.cmd').write_text('@echo off\ncall "'+str(vc)+'" >nul\ncl /nologo /W4 /WX /LD /Od /I"'+str(ROOT/'include')+'" '+' '.join('"'+str(s)+'"' for s in sources)+' /Fe:oem.dll /link /DEF:oem.def\n')
        args=['cmd.exe','/d','/c',out/'compile.cmd']
    else:
        args=['cc','-std=c99','-Wall','-Wextra','-Werror','-shared','-fPIC','-I',ROOT/'include',*sources,'-o',out/'oem.so']
    with (out/'build.log').open('w') as log:run(args,out,log)
    if not run_tests:
        print('Built native OEM/protocol test library')
        return
    run([sys.executable,ROOT/'tests/test_oem.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_records.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_inputs.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_adc.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_voltage.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_vss.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_vss_input.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_digital.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_readiness.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_history.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_rotation.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_timing.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_ignition.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_composed.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_oem_cadence.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_iac_hold.py'],ROOT,sys.stdout)
    run([sys.executable,ROOT/'tests/test_protocol.py'],ROOT,sys.stdout)

if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('target',choices=['native','keil','oem','oem-library','uvision','engine-experimental','stock-95080','stock-native']);args=parser.parse_args()
    {'native':native,'keil':keil,'oem':oem,'oem-library':lambda: oem(run_tests=False),'uvision':uvision,
     'engine-experimental':lambda: keil(experimental=True), 'stock-95080':lambda: keil(stock=True), 'stock-native':lambda: native(stock=True)}[args.target]()
