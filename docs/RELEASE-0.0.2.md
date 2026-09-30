# Firmware 0.0.2

Branch: `firmware/0.0.2`. Calibration schema 5, 3072 bytes. Identity: `TU744 0.0.2`.

Includes Tuning Wizard 0.0.2, with the matching schema-5 calibration controls and
knock telemetry. The Windows executable is at `wizard/TuningWizard.exe` in the
release package.

## Changes

- Idle and DFCO share the calibrated closed-throttle flag; invalid TPS permits neither.
- Closed-throttle idle feedback continues above target + 500 RPM with DFCO off,
  retaining bumpless integral entry. The example idle-spark table's reversed sign
  is corrected so overspeed retards timing.
- DFCO retains simple entry RPM and valid coolant >=60 C. MAP no longer gates
  it; 5EB is reserved. Exit margin is at 93E (BE u16, 0..2000 RPM; zero retains
  the previous 200 RPM behavior).
- The new TU5JP basemap uses 100 ms DFCO delay and 200 RPM exit margin.
- [Global knock control](KNOCK.md) adds OEM-seeded gain, two filter bands,
  window/threshold/load curves, attack/maximum/recovery controls, bench jobs and
  matching Wizard/TunerPro telemetry. Default disabled.

Idle spark is still a correction to base timing. The OEM's approximately
10-degree-ATDC overspeed behavior is not imposed as an absolute timing clamp;
existing base maps may need further idle ignition tuning.

## Migration and artifacts

Run `python tools/migrate_schema5.py old-schema4.bin new-schema5.bin`.
It refuses an existing destination, preserves existing tuning (including DFCO
delay), initializes the new margin/knock block, and leaves knock disabled. Use
the matching schema-5 firmware, Wizard and definitions. Old schema-4 definitions
remain available for old firmware.

`basemaps/TU5JP_1.6_8v_speed_density_v0.0.2.bin` derives from the released 0.0.1
tune. User basemaps and existing Wizard design changes were retained. Local
delivery artifacts are under `release/TU744-v0.0.2/`, with hashes/manifests.

## Verification

Run from the repository root. OEM tests need `TU744_OEM_REPO` or `--oem-repo`;
Wizard builds need the MSVC developer environment.

```
python tools/build.py native
python tools/build.py stock-native
python tools/build.py oem-library
python tests/test_protocol.py
python tests/test_migration.py
python tests/test_knock_oem.py
python tools/build.py stock-95080
python tools/build.py keil
python tests/test_knock_target.py --stock-95080
python tests/test_knock_windows.py --stock-95080
python tests/keil_knock_detection.py
python tests/test_knock_boot_target.py --stock-95080
python tests/test_tunerpro_definitions.py
python tests/test_tunerpro_logging.py
python tools/verify_artifacts.py --profile stock-95080 --skip-legacy
ctest --test-dir wizard/build --output-on-failure
```

`build/keil-knock-detection/results.json` separates blocked native converter
completion from passing completion-model tests. `build/knock-windows/results.json`
records capture/coil/gate behavior and image hash. Build manifests bind sources,
toolchain and linked images. These software checks do not establish physical
production acceptance. No ECU was flashed; physical bench and independently
instrumented engine validation remain open.
