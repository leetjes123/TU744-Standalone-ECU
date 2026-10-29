# Firmware 0.0.8

Calibration schema 5, 3072 bytes, unchanged: the new settings use free bytes
`0x970..0x977` (`0x972..0x973` reserved), so no migration is needed.
Identity: `TU744 0.0.8`. The live frame gains version 5 (`13 05`). Versions 3
and 4 are unchanged.

The package ships Tuning Wizard 0.0.7 as `wizard/TuningWizard_0.0.7.exe`.
Everything below is visible and tunable in the Wizard; no command-line tools
are needed.

## Tuning Wizard 0.0.7

- **Output inhibits on the dashboard.** A banner at the top always shows
  whether the outputs are enabled (green), waiting with the engine stopped,
  inhibited by a condition (amber) or latched (red). Each inhibit appears as
  a chip. Hovering it shows what it blocks, whether it latches, how it
  clears and the live detail: last sync-loss cause, trip source and automatic
  clears this key cycle. The bottom monitor row names the inhibits on every
  tab.
- **Live frame version 5.** The Wizard reads the trip source and the output
  event counters, and falls back to version 4 on firmware 0.0.7. New log
  columns: Output Trip Source, Output Latch Clears, Coil Watchdog Trips,
  Clamped Injector Pulses and Late Output Events.
- **Developer mode** (View menu, remembered). Adds the Developer tab with
  live output health, timing health (command `2F 02`, with optional reset of
  the maxima) and the crank capture snapshot. For a missed edge it lists the
  30 capture times, marks the doubled interval and states how many teeth
  before the block end the edge was lost.
- **Knock sensing in the Developer tab.** The ECU does not store knock
  faults and clears them when the engine stops, so with developer mode on
  the Wizard reads the cause (command `34`) as soon as the live *Knock
  Sensor Fault* flag appears and keeps it: signal at rail (open or shorted
  sensor circuit), null level, test pulse, drift, ADC, window, stale sample
  or reference. The tab also shows the knock level, reference, gain, ratio
  and threshold, and runs the engine-off knock self-test and bench sampling
  (command `35`), which were command-line only before.
- **New settings.** The four firmware settings below are under Idle, Ignition
  and Rev limit and fuel cut, with the Wizard validator kept in step with the
  firmware.

## Basemaps 0.0.8

`TU5JP_1.6_8v_speed_density_v0.0.8.bin`, built into the firmware image, and
`TU3JP_1.4_8v_speed_density_v0.0.8.bin` are the 0.0.6 maps with the new
idle settings:

- idle feedback window 300 rpm;
- retard limit while falling 2°.

Overrun coolant and cranking dwell stay at 0, which means 60 °C and
3500 µs. Both maps pass the firmware calibration validator.

## Guiding principle: no latches the OEM firmware doesn't have

Every latching inhibit was checked against the OEM TU5JP ROM. In the ROM, the
only output shutdowns are the immobilizer (`82DC=FFFF`, which stops ignition
scheduling at 37D94 and the other exits) and the run-permission input
(`2BE0A`). It has no latch for a timing overrun, a capture fault or a coil
dwell timeout. A fault is handled per event instead.

| Inhibit | Before 0.0.8 | 0.0.8 |
|---|---|---|
| `INH_OUTPUT` | Latched until reset. Set by a coil watchdog trip, a late injector start, an injector pulse over 25 ms, or a driver refusal. | Latched only for a driver refusal (`TRIP_COIL_REFUSED`/`TRIP_INJECTOR_REFUSED`, which means the board is not released) or for 3 coil watchdog trips in a row on one coil while running (`TRIP_COIL_WATCHDOG`, to protect the hardware). It clears after a stall (see below). |
| `INH_DEADLINE` | Latched. Set by a tick gap, a foreground gap, a capture failure, a stale block or a late event. | A per-pass condition. The tick gap (`TRIP_TICK_GAP`) and foreground gap (`TRIP_FOREGROUND_GAP`) clear on the next full control pass. Stale blocks and late events skip that event and are counted in `late_events`. |
| `INH_SERVICE` | Latched. Cleared by `service_poll`. | Unchanged. It is a tool session, not a fault. |
| `INH_BOARD` | Latched. | Unchanged. The board is not released, which is a build setting. |
| `capture_failed` | Turned off CC15/PEC and latched DEADLINE. | Removed. A starved worker or a failed acquisition seed takes the same discard-and-reseed path as a missed edge (sync-loss reason 6). The OEM likewise resyncs, at XP1 2B18C. |
| IAC fault 7 | Set whenever DEADLINE or OUTPUT was set. | Set only for a real hold timing failure (stopped T7, late hold service). A foreground deadline still switches the bridges off, but records no fault. An output latch leaves idle control running: OEM 2BE0A doesn't touch the SSC. |

## 1. Starting and output latches

- **Cranking dwell.** While the engine is not running, the plan limits dwell
  to the **Cranking dwell limit** (`0x976`). 0 means 3500 µs; the allowed
  range is 1000–4000 µs. Dwell feedback can't lengthen it. Charge start
  stays angle-based on the measured tooth period, as in the OEM. A cranking
  charge is skipped (and counted as a late event) if its stored angle fire,
  stretched 1.5× by a slowdown on compression, would land more than 6 ms
  after charge start. The 6.5 ms watchdog stays as a backstop.
