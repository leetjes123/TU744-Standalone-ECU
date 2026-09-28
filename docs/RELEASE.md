# Implementation status and release requirements

The [stock-M95080 delivery package](../releases/tu5jp-m744-m95080-2026-09-23/README.md)
contains normal output-enabled firmware and the supplied, converted basemap.
The [23 September continuation](VERIFICATION.md) addresses the eleven output
audit findings in software and adds complete foreground, speed-ramp and loaded
event-count coverage. The source builds and its listed software contracts are
tested. The default development output gate remains off; the explicit output
profiles enable outputs subject to normal runtime checks. Physical production
acceptance and complete OEM parity are not established by those tests.
No ECU was flashed, no engine was run, and no physical timing/current measurement
was performed in this workspace. Turning on `BOARD_RELEASED` alone does not
complete the work below.

The [OEM-style scheduler](OEM-SCHEDULER.md) is implemented with PEC capture,
deferred processing, staged ignition and dwell feedback. Its continuous software
tests replace the previous fixed-phase timing gate. Full physical timing and
controller acceptance remain open.

The [21 September checkpoint](LIFECYCLE.md) supersedes the earlier status below
for analog-wideband policy, launch/anti-lag/gear, P4.4 permission sampling and
diagnostic boot/shutdown ownership. Full native diagnostics, physical latch
release, silicon-specific reset decoding and recovery flashing remain open.
The 23 September reassertion change waits for active outputs and in-flight
storage work, then requests reset even with a rotating crank; it does not
start a new history save solely to service restart.

Follow-up: raw reset observation now survives C startup and is available through
command2A/reference-client `reset`. Silicon-specific flag decoding and physical
reset acceptance remain open; see LIFECYCLE.md. Reset capture does not alter
history validity or supply a firmware recovery mechanism.

## Remaining implementation

The [scheduler implementation audit](../../../docs/audits/tu5jp-standalone-oem-scheduler-2026-09-22/FINDINGS.md)
supersedes the previous 25/2610 per-tooth result and minimum-remaining-dwell
shutdown. G02 now has complete foreground and acceleration/deceleration
regressions, in addition to supplied-plan tests. Comprehensive foreground/load
bounds, very slow gap timing and measured hardware latency remain open; these
tests are not production WCET evidence. G03/G04 recovery and runtime
integrity, engine abstraction G08, and hardware/engine validation remain open.
Build hashes and package identity do not close those gates.

The 22 September audit fix follow-up supersedes older statements below about
flashing and missing standalone cycle ownership. Correct-behavior regressions
are in `tests/test_audit_regressions.c` and `tests/test_audit_target.py`; the
immutable pre-fix audit and the separate follow-up evidence live under
`docs/audits/` at repository root. These repairs do not certify production use.

The standalone now owns immediate-ON, 1 Hz/50% flashing MIL demand, drive
qualification after 3 s continuous synchronized running, and warm-up qualification
after a valid coolant rise of at least 22 C reaching at least 70 C. Only the
warm-up definition and the requirement to flash for catalyst-threatening misfire
are supplied-document claims (Bosch training PDF p53/technical47 and p18/technical12);
the timing and qualification adapters are explicit standalone policies, not OEM
routine parity. Each eligible event advances at most once per qualified drive
or warm-up cycle. Misfire confirmation/healing advances per 128 qualified
neighbor-relative crank observations; unknown phase cannot advance individual
cylinder records. Native callback entry behavior remains separately ROM-tested.
Stored confirmation/healing is regression-tested across EEPROM save/reboot.

Auxiliary pump/boost/heater permission is checked both in the foreground and
atomically at the output HAL. Output/deadline/calibration/service/power/board
shutdowns cannot be bypassed by a later auxiliary update. Sync-only inhibition
still permits key-on wideband power. Soft cuts now have separate, staggered
budgets per physical pair; skipped sparks never arm DEPHIA. No faulty-sensor
fallback policy or physical pin assignment was silently changed.

Misfire thresholds, phase polarity and electrical signal acquisition still need
dummy-load and engine qualification. The fixed 60-2/four-cylinder scheduler and
TU DEPHIA provider are not a completed engine-independent architecture. Production
release additionally requires the board identity/latch, complete interrupt/stack
timing, output-feedback/program-integrity, stock-history/update recovery,
calibration/controller validation and tooling migration gates from the audit.

