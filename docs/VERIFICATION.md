# Recorded verification

## 24 September 2026: knock IC (CC195) boot initialisation

The OEM knock-IC pin timeline is reproduced from reset ([KNOCK.md](KNOCK.md),
phase 1). `START167.A66` starts T1 (fCPU/16) right after the first watchdog
service, and `board_init` no longer zeroes it. `board_knock_ic_boot` (after
`iac_disable`, before EEPROM traffic) drives the port-init state at the OEM
instant, holds for the OEM time, then applies the `sub_493BC` bit sequence and
the `sub_493DC` filter bit.

| Check | Result |
|---|---|
| `tests/keil_knock_boot.py` (Keil C166 simulator, both release profiles) | Unchanged OEM ROM, with the stock 95080 served by a script model (1244 bytes read), vs the standalone: identical 6 observable pin events from reset, each within 5 CPU states (0.25 µs). Hold 320 836 vs 320 827 states. Release steps 6 and 3 states in both. |
| `tests/test_knock_boot_target.py` (c167re emulator, all three profiles) | Identical pin states; standalone hold 16.04 ms; BF2 equals the ROM jump table for 0..19 kHz. |

No ECU was flashed. Keil does not charge BUSCON1 (external RAM) wait states,
so a stock ECU may be slightly slower. A scope comparison of P3.5/P3.6/P8.0 on
the bench is the final reference.

## 23 September 2026 (later): every event to 10,000 rpm

Goal: with the rev limit raised to 10,000 rpm, no injection or spark is missed
at any speed. Keil C166 simulator, as-built bus timing (`BUSCON0=04AE`), image
base firmware SHA-256 `16d3c6a7…6d69` (release r3). The OEM image informed the
interrupt layout: its CC15 handler (`0x2B0B6`) keeps only the last two
captures of each PEC block and defers decoding.

| Change | Reason |
|---|---|
| Crank capture PEC6 and T1 extension at level 15; `hal_lock` raises ILVL to 14 instead of clearing IEN; `hal_hard_lock` (IEN=0) only where state is shared with level 15 | Critical sections no longer delay crank capture (capture overruns latched DEADLINE near 8,100 rpm) |
| Block crank decoder (`rotation_block`): regular teeth in registers, one write-back per block | ~1 ms per 30-tooth block at 10,000 rpm instead of ~2.5 ms |
| Final fire stage toggles CC0IO/CC1IO in hardware (compare mode 1); coil B moved from CC2 to CC1 | Spark edge jitter at 9,950 rpm from up to 110 us to ±0.2 us |
| Fine stages accept one tooth of lateness (anchored to the expected tooth) | A level-14 section could hold a stage past its 100 us tooth |
| Charge-start stage also entered 1-2 teeth early above ~4,000 rpm | Found in this run: with the soft limiter active a delayed charge start was dropped as late (90 sparks lost, 7,300-10,500 rpm) |
| Batch-capture decision uses min(block fastest, rotation.normal) | Brief single-capture mode during fast acceleration |
| UART RX at level 11 | ASC0 holds one byte (0.5 ms); the crank worker could overrun it |
| Calibration begin/commit and the implicit legacy write run as steps, one per control release | Found in this run: a running tuning write held the foreground 26 ms and the plan went STALE for 7 ms at idle |
| Division-free interpolation, located table axes reused, tick without 32-bit divide, diagnostics publish only at their 10 ms cadence | Foreground pass at 9,950 rpm about 15 ms (plan age limit 30 ms) |

Test-side corrections: the Keil K-line model now returns each echo one byte
time (512 us) later instead of instantly. With instant echoes the level-11
receive interrupt re-entered back to back for 2.2 ms and starved the tick,
which no real wire can do. `test_scheduler_regressions.py` injects the forced
equal match on CC1 (was CC2). `test_input_clock.py` models C167 arbitration
(tick taken only when IEN=1 and ILVL<10); its negative control removes the
ILVL raise.

Keil results on the r3 image (limits 10,000/9,900 rpm unless noted):

| Scenario | Result |
|---|---|
| Sweep 250 -> 10,500 -> 900 rpm, tune soft limiter from 5,900 rpm | Every spark at every hold (coil A 83/83 at 9,950); injector counts follow the soft cut; fuel cut at >= 10,000 with sparks retained; `2F`: late 0, overruns 0, plan age max 22 ms, tick max 2 ms |
| 900 -> 9,950 rpm in 300 ms and back, three times, then key-off | Every event in all three 9,950 windows; late 0, overruns 0 |
| 9,950 rpm hold with 4 Hz monitor polling and `2F` reads | 497/497 on all six outputs over 3 s; 14/14 replies; plan age max 24 ms; late 0 |
| 9,950 rpm with a dropped tooth, an extra tooth and a burst | Resynchronizes; every event before and after; 2 late events only inside the injected faults |
| Delivered basemap unmodified (limit 6,000) | Every event to 5,800 rpm; fuel cut at 6,200/7,000 with sparks; plan age max 16 ms |
| K-line abuse at idle including a live calibration write and save | No STALE (was 7 ms); foreground interval max 19.3 ms; 18/18 replies |
| Key-off, sensors, sync, EEPROM absent/busy, cold crank, key bounce, T1 wrap, reboot, 60 s endurance | As specified on the r3 image; no deadline, no STALE, no traps; >= 782 bytes system stack free |
| CPU at 9,950 rpm | Interrupts ~62%; foreground interval max 14.8 ms |

