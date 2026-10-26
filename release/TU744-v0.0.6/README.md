# Firmware 0.0.6

Calibration schema 5, 3072 bytes, unchanged from 0.0.5, so no migration is
needed. Identity: `TU744 0.0.6`. The live frame (command `13 04`) is unchanged.

The package ships Tuning Wizard 0.0.5 as `wizard/TuningWizard_0.0.5.exe`.

## Tuning Wizard 0.0.5

Live logging no longer drops samples. Before, the Wizard kept only the newest
live frame and the screen picked it up once per redraw, so frames that arrived
while the next request was in flight were lost and rows were timestamped at
redraw time. Logs showed irregular gaps (0.17-0.6 s, sometimes more) that were
not real. Now every received frame is queued and stamped when its reply
arrived.

Two new log channels show link health: **Link Failures** (failed live-frame
requests since connecting) and **Link Last Failure** (1 no reply, 2 serial
error, 3 echo mismatch, 4 bad frame, 5 failed validation). Older CSV files
still open.

## Firmware changes

The idle stepper now drives like the OEM firmware. On the engine, the idle
stayed above target with the IAC at 0 steps, and a 52-step close barely changed
MAP or rpm. The OEM stepper code (TU5JP ROM, `sub_40FA6`, `sub_41040`,
`sub_41258`) was disassembled and these parts were ported.

### Drive current

The stepper is driven with the OEM phase bytes (ROM `0x15AD6` = `00,08,09,01`):
polarity bits only, current code `00` on both bridges. The phase cycle and
direction are unchanged.

The low-current hold modulation is no longer used. After a step, the last phase
stays energized at drive current, as on the OEM, which sends nothing while at
target. The response check after each step re-sends the same byte, so the
current does not drop.

### Step speed and reversal wait

- A closing step takes 5 ms and an opening step after an opening step 4 ms. The
  OEM calls its stepper on every 5th ADC completion, or every 4th after an
  opening step. Before, a step took at least 10 ms.
- A step against the previous direction waits 25 ms. The OEM waits five
  stepper calls (counters `92BB`/`92BC`).
- Homing is unchanged: 250 closing steps at 10 ms.

One count is one full step: each count change moves the OEM phase index by
exactly one table entry.

### Stuck-high position re-sync

After 5 s of idle feedback at least 120 rpm above target, the counted position
gains 5 counts, so the valve is driven 5 steps further closed than the count
says. This happens at most 40 times per key cycle and corrects for lost steps.
These values are the OEM's (ROM `0x15AD2`, `0x159F8`, `0x10FB2`, `0x10FB0`).

The trigger differs from the OEM in one way. The OEM also requires the stepper
not to be at its target, plus a controller-state compare whose meaning is
unknown. TU744 requires the idle target position to be at or below the counted
position instead, so the re-sync also acts with the valve at 0 steps.

Tests: `test_iac_direction_and_status`, `test_iac_oem_timing` and
`test_iac_resync` (`tests/test_core.c`).

## Basemaps

New basemaps replace the previous one; both are schema 5 and pass the
firmware's calibration validation.

- `TU5JP_1.6_8v_speed_density_v0.0.6.bin`: TU5JP 1.6 8v. Built into the
  firmware image.
- `TU3JP_1.4_8v_speed_density_v0.0.6.bin`: TU3JP 1.4 8v. Load it with the
  Wizard (**Open file**, **Write active tune**, **Save tune to ECU flash**).

The firmware code is unchanged; only the built-in calibration slot of
`TU744_0.0.6_M95080.bin` differs from the first 0.0.6 package.

## Verification

Passed:

- Firmware native tests and stock-native tests.
- Keil C166 `stock-95080` build: 0 warnings, 0 errors.
- Flash image packaging: HEX/BIN round trip and native calibration validation.

The foreground target simulation was run and fails with `cal_valid 0`.
Firmware 0.0.5 fails the same way, so this is not caused by this release.

Not run: the other target simulations, Keil knock runs and OEM scheduler tests;
the protocol test; the Wizard port tests and a Windows GUI smoke test of Wizard
0.0.5. `verification/NOT-RUN.txt` lists them. Wizard 0.0.5 graph, diagnostic
and monitor-queue tests passed.

The OEM step timing assumes the OEM ADC scan completes about every 1 ms. That
comes from static analysis and was not timed in the simulator. The absolute coil
current of current code `00` on the L9935 was not confirmed. No ECU was flashed
by the release process, and nothing in this release has been validated on the
engine. Check that:

- MAP and rpm respond clearly when the IAC moves;
- idle reaches target without the IAC sitting at 0;
- the valve and ECU do not run hot after a long idle, because the coil now stays
  energized.