1. **OEM diagnostic parity:** finish every applicable producer, its native input
   conditioning/variant gates/cadence, and the shared event-record lifecycle:
   confirmation, replacement, freeze frames, healing/aging, persistence and
   clear. A recoverable EEPROM history journal and native retained-field codec
   now exist, with foreground dirty tracking, durable-clear bookkeeping and
   exclusive calibration/history transactions. Request admission and nine ported
   clear callbacks now update that owner, with ROM-tested reset order and guarded
   deferred service. The other33 callbacks in native clear entry29620 remain open.
   Standalone boot/shutdown now owns the history journal. Native runtime task
   delivery and remaining diagnostic mutation/clear sites still need integration.
   See HISTORY.md. Integrate steady MIL demand from that lifecycle. Current work supplies
   107 lookup rows, coolant/IAT/supply-voltage/vehicle-speed native-state code, native steady-MIL routines and
   record allocation/ingestion/confirmation/recovery/aging ports, clear request
   and worker code, reconciliation and drive/warm-up phase routines. Shared-state
   composition, operating-state/context conversion and adaptive divider contracts
   are also ported and tested. Native capture-history, reset, full-period speed
   and speed-byte conversion contracts are now ported and ROM-tested. Their
   original ISR/PEC sequencing remains to be bound. Whole-word ADC publication
   and a per-channel frontend/binding API are implemented; scheduling and
   unavailable-input policy for the diagnostic owner remain open. Supply-voltage
   production now shares native event/operating state and has ROM-tested ingestion
   and MIL composition. Vehicle-speed event68 production/reset and its shared
   event/flag composition are also ported and ROM-tested. Vehicle-speed init,
   batched capture payload and full input conditioning now have tested native
   contracts, including composed publication. Seven-pin digital filtering and
   native flag/pulse/timestamp publication are now ported and composed with MIL
   and speed state. Their board acquisition and complete native cadence still
   need binding. Board timer/PEC/input ownership,
   full task scheduling and
   remaining producers are still required before enabling runtime diagnostics.
   See OEM-DIAGNOSTICS.md for their exact scope. All event rows still show full
   parity false.
   The ROM-parity module does not implement flashing; the standalone lamp owner
   now does. Missing LTFT-dependent or OEM-only
   equipment inputs cannot be replaced by guessed passing values.
2. **Spartan qualification:** identify the model and bench-qualify startup,
   current/load behavior and transfer while powered across the OEM upstream
   heater conductors. The explicit analog-only readiness policy now exists and
   defaults disabled; see WIDEBAND.md.
3. **Power state and IAC:** bind the real ignition/power-latch input and shutdown
   sequence. `key_input` now waits for filtered P4.4; physical latch release
   remains blocked and reassertion resets only after a settled shutdown. Restore/validate
   adequate IAC holding torque and real driver diagnosis timing. Timed high/low
   hold modulation and serialized SSC ownership are now implemented, together
   with deadline supervision, driver-error handling and bridge-off retry. See
   ACTUATORS.md. Validate the nominal waveform, maximum ISR delay, winding
   current and physical movement. Successful command accounting does not prove
   physical homing or position; the arithmetic current labels are command settings.
4. **Standalone feature parity:** complete the Wizard GUI migration, deliberate
   launch arming at zero vehicle speed, soft-launch/anti-lag policy, gear
   estimation where required, silicon-specific reset decoding and recoverable flash update.
   Explicit stationary/moving arming, soft launch, bounded anti-lag and gear
   estimation now exist; their installation acceptance remains. The firmware-update path is
   intentionally absent until a verified replacement exists.
5. **Controller acceptance:** extend dynamic tests for cold/hot starting,
   saturation/re-entry, fan preload before relay load, delayed wideband response,
   AE transients, input failover and the complete combined running scheduler.
   Fan preload sequencing is implemented and tested against command position,
   with a 500 ms fallback and actuator/input-fault bypass. Physical load and
   engine-speed acceptance still need measurement; there is no IAC position sensor.

## Physical and target acceptance

Confirm the MCU derivative and measured clock; inherited bus/startup settings;
crank input edge and tooth reference; coil pairing; injector phasing; analog
scaling; driver inactive states; reset behavior; IAC current and direction; and
the gauge/tach/boost waveforms. Preserve both supplied-document and ROM evidence
when they conflict and update the repository knowledge-base question rather
than silently choosing a new interpretation.

Use dummy loads before an engine. Exercise compare wrap, maximum pulse/dwell,
simultaneous interrupts, hard/soft cuts, sync loss during active output, missed
foreground/tick service, SSC/UART failure, hardware traps and watchdog reset.
Measure requested versus delivered timing, interrupt latency and worst-case
system/user stack use under maximum RPM plus tuning traffic. Verify that a
hardware compare cannot reassert an output after cancellation. Confirm internal
watchdog timeout with the final clock. There is no proven external watchdog
contract to substitute for these tests.

The artificial example calibration is never an acceptable substitute for the
car's existing tune and baseline traces. Preserve the old binary/tune and compare
the new fuel/time conventions before engine commissioning.

## Meaning of the automated tests

- Native C assertions cover arithmetic, schema rejection, sync acquisition/loss,
  epoch/admission/cancellation, malformed packets, homing/SSC failure, engine
  hysteresis, a simple wideband/STFT feedback plant, stale/rail input rejection,
  clock-progress watchdog policy and calibration power-loss recovery.
- EEPROM tests interrupt each service phase and each byte boundary in the
  sequential write stream. They model prefix-torn writes, not arbitrary physical
  EEPROM corruption or retention degradation.
- OEM differential tests compare coolant, IAT, steady-MIL, record-manager,
  operating-state, context and cadence contracts against the unchanged ROM.
  Composed sequences also retain sensor and event history across calls. They do
  not cover the full event lifecycle or physical-input equivalence. Exact
  recorded counts are in VERIFICATION.md.
- Target tests execute the actual Keil-linked arithmetic, calibration validation
  and signed-temperature lookup instructions with the repository emulator.
  They do not prove peripheral timing or full application scheduling.
- Host-client tests use the actual C parser to test begin/write/commit/readback,
  rejected commit rollback and the original malformed-packet regression.

Build success and isolated routine parity do not establish complete engine or
OEM-diagnostic equivalence. Keep the capability flags and this status accurate
as remaining work is completed.