All 38 firmware regression steps pass (builds, native, OEM comparison, target,
stock-95080 target, `verify_artifacts`).

Keil does not charge the 8-bit external-RAM bus; the ECU is slower in
data-heavy code. Measure `2F` on the bench up to the intended rev limit before
running the engine.

## 23 September 2026 Keil uVision timing investigation and fixes

The stock-95080 image was run headless in the Keil C166 V7.57 simulator
(S166/D167, `-p167SR`), which charges the programmed flash wait states
(`BUSCON0=04AE`, 1 WS; the OEM application init at ROM `0x324EC` programs the
same value). The repository C167 emulator charges a flat 2 cycles per
instruction and was up to ~3x optimistic. Keil does **not** charge the external
RAM bus (`BUSCON1`, 8-bit): cycle counts were identical at 0 and 15 wait states.
The real ECU is therefore slower than these Keil results in data-heavy code.

Defects found and fixed (all software):

| Finding | Fix |
|---|---|
| `SSCBR` written while SSC enabled (C167CR UM p.280); Keil ignored it, SSC never transferred | `board_init` disables, loads SSCBR, enables (OEM order) |
| ADC continuous scan, one interrupt per conversion (17-24k/s, ~36% CPU) | single 16-channel scan started every 5 ms from the tick |
| Batch capture decided on a block holding only the post-gap edge; queue overrun and latched DEADLINE at ~1,150 rpm | judge `rotation.normal`, hysteresis 1092/1250 ticks |
| XP1 (level 11) delayed the 1 ms tick past its 3 ms bound; DEADLINE at ~2,000 rpm | XP1 at level 9, below T6 |
| Diagnostics ran before plan publication; with the 10 ms gate plans aged ~30 ms (STALE) | `oem_runtime_poll` after control publication |
| ~790 cycles per crank tooth | regular-tooth fast path with identical effects (native equivalence test, 320,000 randomized edges) |
| K-line reply sent one byte per foreground pass (98-byte monitor: 1.75 s) | receive-echo interrupt launches the next byte |

New read-only protocol command `2F` reports worst foreground pass/interval,
tick gap, plan age, capture overruns and late stages for bench measurement.
The scheduler test harness no longer fast-forwards time over a pending
enabled interrupt request (it starved the lower-priority XP1 worker).

Keil results on the final image (as-built bus timing, basemap tune):

| Scenario | Result |
|---|---|
| Delivered basemap, crank forced 900 -> 5,800 -> 7,000 -> 900 rpm | every event to 5,800 rpm; fuel cut at 6,200/7,000 rpm with sparks retained; clean recovery; `2F` plan age max 29 ms, 0 overruns, 0 late |
| Limits raised to 10,000 rpm, 250 -> 10,500 rpm sweep | every event to 6,000 rpm; 96% at 7,000; degraded at 8,000; capture overrun latches DEADLINE near 8,100 rpm (CPU saturated) |
| 60 s idle endurance with blips and K-line polling | every event, zero STALE, plan age max 20 ms, foreground pass max 14 ms |
| Key-off / save / reboot, sensor open/short, EEPROM absent/stuck busy, tooth drop/extra/burst, stall and re-crank, cold low-battery crank, key bounce, K-line abuse, T1 wrap | behaved as specified; no traps, no watchdog resets, >= 740 bytes system-stack headroom |

These are simulation results. Measure `2F` on the actual ECU before engine
operation: foreground interval and plan age must stay below the tune plan age
(`0x922`) with margin, tick gap <= 3 ms, zero overruns.

## 23 September 2026 output closure and supplied calibration

The [continuation evidence](../../../docs/audits/tu5jp-output-closure-2026-09-23/README.md)
supersedes the first scheduler checkpoint below. Software changes address
R-01 through R-11 on both output-enabled profiles. Soft revocation drains an
already charging coil to its retained deadline; output/board faults still turn
it off immediately. Healthy foreground progress continues watchdog service
after an output latch. Key reassertion waits for electrical/storage owners,
then requests reset even while rotating. CC9 correction requires explicit tune
eligibility and valid feedback; missing feedback cannot increase dwell.
Counter snapshots, charge-edge ordering and capture-buffer handoff are guarded.

Complete reset-to-main execution also exposed and corrected first-capture
alignment while rotating during boot, publication of permissions before the
new plan, and coarse-descriptor expiry during speed changes. All are covered
by linked-image regressions. The source and emulator are distinct: no emulator
implementation was changed for these repairs.

