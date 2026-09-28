"""Build the Win32 TunerPro DLL against the user-provided, unmodified SDK."""
from pathlib import Path
import os
import json
import subprocess
from artifacts import hashes, plugin_source_hashes, require, sha256
ROOT=Path(__file__).resolve().parents[1]
SDK=Path(os.environ.get('TUNERPRO_SDK',str(ROOT.parents[1]/'third_party/tunerpro/sdk')))
OUT=ROOT/'build/tunerpro'
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'manifest.json').unlink(missing_ok=True)
vc=Path(os.environ.get('VCVARS32',r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat'))
if not (SDK/'ITPPlugin.h').is_file(): raise SystemExit('Set TUNERPRO_SDK to the extracted SDK folder.')
before = plugin_source_hashes(ROOT)
sdk_before = hashes(SDK, [p for p in SDK.rglob('*') if p.is_file() and p.suffix.lower() in ('.h', '.hpp', '.lib')])
batch='@echo off\ncall "'+str(vc)+'" >nul\n'
batch+='if errorlevel 1 exit /b 1\nwhere cl > toolchain.txt\nwhere link >> toolchain.txt\nwhere rc >> toolchain.txt\nif errorlevel 1 exit /b 1\n'
batch+='rc /nologo /fo plugin.res "'+str(ROOT/'tunerpro/plugin.rc')+'"\nif errorlevel 1 exit /b 1\n'
batch+='cl /nologo /W4 /WX /EHsc /std:c++17 /MT /LD /O2 /I"'+str(SDK)+'" "'+str(ROOT/'tunerpro/plugin.cpp')+'" plugin.res /Fe:TU744.dll /link /DEF:"'+str(ROOT/'tunerpro/plugin.def')+'" user32.lib advapi32.lib\n'
batch+='if errorlevel 1 exit /b 1\n'
sources=[*sorted((ROOT/'src').glob('*.c')),ROOT/'tests/hal_fake.c',ROOT/'tests/protocol_bridge.c']
batch+='cl /nologo /W4 /WX /MT /Od /DSTOCK_95080=1 /I"'+str(ROOT/'include')+'" /c '+ ' '.join('"'+str(p)+'"' for p in sources)+'\nif errorlevel 1 exit /b 1\n'
batch+='cl /nologo /W4 /WX /EHsc /std:c++17 /MT /Od /DSTOCK_95080=1 /I"'+str(SDK)+'" /I"'+str(ROOT/'include')+'" "'+str(ROOT/'tunerpro/test_plugin.cpp')+'" '+' '.join(p.stem+'.obj' for p in sources)+' user32.lib /Fe:plugin_tests.exe\nif errorlevel 1 exit /b 1\n'
# Explicit path: with NoDefaultCurrentDirectoryInExePath set, cmd does not
# search the current directory and reports the test as not found.
batch+='.\\plugin_tests.exe\n'
(OUT/'build.cmd').write_text(batch)
r=subprocess.run(['cmd.exe','/d','/c',str(OUT/'build.cmd')],cwd=OUT,capture_output=True,text=True)
(OUT/'build.log').write_text(r.stdout+r.stderr)
print(r.stdout+r.stderr)
if r.returncode == 0:
    require(plugin_source_hashes(ROOT) == before, 'plugin/test sources changed during build')
    require(hashes(SDK, [SDK / name for name in sdk_before]) == sdk_before, 'SDK changed during build')
    compilers = [Path(line.strip()) for line in (OUT/'toolchain.txt').read_text().splitlines() if line.strip()]
    require(all(p.is_file() for p in compilers) and {'cl.exe', 'link.exe', 'rc.exe'} <=
            {p.name.lower() for p in compilers}, 'could not identify native toolchain')
    metadata = dict(manifest_schema=1, plugin_tests_passed=True, source_sha256=before,
                    sdk_sha256=sdk_before,
                    toolchain_sha256={str(p): sha256(p) for p in compilers},
                    artifacts_sha256=hashes(OUT, [OUT/'TU744.dll', OUT/'build.log']))
    (OUT/'manifest.json').write_text(json.dumps(metadata, indent=2)+'\n')
raise SystemExit(r.returncode)
