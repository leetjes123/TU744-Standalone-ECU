# Firmware 0.0.4

Branch: `firmware/0.0.4`. Calibration schema 5, 3072 bytes, unchanged from
0.0.3, so no migration is needed. Identity: `TU744 0.0.4`. The live frame
(command `13 04`) is unchanged.

Includes Tuning Wizard 0.0.4. The Windows executable is at
`wizard/TuningWizard_0.0.4.exe` in the release package.

## Firmware changes

### Fan preload is held as a reserve

The radiator fan can be switched by a physical thermoswitch instead of the ECU.
In that case the fan idle-up (`0x8C0`, fan on/off `0x5E2`/`0x5E3`) starts before
the fan actually draws current. In PI idle mode the integral used to remove the
extra steps while the engine idled above target. When the fan then switched on,
the reserve was gone and idle dropped.

Now:

- While the fan is not requested, the last in-deadband idle integral is
  recorded as the settled value.
- When the fan request starts, that settled value becomes a floor. Feedback can
  still open the valve further, but it cannot close the integral below the
  floor. The feedforward steps stay in as reserve.
- The floor is released when the fan request ends, and the settled value is
  forgotten when the engine stops.

Test: `test_fan_reserve` (`tests/test_core.c`).

### Faster idle stepper

Each IAC step needs two driver services, drive and then hold, at least 5 ms
apart. The foreground serviced the stepper once per pass using the pass-start
time. A pass takes about 10–12 ms, so the stepper moved only about 40–50
steps/s while running.

The stepper is now serviced before and after the diagnostics, each time with a
fresh timestamp (`src/runtime.c`). The 5 ms minimums are unchanged, so the
running rate can reach the homing rate of 100 steps/s but never exceed it.

This was not measured on the engine. Check that warm idle returns to the same
IAC position and integral after several throttle blips, and that no idle-valve
faults appear. Drift or new faults would mean lost steps.

## Tuning Wizard 0.0.4

Autotune was reworked. See the operator manual, section *Autotune suggestions*.

- **Feedback modes:** wideband open loop, wideband trim-compensated, and
  narrowband trim learning over complete lean/rich switching cycles. Auto picks
  the mode from the oxygen input setting.
- **Lambda delay:** RPM-dependent attribution, `base + cycles × 120 / RPM`.
- **Filters and confidence:**
  - configurable sample filters, settle time after transients, and outlier
    rejection;
  - a live requirements checklist;
  - counts of rejected samples by reason.
- **Proposal limits:**
  - change resistance;
  - an authority limit from the baseline VE;
  - optional step limit, deadband, VE bounds and smoothing;
  - Conservative, Normal and Aggressive presets.
- **VE map:** several views, plus cell locks saved by the Wizard for each
  calibration file. Cells at the authority limit are outlined red, and cells
  blocked by resolution are outlined violet.
- **Optional auto-apply** while Live tuning is on.
- **Log analysis:** **Analyse log...** replays a recorded CSV log through the
  same engine.

Live-data signal decoding now lives in `monitor_channels.cpp`, so logging and
autotune share it.

## Basemap

`TU5JP_1.6_8v_speed_density_v0.0.4.bin` is identical to the 0.0.3 basemap. The
fan reserve only takes effect with PI idle (`0x5D8` = 1) and a fan idle-up
(`0x8C0`) above zero.

## Verification

Passed:

- Firmware native tests and stock-native tests.
- Keil C166 `stock-95080` build: 0 warnings, 0 errors.
- Flash image packaging: HEX/BIN round trip and native calibration validation.
- Wizard CTest: 5 of 5 suites passed.

Not run: the target simulations, Keil knock runs and OEM scheduler tests,
which need `TU744_OEM_REPO`; and the protocol test. `verification/NOT-RUN.txt`
lists them. No ECU was flashed by the
release process, and the faster stepper has not been validated on the engine.
