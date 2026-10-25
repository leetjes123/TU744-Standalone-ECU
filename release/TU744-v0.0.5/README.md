# Firmware 0.0.5

Calibration schema 5, 3072 bytes, unchanged from 0.0.4, so no migration is
needed. Identity: `TU744 0.0.5`. The live frame (command `13 04`) is unchanged.

The Tuning Wizard has no changes in this release. The package ships the 0.0.4
executable as `wizard/TuningWizard_0.0.4.exe`, and it works with this firmware.

## Firmware changes

### Idle valve fault 7 no longer latches on a healthy driver

The IAC hold compare runs at interrupt level 11. It has to wait for any
`hal_lock` section and for level-15 capture work. At 2700–3500 rpm, Keil
measured up to 221 ticks of lateness. Most of that came from injector
admission (about 85 µs) and the ignition segment pass (up to about 146 µs),
each stretched by capture interrupts. The late bound was 250 ticks, so a
healthy driver could latch idle-valve fault 7.

Two changes:

- **Shorter locked section for injector admission.** The scheduler now does
  the plan-derived admission checks (`injector_prepare`) before it takes the
  lock. Inside the lock, `injector_commit` only re-checks the plan age and
  whether the pair is busy, then starts the pair. Every write of inhibits,
  epoch or plan increments a new `generation` counter. If the generation has
  changed, the ticket is not used and the full `injector_admit` path runs
  instead, so admission decisions are the same as before. When the pair is
  idle, the injection is started straight away instead of going through the
  pending descriptor. `hal_injector_start` uses the 16-bit T7 distance instead
  of reading the 32-bit capture clock for each channel.
- **Late bound raised from 250 to 1250 ticks** (`HOLD_LATE_TICKS`). A late
  compare only stretches one hold phase. A stalled T7 or a stuck session is
  still caught by the 2 ms progress supervision in `board_ssc_tick`.

Test: `test_injector_ticket` (`tests/test_core.c`) covers stale tickets after
an inhibit, a plan republished between prepare and commit, and the plan-age
recheck. It also runs 200,000 randomized trials that check `injector_commit`
admits exactly what `injector_admit` admits from the same state.

## Basemap

`TU5JP_1.6_8v_speed_density_v0.0.5.bin` is identical to the 0.0.4 basemap.

## Verification

Passed:

- Firmware native tests and stock-native tests.
- Keil C166 `stock-95080` build: 0 warnings, 0 errors.
- Flash image packaging: HEX/BIN round trip and native calibration validation.

Not run: the target simulations, Keil knock runs and OEM scheduler tests,
which need `TU744_OEM_REPO`; the protocol test; and the Wizard CTest, because
the Wizard is unchanged. `verification/NOT-RUN.txt` lists them. Hold compare
lateness has not been measured again in Keil with the new admission path. No
ECU was flashed by the release process, and the fix has not been validated on
the engine. Check that idle-valve fault 7 no longer appears at 2700–3500 rpm.
