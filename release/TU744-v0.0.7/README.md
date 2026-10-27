# Firmware 0.0.7

Calibration schema 5, 3072 bytes, unchanged from 0.0.6, so no migration is
needed. Identity: `TU744 0.0.7`. The live frame (command `13 04`) is unchanged.

The package ships Tuning Wizard 0.0.6 as `wizard/TuningWizard_0.0.6.exe`.

These changes come from the TU5JP road and cold-start logs of October 2026.

## Firmware changes

### Sync loss recovery window

New setting **Sync loss recovery window** (Engine state, offset `0x96C`,
0–2000 ms). Before, any crank sync loss while running was treated as a new
start: cranking fuel and advance, and after-start enrichment, idle start catch
and the STFT start delay all began again.

If the engine was running and sync returns within the window, it now goes
straight back to running and keeps its original start time. If the rpm is
below the cranking rpm at resync, it still goes to cranking. The sync-loss
counter still counts every loss.

0 keeps the 0.0.6 behaviour (every loss is a new start).

### Idle spark correction RPM limit

New setting **Idle spark correction RPM limit** (Idle, offset `0x96E`,
0–3000 rpm). Idle spark correction only applies up to idle target plus this
margin; above it, the correction is 0. When coasting down towards idle, the
engine keeps its normal advance instead of being retarded by idle feedback.

0 keeps the 0.0.6 behaviour (no limit).

Both settings can be changed with the engine running. The basemaps leave them
at 0.

### SERVICE and DEADLINE clear after a tool session

A tune save, knock bench run or refused firmware update sets SERVICE. The
foreground deadline could also latch during the session. Both used to stay set
until the ECU was reset.

Now they clear by themselves when the session ends, but only if the engine
stayed stopped and unsynced for the whole session. The ECU first waits for one
normal-length main loop pass, so the deadline does not latch again straight
away, then re-homes the idle valve. These still need a reset:

- a deadline that was already latched before the session;
- any session with crank rotation in it.

Tests: `test_sync_recovery`, `test_idle_spark_rpm` and `test_service_release`
(`tests/test_idle_dfco.inc`).

## Tuning Wizard 0.0.6

- Two new log channels:
  - **Dropped Frames** counts live frames that were discarded because the
    Wizard's receive queue was full. It is reset by **Clear**.
  - **Sample Gap** is the time in seconds since the previous logged row. The
    first row of a recording is 0, and clearing the history doesn't hide a gap.
- The thread that polls the ECU runs at above-normal priority, so logging keeps
  up when the Wizard window is in the background.
- The two new firmware settings are under **Idle** and **Engine state**. They
  need firmware 0.0.7; firmware 0.0.6 ignores them.

The log gaps of 3–26 s in the October road logs were recorded with a Wizard
older than 0.0.5. Wizard 0.0.5 already fixed the cause. With 0.0.6, any gap
that remains shows up in the log as Dropped Frames or Sample Gap.

## Basemaps

The TU5JP 1.6 and TU3JP 1.4 basemaps are the 0.0.6 files, unchanged. The
TU5JP map is built into `TU744_0.0.7_M95080.bin`.

## Verification

Passed:

- Firmware native tests and stock-native tests.
- Keil C166 `stock-95080` build: 0 warnings, 0 errors.
- Flash image packaging: HEX/BIN round trip and native calibration validation.
- Tuning Wizard 0.0.6 (MSVC, static runtime): graph, diagnostic, monitor-queue,
  port, calibration and protocol safety tests, generated definition checks and
  the Windows GUI smoke test.

Not run: the other target simulations, Keil knock runs and OEM scheduler tests,
and the protocol test. `verification/NOT-RUN.txt` lists them.

No ECU was flashed by the release process, and nothing in this release has been
validated on the engine. Check that:

- after a short sync loss while running, the engine does not stumble or go rich
  from cranking fuel;
- with the RPM limit set, coasting towards idle keeps normal advance and idle
  still holds its target;
- after a tune save with the engine off, SERVICE and DEADLINE clear without a
  key cycle and the idle valve homes.