- **Coil watchdog.** A trip switches that coil off and drops its stored
  fire, so at most one spark is lost. It is counted in `coil_timeouts`. Only
  3 trips in a row on one coil while running latch `INH_OUTPUT`; a normal
  fire resets the count.
- **Injector pulse over 25 ms.** The pulse is clamped to 31000 ticks
  (24.8 ms) and counted in `injector_clamps`. There is no latch. The fuel
  plan already limits pulses to 25 ms, so this is a backstop.
- **Late injector start or end.** That event is skipped, or the injector is
  closed at once, and the event is counted in `late_events`. The other pair
  keeps its own pulse. The OEM starts a late pulse immediately without a
  fault; neither firmware latches.
- **Auto-clear after a stall** (`latch_poll`). An `INH_OUTPUT` latch clears
  once all of these hold: sync is lost, rpm is 0, there has been no crank
  capture interrupt for 1 s, and every coil and injector pin reads back off
  with its compare disarmed. If an output is not off, it is forced off and
  checked again on the next pass. This can happen at most 3 times per key
  cycle; the counter resets on key-on.
- **Diagnostics.** `trip_source` records the check that last tripped
  `INH_OUTPUT` or `INH_DEADLINE`. Live frame v5 reports it.

## 2. Idle control

All new settings are live tunable. At 0 they behave as in 0.0.7, except for
the feedback entry.

| Setting | Offset | Range | Suggested |
|---|---|---|---|
| Idle feedback RPM window | `0x970` u16 | 0–3000 rpm | 300 |
| Idle spark retard limit while falling | `0x974` u8, 0.5° | 0–20° | 2–4° |
| Overrun minimum coolant | `0x975` u8 | 0–150 °C, 0 = 60 °C | 60 |
| Cranking dwell limit | `0x976` u16 | 0 or 1000–4000 µs, 0 = 3500 µs | 0 |

- **Feedback window.** Feedback runs only below target plus the window,
  at any vehicle speed. Above it, idle uses
  `IDLE_RETURN`: it holds base position and closes one step per pass. This
  happens even without overrun fuel cut and with a cold engine.
- **Feedback entry.** The integral starts from base position (0), instead
  of tracking the previous valve position.
- **No speed condition and no floor.** Feedback has no vehicle-speed
  condition and no limit on how far below base it may close the valve, as
  in 0.0.7.
- **Spark retard limit.** "Falling" means more than 20 rpm lower than
  100 ms ago.

## 3. Crank capture and sync losses

- **Where a loss is detected.** A missed edge is detected only when its
  block ends (tooth 1 or 31), as sync-loss reason 5. Command `2F 02` still
  holds the 30 capture words of the first failed block. The Wizard's
  Developer tab decodes them and shows the doubled interval, that is, how
  many teeth before the block end the edge was lost. If no interval is
  doubled, the edge was lost in the re-arm window at the block end.
- **IEN=0 sections.** These also stop PEC, and when one lasts longer than a
  tooth period (166 µs at 6000 rpm) a capture is lost. `capture_timeout`
  and `capture_rotation_lost` used to run `safety_inhibit` inside
  `hal_hard_lock`; that call now runs afterwards under `hal_lock`, which
  level 15 and PEC still preempt. The remaining hard-lock sections are a few
  register moves each. The re-arm at the block end runs in `capture_isr` at
  level 15, where PEC is also held off. Its path up to `capture_arm` is
  short, but two edges inside it still lose one.
- **OEM comparison** (68678, `kb/algorithms.yaml` 281–310). The OEM corrects
  a single bad tooth locally: it adjusts PECC2 by one tooth either way (9BB4
  and 9BAC count these corrections). Only a failed gap check returns 0, and
  then XP1 does a full resync at 2B18C. TU744 still drops sync on any
  counter/capture mismatch and recovers through the 0.0.7 sync-loss recovery
  window. Local one-tooth repair is not implemented yet: the block has no
  free word for an inserted edge. Measure with the snapshot tool first.

## Verification

Passed:

- Native and stock-native firmware tests, including `test_output_faults`,
  `test_idle_008` and `test_latch_recovery`.
- Keil build of the stock-95080 image with no warnings; native calibration
  validation and HEX/BIN round trip of the packed image.
- Both 0.0.8 basemaps accepted by the firmware calibration commit.
- Tuning Wizard 0.0.7: all 5 test suites, against the 0.0.8 firmware
  protocol library, and the desktop smoke test.
- Target simulations (C167 emulator and Keil uVision) passed on the build
  before the idle speed condition and floor were removed. On the final
  binary the emulator suites pass again (main, audit, faults, fuel, reset,
  flash, firmware update, output closure, OEM scheduler, scheduler
  regressions, knock, knock boot, protocol); the foreground, uVision and
  artifact runs were not repeated.

Not run:

- the bench and engine checks of cranking dwell, the coil watchdog, the stall
  recovery and the new idle settings;
- Tuning Wizard 0.0.7 against a real ECU;
- native knock ADC completion in uVision (blocked in the installed version).

Known failing, not caused by this release: `tests/test_lifecycle_target.py`
expects wideband policy 0 to disable the wideband, while the firmware has
treated it as direct since 0.0.2; the synthetic fixture tune of the
foreground test fails validation (also on 0.0.5 to 0.0.7), so those runs use
the release binary.

Known limit: an automatic latch clear is reported live (trip source, clears
this key cycle) but is not written to the fault memory. The standalone fault
journal is full at 6 records.
