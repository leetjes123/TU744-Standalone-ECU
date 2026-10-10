# Firmware 0.0.3

Branch: `firmware/0.0.3`. Calibration schema 5, 3072 bytes (unchanged from
0.0.2; no migration). Identity: `TU744 0.0.3`.

Includes Tuning Wizard 0.0.3. The Windows executable is at
`wizard/TuningWizard.exe` in the release package.

## Changes

### Idle valve no longer freezes after a single disturbance

Field log `ecu_log_20261007_150809.csv`: one crank sync loss at 78.5 s left
the idle valve at 45 steps for the rest of the run. On the re-crank that
followed, the start addition was never applied, and idle feedback stopped
although the engine idled 150 to 200 RPM above target for 157 s. Every
stepper-driver fault latched in `src/iac.c` until the ECU was reset (not
even a key cycle cleared it), and idle feedback only runs when the valve
is ready. The engine stalled at 235.6 s when the fan switched on: crank sync
was lost again, and that loss ended the run.

Now:

- A faulted driver is retried 200 ms after the fault, then at most once every
  10 s (`IAC_RETRY_MS`, `IAC_RETRY_SPACING_MS`).
- If the engine is running, the retry resumes from the counted position.
  Homing would drive the valve closed under a running engine. If the engine
  is stopped, the retry re-homes the valve.
- A retry needs key on, a valid tune, no service mode, no `INH_DEADLINE` or
  `INH_OUTPUT` inhibit, and a hold session that has released the SPI bus
  (new `hal_iac_hold_reset`). A sync-loss inhibit alone never blocks it.
- Key-on clears a fault from the previous drive and re-homes.
- `last_fault`, `fault_count` and the retry count keep the evidence.

Tests: `test_iac_recovery` (`tests/test_core.c`) and the hold-reset case in
`tests/test_ssc.c`.

### Sync-loss cause and idle detail in the live data

Each counted crank sync loss now records its cause and tooth:

| Code | Cause |
|---|---|
| 1 | gap too early |
| 2 | gap missing |
| 3 | irregular tooth |
| 4 | noise edge |
| 5 | capture missed edge |
| 6 | capture fault |

Codes 4 and 5 point to electrical interference on the crank input.

Command `13 04` returns a new 64-byte live frame, version 4. It carries the
idle valve target, state, fault, last fault, recovery and fault counts, the
L9935 response, idle mode, integral and spark correction, the sync-loss cause
and tooth, the first inhibit reason, and the crank capture resync and overrun
counters. A bare `13` still returns the unchanged 44-byte version 3 frame for
0.0.2 hosts. See [LIVE-MONITOR.md](docs/LIVE-MONITOR.md).

Wizard 0.0.3 requests version 4 and falls back to version 3 on older firmware.
It logs 15 new CSV columns, which are `nan` on older firmware, and shows the
idle valve status and the last sync-loss cause on the dashboard.

Tests:

- `test_rotation_loss_reason`
- `tests/test_protocol.py` (version 4 layout, version 3 compatibility)
- `wizard/tests/port_tests.cpp` (decoding against the firmware parser, and
  the 0.0.2 fallback)
- `wizard/tests/graph_tests.cpp` (CSV round trip)

### Idle Steps mode applied the hidden P gain

In idle mode 0 (Steps), the proportional term `error x 0x630 / 10000` was
still added to the valve command. The Wizard hides that gain unless PI is
selected. Steps mode now uses a P term of 0, including in the bumpless
re-seed. Test: `test_idle_step_mode`.

### Closed-loop fuel trim switch

- Disabling STFT (`0x5D4` bit 5) now also resets the trim integrator, so
  re-enabling it starts neutral.
- Live data reports a neutral trim while STFT is disabled.
- In the Wizard, the enable switch stays editable while the settings that
  depend on it are disabled.

## Not changed

Firmware cannot make crank sync tolerate interference. Accepting an extra
edge would put spark at the wrong angle, so noise still revokes sync by
design. In the field log, sync was lost about 60 ms after the radiator fan
switched on, with 14.3 V battery. Check these:

- the crank sensor shield and routing;
- suppression on the fan relay;
- ECU and sensor grounds.

The new sync-loss cause and capture counters show whether a fix worked.

## Artifacts

`basemaps/TU5JP_1.6_8v_speed_density_v0.0.3.bin` is the 0.0.2 basemap,
unchanged. Schema 5 tunes from 0.0.2 load as-is.

## Verification

Run from the repository root with `TU744_OEM_REPO` set. The
Wizard needs the MSVC x64 environment.

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
python tools/verify_artifacts.py --profile stock-95080 --skip-legacy
ctest --test-dir wizard/build --output-on-failure
python tools/package_003.py
```

For this package the simulator lane was stopped on request before it finished:
`verification/NOT-RUN.txt` lists the target tests without a passing log, and
`verification/` holds the logs of everything that passed (native, stock-native,
protocol, migration, knock, Wizard, SSC/IAC hold and the completed
target tests).

These are software and simulator checks. No ECU was flashed. Bench and engine
validation of the idle valve recovery remain open.
