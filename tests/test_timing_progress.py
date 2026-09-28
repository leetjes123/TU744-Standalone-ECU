"""Compatibility entry point for the OEM-style scheduler tests.

The former per-tooth/frozen-timer test is retained in
docs/audits/tu5jp-standalone-oem-scheduler-2026-09-22/previous-test_timing_progress.py.
"""
from pathlib import Path
import runpy

if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).with_name("test_oem_scheduler_faults.py")), run_name="__main__")