| Check | Result |
|---|---|
| Native core / sensor faults / earlier audit regressions | 29,824 / 901 / 448 assertions passed |
| Stock NOR/storage/lifecycle | 19,683 assertions and 3,107 interrupted program prefixes passed |
| SSC / staged output and decoder, both native profiles | 9,788 / 3,146,681 assertions passed per profile |
| Linked scheduler, each output-enabled profile | 16 existing + 27 regression + 5 ownership/feedback/ramp cases passed |
| Long pulse and load sweep | Every expected event on all six pins; 24-revolution windows through 85% injector duty |
| Acceleration/deceleration | 40 revolutions, 2,000 to 7,000 to 2,000 rpm; one event per pin per measured revolution |
| Complete stock foreground | Running tune commit, fault-latched watchdog service, rotating key restart, 9,000 rpm operation and supplied-calibration boot passed |
| Exact delivery BIN with supplied calibration | Reset while rotating at 5,000 rpm; 12/12 events per pin, zero inhibits/overruns; maximum poll 5.6503 ms |
| Converted calibration in linked C167 code | 1,024 map cells, 16 cranking points and 141 angle references passed |
| Target reset / input-clock / lifecycle / sensor-fault checks | 24 / 212 plus two negative controls / 1,064 / 6 passed |
| Native OEM comparison pipeline | Passed, including 5,920 unchanged-ROM dwell comparisons and 23,312 composed sequences |
| TunerPro DLL / definitions | 473 C++/actual-C-parser/Win32 assertions and definition/calibration CRC checks passed |
| Repository pytest | 945 passed, 1 skipped, 64 subtests passed |
| C166 builds and artifact identity | Default, engine-experimental, stock-95080 and uVision passed; 94,355 programmed bytes match between default CLI and uVision |
| Knowledge base | TU5JP: 684 records validate |

The running tune commit takes 13.3649 ms in complete foreground execution at
6,000 rpm, with maximum observed plan age 19 ms and no missing events. The
soft-revocation noise case now retains a 2,944.6 us charge and turns off about
0.715 degrees early, versus 11.41 degrees early at the earlier checkpoint.
This remains a prediction after loss of angle, not proof of the physical spark.
Both compare-install models pass the race regression. Missing-feedback runs
remain at calibrated dwell with zero correction over 20 revolutions.

The [calibration conversion report](../../../docs/audits/tu5jp-output-closure-2026-09-23/calibration/CONVERSION.md)
records non-equivalent legacy policies and the cranking reference assumptions.
Validation uses the 20 MHz emulator model. Silicon compare semantics, actual
clock/bus/interrupt timing, coil-stage behavior, CC9, watchdog period and engine
calibration remain physical acceptance work. No ECU was programmed or operated.

## Historical: first 23 September 2026 scheduler checkpoint

The following is the earlier five-fix checkpoint, retained with its original
results and limitations. The continuation above supersedes its open R-05 to
R-11 software status and final artifact identities.

Implemented audit R-01, R-02, R-03, R-04 and R-07 in the standalone source.
R-01 uses a pending start per injector pair, including the long-pulse case the
minimal audit patch did not fix. R-03 also expires abandoned charge descriptors.
The [scheduler contract](OEM-SCHEDULER.md) describes ownership, deadlines and
remaining limits. Original source and images are preserved under
`docs/audits/tu5jp-scheduler-fixes-2026-09-23/baseline/`; the
[implementation record](../../../docs/audits/tu5jp-scheduler-fixes-2026-09-23/implementation.json)
binds results to source, tests and image hashes.

| Check | Result |
|---|---|
| Native core, sensor journal and prior audit regressions | 29,812 / 901 / 448 assertions passed |
| Stock native storage | 19,625 assertions and 3,107 interrupted program prefixes passed |
| SSC and staged-output/decoder tests, both native profiles | 9,788 and 3,146,625 assertions passed per profile |
| C166 default, engine-experimental, stock-95080 and uVision builds | Zero warnings/errors; artifact identity checks passed; uVision programmed bytes match default command-line build |
| Existing continuous scheduler tests | 16 cases passed per output-enabled profile; maximum measured interrupt mask 1,206 modeled cycles |
| Existing lost-PEC, wrong-gap/recovery and stale-plan tests | Passed on both output-enabled profiles |
| New linked scheduler regressions | 19 cases passed per output-enabled profile |
| Preserved-image negative controls | All five regression groups fail at the expected defect on each original output-enabled profile |
| Target arithmetic/calibration and unchanged-ROM dwell comparisons | 1,095 and 5,920 passed |
| Repository pytest | 945 passed, 1 skipped, 64 subtests passed |

The event tests check all four injector and both coil pins. The 83.3% duty
cases deliver 24 injections per injector and 24 sparks per coil in each measured
24-revolution window, including phase 1620 and 32-bit timestamp wrap. Pulses
begun in the window are drained before checking completeness and width. Sparks
are counted at their firing edge because dwell adaptation can move charge starts
across the window boundary. Both race test variants (increment-only compare and
an explicit match-on-install variant) pass; this does not decide silicon behavior.

