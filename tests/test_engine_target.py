"""Compatibility entry point for the OEM-style scheduler tests.

The former per-tooth/frozen-timer test is retained in
docs/audits/tu5jp-standalone-oem-scheduler-2026-09-22/previous-test_engine_target.py.
"""
from pathlib import Path
import runpy
import subprocess
import sys

if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).with_name("test_oem_scheduler_target.py")), run_name="__main__")
    if '--probe' not in sys.argv:
        subprocess.run([sys.executable, str(Path(__file__).with_name('test_scheduler_regressions.py')),
                        *(['--stock-95080'] if '--stock-95080' in sys.argv else [])], check=True)
        subprocess.run([sys.executable, str(Path(__file__).with_name('test_output_closure_target.py')),
                        *(['--stock-95080'] if '--stock-95080' in sys.argv else [])], check=True)