At 6,000 rpm, an actual framed single-byte tuning commit takes 13.179 ms with
scheduler/T6 interrupts in the 20 MHz model and passes supplied plan-age limits
20, 30 and 50 ms. The unchanged baseline takes 33.3454 ms in the 20 ms negative
control and inhibits. The input ring and preceding plan/heartbeat are supplied;
this is neither physical UART nor complete foreground-load acceptance.

Rejected noise now revokes synchronization in single-capture mode. During active
charging, the retained cancellation policy still produces an off edge about
11.41 degrees early in the regression. Its physical spark consequence (R-05)
remains unresolved. R-06 recovery/watchdog policy, R-08 run-permission recovery,
R-09 feedback eligibility, R-10 charge-edge ordering and R-11 snapshot races are
not closed by these changes. Acceleration, actual clock/bus/interrupt timing,
coil stage, CC9 and watchdog behavior still require physical validation.
No ECU was flashed or operated, and no engine release is claimed.

## 21 September 2026 reset-observation follow-up

Startup now preserves WDTCON before watchdog configuration/service and publishes
it after C initialization. Command2A and the reference client's `reset` action
expose availability, the raw word and WDTR only. The implementation and supplied
manufacturer evidence are in [LIFECYCLE.md](LIFECYCLE.md#reset-observation).

| Check | Result |
|---|---|
| Command-line Keil C166/A166/L166 build | 0 warnings, 0 errors |
| Linked reset vector through C startup, board_init and ecu_init | 24 cases passed across 8 raw observations and 3 RAM patterns |
| Linked CP pipeline barrier | NOP verified after initial CP write |
| C parser/reference client | 1,040 reset observations passed; malformed status/request rejection and existing transactions passed |
| Native core and SSC suites | 18,579 and 9,784 assertions passed |
| Native OEM differential pipeline | All existing producer, record, composed, readiness, history, cadence and hold suites passed |
| Keil arithmetic/calibration and lifecycle policies | 1,095 and 1,064 checks passed |
| Keil input-clock interleavings | 212 passed; both negative controls detected torn clocks |
| Keil history owner | 58 scenarios passed with modeled EEPROM/admission |
| Artifact verification | Original-source hashes, IRQ priorities, closed release gate and flash ranges passed |
| Repository pytest suite | 908 passed, 1 skipped, 64 subtests passed |
| TU5JP knowledge base | 680 records validate; rendered pages refreshed |

Target size: `data=11675 (near=11675)`, `const=3764 (near=3706)`,
`code=67722`. Raw reset observations are explicit emulator inputs, including
uninterpreted high bits; dirty RAM and stale capture markers must not leak into
the new observation. Startup executes the actual linked zero/copy tables and
continues through both application initializers. Physical reset sources, CPU
pipeline timing, reset wiring, watchdog timeout and complete ECU boot remain
outside these tests. No ECU was flashed, and BOARD_RELEASED remains zero.
No uVision rebuild or physical acceptance was performed for this follow-up.

## 21 September 2026 lifecycle checkpoint

Scope and unfinished work: [LIFECYCLE.md](LIFECYCLE.md). No Wizard changes,
ECU flashing, physical testing or engine commissioning were performed.

| Check | Result |
|---|---|
| Final command-line Keil C166/A166/L166 build | 0 warnings, 0 errors |
| Final native C regression suite | 18,579 assertions passed, including 51 new lifecycle checks |
| Actual SSC-owner/register suite | 9,784 assertions passed |
| Final Keil arithmetic/calibration checks | 1,095 passed |
| Final Keil analog qualification and launch-arm checks | 1,064 passed |
| Keil input-clock interleavings | 212 passed; both negative controls detected torn clocks |
| Keil history-owner scenarios | 58 passed with modeled EEPROM/admission |
| Native OEM differential pipeline | Passed all existing routine, composed, readiness, history and cadence suites |
| C parser/client | Passed, including lifecycle status and rejected/uninitialized arm/history requests |
| Artifact checks | Original-source hashes, IRQ priorities, closed release gate and flash ranges passed |
| TU5JP knowledge base | 678 records validate |

Final target size: `data=11671 (near=11671)`, `const=3760 (near=3706)`,
`code=67546`. This includes the instantiated diagnostic/history owner. It does
not establish stack depth or real-time acceptance. The uVision project was
regenerated to include the new modules; uVision itself was not rebuilt here.

New native tests cover analog-only disable/warm-up/continuous qualification,
clock rollover, rail and service-gap revocation, explicit stationary arming,
motion/expiry/calibration revocation, soft/hard launch, bounded anti-lag,
gear availability, boot history, save/reload, retained warning demand, unreadable
EEPROM, blocked power release after lost input acquisition, and reassertion
waiting for durable history before issuing one software-reset request. The new target
test executes actual Keil wideband/arm routines using compiler-derived offsets;
its inputs are explicit contracts, not modeled sensor electronics.

The OEM pipeline, clock and history-owner suites passed before the final small
runtime availability/power-release, restart and wideband requalification changes; their unchanged native/owner
contracts were not all rerun afterward. The final native suite, linked lifecycle
checks and artifact checks passed after those changes. Complete native monitor
delivery remains blocked on missing bindings, and physical power release remains
unimplemented. No full diagnostic/MIL parity is inferred from these results.

## Previous checkpoint — 19 September 2026

| Check | Result |
|---|---|
| Keil C166 7.57 / A166 / L166 command-line build | 0 warnings, 0 errors |
| uVision project rebuild | Previous checkpoint: 0 warnings, 0 errors; current invocation stalled without fresh output |
| Native C regression suite | 18,518 core assertions and 9,784 actual SSC-owner/register assertions passed |
| Compiled C versus unchanged OEM ROM | 163,886 comparisons passed: 6,400 coolant, 6,400 steady-MIL, 33,842 record/dispatch/phase, 22,088 input/state/context, 4,096 ADC publication, 28,577 supply-voltage, 6,976 vehicle-speed producer/reset, 7,046 vehicle-speed inputs, 13,569 digital inputs, 13,740 rotation/period/speed, 20,384 composed and 768 cadence cases |
| Command-line Keil machine code in C167 emulator | 1,095 arithmetic/calibration checks passed |
| Keil machine code versus compiled native C and OEM ROM | 151,086 record/dispatch/phase, input/state/context, ADC, voltage, vehicle-speed producer/reset/inputs, digital inputs, rotation/period/speed, composed and cadence cases passed across the recorded checkpoints below |
| Composed clear guards | 2,408 additional native/Keil duplicate and invalid-request checks passed; these are standalone guards, not ROM comparisons |
| Target input-clock interleavings | 212 actual Keil ISR boundaries passed, including 48 deferred ticks; both negative controls expose torn timestamps |
| History persistence owner | 58 scenarios passed in actual Keil-linked code with modeled EEPROM transfers and service admission, including actual deferred clears at all eight writer phases; native tests cover competing journals and changes/clears at every phase |
| Native retained-history codec | 610 native/Keil checks passed against retained RAM spans, including execution of the unchanged ROM clear walker |
| Standalone hold-state contract | 900 boundary cases passed with both native C and actual Keil-linked code |
| uVision-linked machine code | Previous checkpoint matched the tested command-line image; current image not verified |
| Command-line versus uVision programmed flash | Previous checkpoint: all 63,764 programmed bytes identical; current parity check remains open |
| Reference client versus compiled C protocol parser | Transactions, rejection rollback, readback and malformed legacy packet passed |
| Audited original sources | All 22 firmware and 6 Wizard files match audit hashes |
| IRQ/HEX checks | Unique configured priorities; development gate closed; flash records within reserved ranges |

The command-line target build reports `data=9595 (near=9595)`,
`const=3720 (near=3690)`, `code=60386`. This is static allocation and code size,
not a stack-depth or real-time execution measurement. User stack is 2048 bytes;
the linked system-stack reservation is 1024 bytes.

The uVision project and command-line build use LARGE, MOD167 and optimization
level 4 with speed emphasis. DPP preservation on interrupt entry remains at the
compiler default; the inherited uVision `NODPPSAVE` option was removed.

The latest change binds request admission and the eight ported callbacks from
native clear entry29620 to the persistence owner. Another34 native callbacks
remain unported. The owner rejects missing or malformed requests before any
producer mutation and reports clear persistence only after a matching snapshot
commits. All three request kinds were tested during each of the eight writer
phases, including restoration of both the older and cleared snapshots. The
target test exercises the single-event argument passed on the C166 user stack.

The complete native build/OEM pipeline passed. The expanded composition suite
passed22,792 checks in native and Keil modes:20,384 against unchanged ROM routines
and2,408 standalone guard checks. The212 input-clock interleavings and1,095
arithmetic/calibration checks also passed. The repository suite passed908 tests,
with1 skipped and64 subtests passed. The TU5JP knowledge base validates with677
records and was rendered. Older target differential counts remain checkpoint
evidence for their unchanged contracts; those suites were not all rerun.

The current uVision rebuild stalled without updating its log or HEX, including
an attempted build outside the sandbox. The launched processes were stopped.
Its older HEX therefore differs from the current command-line image; this is
not evidence of a fresh compiler-output mismatch. Current uVision parity remains
unverified. The ordinary artifact verifier passed source-isolation, IRQ, release
gate and flash-range checks for the current command-line build.

The preceding persistence changes add a foreground EEPROM transaction lease to
both journals and an explicit diagnostic-history persistence owner. Tests verify
that an older snapshot cannot clear a later dirty notification or acknowledge a
later completed clear; failed admission, IO failure and clock-wrap timeout retain
pending work and release the lease. The owner is compiled but not instantiated
by the running diagnostic lifecycle. Native startup, mutation-site and shutdown
binding remain pending. Its state occupies 854 bytes per instance on C166; no
such instance has been added to the running ECU's static allocation.

The target owner test executes the linked journal/codec/owner and lease functions,
with only EEPROM HAL transfers, service admission and boot watchdog service
modeled. It exercises all eight writer phases and explicit retry. This is not a
physical EEPROM, SSC-latency or full-application test. The harness accounts for
the C166 stack-passed high word of the save timestamp at clock rollover.

All native OEM suites passed at this checkpoint. The history suite initially
exposed a stale fixture that seeded only the event manager's run-flag alias;
it now seeds the canonical engine flags and checks that both survive restore.
That suite was rerun in native and Keil modes, followed by the remaining native
rotation/composition/cadence/hold/protocol checks. The earlier successful native
suites were not repeated. The 212 input-clock and 1,095 arithmetic/calibration
checks also passed on the rebuilt image. Older target differential counts below
remain checkpoint evidence for their unchanged contracts, not full reruns of
every suite on this image.

The preceding controller changes add timed high/low hold modulation, exclusive
SSC ownership, driver/peripheral/deadline fault handling and an independent
clock-progress check. Tests compile the actual target SSC owner with observable
register substitutes and explicitly supplied interrupts. They cover 800 normal
modulation transitions, counter wrap, bus admission, handoff, receive failure,
first-cause retention and stalled-timer recovery. They do not model electrical
waveforms, physical peripheral timing or real interrupt latency.

The preceding input changes add 70 native assertions for full-word retention,
channel ordering, freshness, clock wrap, calibration independence and read-time
age. The input-clock test asks C166 for its actual structure offsets and injects
a higher-priority clock update at every instruction boundary where IEN allows
it. Its negative controls temporarily replace BCLR IEN with NOP only in emulator
memory; each then exposes a torn timestamp. Peripheral timing is not modeled.

The digital-input checkpoint passed 13,569 native/Keil/ROM cases and 17,984
composed sequences. These include actual event ingestion, shared digital/MIL
flags, input publication and high-voltage qualification across speed-fault
assertion/recovery. The 212
input-clock interleavings and 1,095 arithmetic/calibration checks were also rerun
on the new build. The earlier 117,133 Keil record/input/ADC/voltage/vehicle-speed/
rotation/cadence comparisons and 900 hold cases remain recorded evidence for
those unchanged contracts; they were not all rerun for the digital-input addition.
The complete native OEM pipeline and protocol checks were rerun. None of these
counts claims complete application equivalence.

The 33,842 diagnostic cases include 8,192 callback-dispatch traces, record
allocation and state transitions, retained competing-event history, aging and
its repeat-call guard, clear admission/worker behavior, cycle initialization,
periodic reconciliation, drive/warm-up phases and timestamp boundaries. All
compared OEM routines execute from the unchanged reference ROM. Clear-request
tests also execute the original RTOS notification into an empty mailbox. The
standalone owner must still bind the accepted event to its foreground service.

The input/state/context suite contains 22,088 comparisons: IAT initialization,
reset, capture, all ADC-byte values and retained filter/fault trajectories;
operating-state transitions at native thresholds; and both native context
conversion entries including zero divisors, saturation and byte wrapping.
The 17,984 composed comparisons include 4,400 independently seeded sequences,
2,400 raw ADC publication/binding steps and 11,184 calls retaining shared
sensor/digital/voltage/vehicle-speed/event/MIL state over 2,400 synthetic base invocations.
The 768 cadence cases execute the OEM's actual RTOS delivery and
timer setter with accepted/rejected releases and native counter edge values.
These suites also execute the Keil-linked replacement. The harness masks the
unspecified upper register byte for C166 unsigned-char return values.

The supply-voltage suite covers initialization, base/alternate conditioning,
filtering, clear-request reset and the event producer. It includes all 256 ADC
byte values crossed with operating/count/speed-gate states, randomized retained
filter state, byte-divider wrap and a 2,000-step trajectory. The native
`9201` context field is correctly named vehicle speed; its old `tps` label was
removed without changing layout or conversion arithmetic.

The vehicle-speed suite covers both source policies, strict native operating
thresholds, separate failure/recovery/source counters, byte wrap, latched
subtypes and reset retention. Its 800-step trajectory retains each implementation's
state. The separate input suite adds initialization, the ISR payload, complete
native conditioning, all alternate acceleration-byte inputs and a 1,200-step
retained pulse/timeout/source history. It does not emulate PEC transfers,
register-bank interrupt entry/exit or concurrent capture/task ownership.

The digital-input suite compares complete initialization, seven-pin filtering,
flag/pulse/timestamp publication and the separate divider10 bit update. It
includes all old-output/three-sample combinations for each pin, pulse qualifier
states, full-word history retention, one-time timestamp capture with pending
overflow and an 800-step input history. Clock and port values are explicit
inputs; concurrent hardware acquisition is outside that routine contract.

The rotation suite compares capture threshold/reset, 24-bit interval history,
rotation/phase qualification, full-period speed/filter and speed-byte
publication. Its 13,740 cases include 1,800 retained capture steps with restart,
gap and slow-rotation patterns. SFR values are compared as state; the suite
does not emulate the complete capture ISR/PEC sequence or bind native inputs
to the standalone diagnostic runtime. See OEM-DIAGNOSTICS.md.

The TU5JP knowledge base validates successfully after the dispatcher and
MIL-aggregation evidence corrections. The rendered knowledge base was refreshed.

### Engine-output scheduler follow-up, 21 September 2026

`build.py native` also compiles the actual `engine_outputs.c` with register
substitutes and executes it together with `rotation.c`, `safety.c` and `math.c`.
The new suite passes **15,427 assertions**: constant-period 60-2 traces spanning
approximately 40, 100, 200, 1,000, 3,000 and 8,000 RPM; five advance settings; exact event
counts after synchronization; 80-tick rejected noise after short injection;
capture/T7 wrap; delayed dwell; exact tooth-boundary start; stale/expired
compare rejection; soft-cut opportunity consumption; sync loss and hard cut.
The existing native suites still pass 18,579 core/lifecycle assertions and
9,784 SSC assertions.

`tests/test_engine_target.py` passes **32 linked C166 cases**, including actual
capture-ISR rejection of a noise edge and delayed ignition admission through
T7 wrap and staged off-state waiting across a gap longer than one timer wrap.
It compiles an unchanged copy of the output owner against a test-only
header with `BOARD_RELEASED=1` and links into
`build/engine-output-test-only/OUTPUT_TEST_ONLY`. All other objects come from
the selected Keil build. The test-only image is for emulator admission coverage,
not an engine release. The default development header/image remain gated.
The separately compiled `engine-experimental` artifact also passes these
output cases, with byte identity checked against the test link before execution,
and the 1,095 linked arithmetic/calibration checks. Main's actual argument to
ECU initialization is checked against the selected build profile. The experimental
image also passes 24 reset/startup cases and 212 input-clock interleavings with
both negative controls detected. Its 512 KiB binary SHA-256 is
`cb3a9ad3c5b714eed28c5a28be0d163841c8479739d4682a1683df8c81a445db`.
Keil reports zero errors/warnings. The default Keil and uVision builds match
at all 72,436 programmed flash bytes after this follow-up.
Scripted clock/SFR observations do not simulate physical ISR timing, analog
inputs, complete ECU operation or a loaded engine.

### Sensor fault policy follow-up, 21 September 2026

Faulty TPS, coolant, intake-temperature, battery and speed-density MAP inputs
now retain their measured raw ADC values through normal calibration conversion,
record private standalone DTCs and do not assert a sensor engine inhibit.
Stale inputs use the last captured raw count. No replacement sensor values or
default calibration are introduced; missing calibration still inhibits start.
See FAULTS.md for the policy and persistence contract.

The native suites pass **19,480 core/lifecycle assertions**, including **901**
sensor/fault-journal assertions, plus **9,784 SSC** and **15,427 engine-output**
assertions. Coverage includes all 99 prefix-write interruption points and
combined standalone/native-history shutdown ownership. The actual C parser and
reference client pass their transaction suite, including private DTC reads and
1,040 reset observations.

Both default and experimental C166 builds pass **6 sensor fault cases** and
the 1,095 arithmetic/calibration checks. The rebuilt experimental artifact passes
**32 engine-output/capture cases**, with identical test-link bytes. The default
build passes **24 reset/startup cases** and **212 input-clock interleavings**
with both negative controls detected. Default Keil and uVision outputs match
at all **75,817 programmed flash bytes**, with zero build errors or warnings.

This follow-up supersedes the experimental binary hash above. The current
512 KiB experimental binary SHA-256 is
`6092b3a522ce041e1bd7ab00f2c5a14de3763b88cdc73314a1d2ee8ec8e5e032`.
Its source hashes are recorded in `build/engine-experimental/manifest.json`.
These checks do not constitute physical ECU or engine validation.

Logs and generated artifacts:

- `build/native/build.log`
- `build/input-clock/layout.SRC`, `build/input-clock/build.log`: compiler-derived ABI layout
- `build/history-owner/layout.SRC`, `build/history-owner/build.log`: persistence-owner ABI layout
- `build/oem/build.log`, `build/oem/example-schema3.bin`
- `build/c166/build.log`, `build/c166/TU5JP.m66`, `build/c166/TU5JP.H86`
- `build/uvision/build.log`, `build/uvision/TU5JP.m66`, `build/uvision/TU5JP.H86`
- `build/verification.json`: hashes of source and generated command-line HEX,
  flash range, configured interrupt assignments and original-source checks.

`build/` is ignored. The tests and build scripts regenerate these artifacts.
After rebuilding in uVision, `tools/verify_artifacts.py --uvision` also compares
every programmed flash address/value with the command-line build and records
the result and both HEX hashes in `build/verification.json`.
The statement that all audited original files are unchanged is limited to the
audit manifest; no write operation was directed at the original repositories.

No ECU flashing, physical waveform/current tests, engine testing, complete OEM
diagnostic lifecycle validation or Wizard GUI migration was performed. The
remaining implementation and acceptance items are listed in RELEASE.md.


## Fuel percentage follow-up, 21 September 2026

Schema 4 replaces cranking event pulsewidth with effective VE percent and
changes after-start and acceleration enrichment to fuel multipliers with 100%
neutral. See FUELING.md for ranges, exact calculation, migration limitations
and monitoring changes. The decoder is unchanged; its OEM-equivalence boundary
is recorded there separately from the supplied PDF geometry/DEPHIA evidence.

Validation on the updated sources:

- Native: 19,507 core/lifecycle assertions, including 901 fault-policy/journal
  assertions; 9,784 SSC and 15,427 engine-output assertions.
- Default and experimental linked C167: 26 fuel/AE cases each, plus 1,095
  arithmetic/calibration checks each. Coverage includes percentage composition,
  VE above 255%, IAT/MAP, zero fuel, clock wrap, saturation and AE recovery.
- Default linked C167: six sensor fault-policy cases.
- Actual C parser/reference client: transactions, 1,040 reset observations,
  schema-3 rejection on both sides, active-tune retention and neutral telemetry.
- Experimental engine-output/capture: 32 cases, with test-link bytes identical
  to the actual output-enabled artifact.
- Default Keil/uVision: all 76,125 programmed bytes identical, zero warnings
  and errors. Code size 72,244 bytes, data 11,911 bytes, constants 3,785 bytes.

The experimental binary is rebuilt without an included calibration. Its current
SHA-256 (superseding earlier hashes in this document) is
`120142f388a7c76524fea06cfc1108d0e792e51d8e067d37fcde1fc7bc4443e9`.
All source hashes in its manifest were checked against the final sources.
The schema-4 example generated by the protocol test is
`build/oem/example-schema4.bin`; it is not an engine tune. Physical ECU/engine
validation and the user's base-map conversion remain outstanding.

## Standalone OEM-event monitor checkpoint, 22 September 2026

The 10 ms runtime now feeds 28 applicable event IDs into the OEM-derived
record/aging/MIL lifecycle. Tests cover whole-event and per-subtype activation,
live-subtype reconciliation, wideband suspension of narrowband events, sensor
range/performance selection and DEPHIA delay/direction/missed-capture handling.

- Native: 19,520 core/lifecycle assertions, 9,788 SSC assertions and 15,427
  engine-output assertions.
- Default C166: zero compiler/linker warnings or errors; 81,298 code bytes,
  12,373 data bytes and 3,831 constant bytes. Command-line and uVision output
  matched at all 85,229 programmed flash bytes.
- Linked target: 1,095 arithmetic/calibration cases and 32 engine-output/capture
  cases passed.
- OEM record differential test: 33,842 comparisons passed, including the
  identical Keil-linked calls.
- TunerPro: XDF/ADX bounds and packed-tune boot/CRC suite passed after adding
  the event/subtype masks and monitor thresholds.
- Artifact verifier: original-source hashes, unique IRQ priorities, development
  output gate and linked flash ranges passed.

These are software results. DEPHIA polarity still needs a board waveform/cylinder
test. The P6.5/P6.6/P6.7 driver diagnostic interface and a post-link whole-image
checksum are not implemented, so output-circuit events and P0605 remain inactive.

## DTC readout/clear, homing steps and lean logging, 24 September 2026

Changes: calibration word `0x93C` sets the IAC homing step count (0 = 250),
monitor extension 2 carries the inhibit word and native DTC counts, command `30`
clears stored DTCs, freeze frames capture fuel trim in 1/128, plugin 0.3.0 adds
Read/Clear DTCs, ADX 5.0 polls command `10` only, and the XDF is regrouped.

- Native: 2,365,852 core, 467 audit-regression (including an end-to-end misfire
  DTC, freeze frame, refused clear while running and accepted clear when
  stopped), 901 fault-journal, 9,788 SSC and 3,146,681 staged-output assertions.
- Stock-95080 C166: zero warnings/errors; code 95,994, data 13,336 and const 3,900
  bytes. Firmware and plugin build identities verified against final sources.
- Linked target: arithmetic/calibration, engine, fault, lifecycle, reset, IAC hold,
  input-clock and all OEM input/record/history/composed/rotation/cadence suites passed.
- OEM record differential: 33,842 comparisons; history codec 612; composed
  sequences 23,312; history owner 58 scenarios.
- Plugin: 568 C++/actual-C-parser and DLL assertions, including the DTC panel.
- TunerPro definitions, ADX hash vectors/references and artifact tests passed.

Timing in the Keil uVision simulator was not re-measured for this revision. The
changes are outside the crank, spark and injection paths, but the r3 bench timing
procedure still applies before running the engine.
