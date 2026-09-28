# OEM diagnostic contracts

Runtime update21 September: boot/history/shutdown ownership and a guarded
projection of the ported task calls now exist. Native input bindings still
block monitor delivery, all full-parity flags remain false, and the retained
steady-lamp fallback is a standalone policy. See [LIFECYCLE.md](LIFECYCLE.md).

Reference image: `bins/M744_C167_FULL.bin`, SHA-256
`5710015f7c5c066c860a1757fd893f305701608c25af8ff23bfcb4fd1e4837b3`.
All ROM addresses below are offsets in that image. No TU1JP/ME7.4.4 addresses
or calibrations are substituted. The C ports use native bytes, words and phase
identities, not guessed engineering-unit thresholds.

## Implemented record routines

| C entry | ROM routine | Contract |
|---|---|---|
| `oem_dtc_assert` | `6A1E6` | Allocation, reassertion, class limits, context capture |
| `oem_dtc_recover` | `6A636` | Recovery and eligible removal |
| `oem_dtc_complete` | `6A7D2` | Monitor-completed transition |
| `oem_dtc_subtype` | `6A92A` | Current subtype replacement |
| `oem_dtc_ingest` | `6A996` | Ordered dispatch and live descriptor feedback |
| `oem_dtc_aggregate` | `6AA60` | Demand priority, occurrence count, phase callback |
| `oem_dtc_phase` | `6AB72` | Phase-specific confirmation and healing |
| `oem_dtc_age` | `6B252` | Gated aging, counter reload and row erasure |
| `oem_dtc_clear_worker` | `6B4D8` | Deferred clear of RAM records/live descriptors |
| `oem_dtc_remove` | `6A13C`, `6A068` | Native row removal and shifting |
| `oem_dtc_maintain` | `6AD34` | Live/stored reconciliation, deduplication, request timeout |
| `oem_dtc_cycle_begin` | `6B1AA` | Cycle initialization with FD14.15 variant path |
| `oem_dtc_clear_all` | `6BA14` | Admit an all-event deferred clear request |
| `oem_dtc_clear_emissions` | `6BA7C` | Admit an emissions-class deferred clear request |
| `oem_dtc_clear_event` | `6BB22` | Admit a single-event deferred clear request |
| `oem_dtc_drive_init/update/clear` | `69F8C`, `69F9A`, `69FDE` | Native drive-phase timer and callbacks |
| `oem_dtc_warmup_init/update/clear` | `6BE44`, `6BE52`, `6BEB2` | Native temperature-rise phase and callbacks |
| `oem_dtc_clock` | `6BE28` | Timestamp divider and wrap |

`tools/generate_oem.py` extracts the 38 configuration rows, 107 event/config
entries, 107 event-count thresholds, seven phase addresses, drive/warm-up
calibration and report words.
It verifies the reference image before generation. `OemDtcState` holds the live
event descriptors and native record state; it is not yet a runtime diagnostic
service. `dtc-parity.json` continues to mark complete event parity false.
The event-level acceptance criteria and current 73-event coverage totals are in
[OEM-MONITOR-COVERAGE.md](OEM-MONITOR-COVERAGE.md); a report-word row or generic
event-manager call is not counted there as an implemented monitor.

Each record is 24 bytes. Word offsets 2 and 4 are little-endian descriptor and
status/configuration words. The timestamp at offsets 20/21 is big-endian, as
written by `6B74A`. Context capture follows `6B698`, including its repeated
source bytes. Native context conversion and capture cadence remain separate.

The implementation preserves behavior that a generic DTC library would change:

- New-event and recovery branches skip later callbacks. Completion falls through
  to subtype comparison; both callbacks can execute in one ingestion.
- Demand 2 has priority over demand 1. Aggregation happens before the final
  confirmation callback, so that confirmation affects the next aggregation.
- Last-slot removal clears the row without decrementing the native count.
  Interior removal shifts rows, decrements count and clears the new tail. A
  phase loop still increments its index after removal.
- Allocation checks the 12/8 class limits during its scan. Those checks can stop
  it before a later matching record. The code does not invent replacement rules.
- Aging is gated by native FD6C bit 6. A second call has no effect until the
  proper native phase clears the bit. Row erasure does not compact the pool.
- The all-events clear worker clears live entries 1 through 105, leaving 0 and
  106 untouched. Its class-selective mode clears bit 7 and qualifying rows,
  preserving count. A single-event clear searches all 20 rows. The worker does
  not validate the complementary request word; request admission does that.
- Periodic maintenance moves one successor into an empty interior row and clears
  that successor; it does not compact the entire pool. Its inclusive scans can
  read one row beyond the count. At a full pool that row aliases the first live
  descriptors; the C implementation models this explicitly without an out-of-
  bounds array access.
- Clear admission validates the request/complement pair and marks live bit 7.
  Successful admission hands a deferred event to the standalone owner. Tests
  execute the OEM's real RTOS send into an empty mailbox and check the event.
  RTOS notification is an integration boundary, not an imported OEM RTOS.
- Warm-up completion requires native FD16.2, initial coolant below 156, current
  coolant strictly above 159 and a rise of at least 28 native units. Completion
  sets phase 952B and calls the shared phase routine. The supplied training
  document's Celsius description is not used to replace these ROM comparisons.
- Timestamp advancement takes 361 calls from a zero divider. Its time unit
  depends on the original scheduler, not on a guessed millisecond tick.

Record-walking C APIs reject corrupt standalone count/config/event indices before mutation.
That is an explicit memory-safety boundary around the valid native contract,
not an assertion that the OEM performs those same guards. Callers must serialize
access. Native lock bits are represented as state; concurrent ISR mutations are
not simulated by these pure routine tests.

`oem_diagnostics_clear_ported` now composes the eight implemented reset/worker
calls from native entry `29620`, preserving their order and shared state. It
rejects absent or invalid pending requests before any mutation; this additional
preflight is a standalone guard, not behavior attributed to the native worker.
The comparison suite executes the unchanged eight ROM routines in2,400 seeded
cases. Another2,408 duplicate/invalid-request cases check the standalone guard.
The native entry contains42 calls; the remaining34, including final `6C18E`, are
outside this composition. The persistence owner binds request admission and
successful service to dirty/durable-clear tracking; see [HISTORY.md](HISTORY.md).
This does not expose a diagnostic-tool clear command or complete the runtime task.

## Input, state and composed contracts

`oem_adc.c` ports the complete analog publication routine at `2C188`. Startup
at `2C126` fills `F7B0..F7CE`; `2C1D8`/`2C200` configure 16-word PEC transfers
from `ADDAT` to that buffer. The [C167 manual, Auto Scan Conversion Modes, PDF p.271](https://community.infineon.com/gfawx74859/attachments/gfawx74859/twlegacymcu/1131/1/2594108.pdf)
specifies descending channel order in auto-scan mode. Combined with the ROM's
channel-15 start and incrementing destination, this establishes the following
buffer mapping; it is distinct from a physical measurement of the fitted ECU.

| Native output | Buffer input | Channel | Native publication |
|---|---|---|---|
| `95B0` | `F7CC` | AN1 | low 10 bits |
| `9209` | `F7C4` | AN5 | bits 2..9 as byte |
| `95B8` | `F7C2` | AN6 | low 10 bits |
| `95B6` | `F7C0` | AN7 | low 10 bits |
| `95BA` | `F7BE` | AN8 | low 10 bits shifted left 6 |
| `95B4` | `F7BA` | AN10 | entire conversion word |
| `9208` | `F7B8` | AN11 | bits 2..9 as byte |

The actual standalone ADC publication now retains each captured `ADDAT` word
alongside its raw count, timestamp and generation. `oem_adc_snapshot` reads
those fields under short per-channel exclusion, converts their order, and
returns a separate freshness bitmap. Channels can come from different scans.
Its age limit is a standalone transport contract, not an imported OEM DTC
threshold. It preserves stale raw values rather than fabricating passing ones;
the future diagnostic owner must handle unavailable input explicitly.
`oem_diagnostics_adc` binds the resulting coolant word, IAT byte and supply-voltage byte to the
composed producers. The tunable sensor calibration/filter path cannot change
these native values. This frontend API is implemented; the full diagnostic
service still needs its native scheduling and other input gates.

There are 4,096 direct publisher comparisons, covering all 10-bit values with
distinct channel patterns and preserved upper bits. The composed test also
feeds raw scan words through the publisher and producer binding on 2,400
retained base invocations, before exercising the sensor/event/MIL sequence.
Both paths execute the unchanged ROM and, with `--target`, the Keil-linked C.

## Supply-voltage producer

`oem_voltage.c` adds event `0x65`, whose native report rows contain P0563,
P0562, P0560 and P0561. This identifies report-word mapping; the producer still
has to emit the corresponding subtype under its actual native conditions.

| C entry | ROM routine | Contract |
|---|---|---|
| `oem_voltage_init` | `663C0` | Delay/counter initialization, substitute and filter seeding |
| `oem_voltage_base` | `66422` | Conditioning while native running bit `FD16.2` is clear |
| `oem_voltage_update` | `664A2..666FC` | Voltage event descriptor before ingestion |
| `oem_voltage_filter` | `6670C`, including `06CAE` | Native narrowed scaling and fractional filter |
| `oem_voltage_alternate` | `6675E` | Substitute selection without changing the filter/status |
| `oem_voltage_reset` | `6677C` | Clear-request counter reload |

The exact calibration bytes `115FE..11603` are 30, 5, 127, 91, 36 and 145.
They are retained as native counts and ADC coordinates. Below 36 the producer
selects substitute 127 and its invalid-input path. The running path also
detects input below 91 or above 145; those threshold comparisons are strict.
The high subtype additionally requires expired startup delay, nonzero road-
speed byte `9201`, and a clear fault bit in vehicle-speed event `0x68`.
The low subtype excludes the invalid-input state. Descriptor retention,
separate fail/pass counters, counter evaluation before decrement and the
byte-sized startup divider are preserved rather than replaced by a generic
millisecond debounce. No fabricated passing vehicle-speed gate is supplied.

The scaling intentionally narrows `(voltage * 1130) >> 2` to 16 bits. The filter
multiplies whole-word distance by `0x3333`, borrowing one word from a nonzero
fraction on the rising branch; minimum fractional movement is one. A generic
exponential filter would change this behavior. Initialization leaves the
fractional word, status and divider as supplied by its caller.

The normal base task calls `66422` before its context and operating-state
entries. Normal divider10 entry13 calls the producer, after the IAT/coolant
entries. Separate composed entry points retain this order and call the real
event manager port. The expanded composed test executes the original ROM
ingestion too, retaining voltage, sensor, event and MIL history independently.
The 28,577 producer/conditioning cases include every ADC byte, running/stopped
paths, counter boundaries, vehicle-speed gating and retained trajectories.
Both native and Keil-linked replacements pass. Runtime scheduling and full
event lifecycle parity remain incomplete, so event65's full-parity flag stays
false.

The early `OemContext.tps` name for byte `9201` was incorrect and has been
replaced by `vehicle_speed`. This is a semantic correction with the same field
layout and arithmetic. ROM `29EC4..29F9E` publishes that byte from the filtered
period-derived word `95AA`, and the voltage producer pairs it with the live
vehicle-speed descriptor `B2F4`. Supplied training PDF p.44 distinguishes the
throttle potentiometer from the gearbox-output speed sensor; PDF p.9 names
battery voltage as an ignition input. Those supplied descriptions do not prove
the image's addresses, thresholds or physical calibration. The superseded name
and replacement evidence are recorded in the TU5JP knowledge base.

## Temperature, state and context composition

`oem_inputs.c` adds the intake-temperature initialization/reset/update/capture
at `69740`, `697A4`, `697CE` and `69970`, the operating-state initialization/update
at `329C8` and `329D6`, and context conversion shared by `6B760` and `6B8BA`.
The generator extracts native curves and constants from the reference image;
`oem-input-layout.json` records the context source addresses. The context
conversion retains native saturation, division-by-zero handling and the two
byte-wrapping conversions. Converting an explicitly supplied native context
word does not add an LTFT controller to this standalone firmware.

The IAT producer retains the OEM counter and filter behavior, including the call
after a debounce counter reaches zero, the coolant-dependent substitute, and
allocation feedback through its live event descriptor. `oem_diagnostics.c`
binds shared RAM identities between IAT, coolant, event storage, operating state,
context and MIL. Its entry points preserve these normal-list sequences:

- Divider 10: IAT producer and ingestion, then coolant producer and ingestion.
- Divider 100: IAT capture, then coolant capture.
- Divider 20: drive phase, warm-up phase, aggregation, then steady-MIL policy.
- Separate base-task context and operating-state entries preserve their native
  ordering without claiming to implement the intervening OEM tasks.

The composed tests execute the complete original routines, including native
ingestion. They compare all bound fields and shared aliases. A retained history
exercises both sensor faults, recovery, record allocation, operating state,
context and MIL without copying oracle state back into the replacement after
each call. It uses synthetic native inputs and a subset of normal tasks; it is
not a board-input or complete application test.

`oem_cadence.c` preserves the divider startup counts 1/2/4/10/20, reload counts
2/5/10/20/100, signed-byte expiry test, rejection-counter wrapping and five-call
period adaptation from `28D9A..28EB6` and `28F3A..28F60`. The caller supplies the
previously published `F8AC` speed byte. The period is 781..976 T4 ticks; this means
9.9968..12.4928 ms only at 20 MHz with a /256 timer. Task release delivery remains
an explicit owner boundary. Tests execute the original RTOS delivery and timer
setter to compare accepted and rejected releases; they do not prove task
preemption or hardware timing. The standalone foreground currently still runs
its separate 10 ms control pass. Do not use that fixed period to claim native
diagnostic cadence equivalence.

The supplied training document PDF pp.11-12 describes starting/running and idle
strategies; p.16 describes intake-temperature use. These are functional context,
distinct from the exact ROM thresholds and native-unit conversions above.

For runtime integration, retain the distinction between the tunable standalone
fueling state and this native diagnostic operating state. In particular, native
`FD6A.6` is qualified from successive capture periods by `688BC..68A58`; it is
not equivalent by definition to the standalone decoder's `ROT_VALID`.
Supplying engineering-unit RPM, Celsius or a guessed true rotation flag would
invalidate the trigger claim.

## Rotation input conditioning

`oem_rotation.c` now ports these complete native routine contracts:

| C entry | ROM routine | Contract |
|---|---|---|
| `oem_rotation_threshold` | `68548` | Capture threshold from native divisor at `162EE` |
| `oem_rotation_reset` | `6860A` | Capture/phase reset and requested PEC register state |
| `oem_rotation_capture` | `688BC`, including `669F0` | 24-bit capture subtraction, period history, rotation flags, preliminary gap/phase qualification |
| `oem_rotation_period` | `668B2` | Native full-period speed update and asymmetric speed filter |
| `oem_rotation_speed` | `66A10` | Stopped-state publication and native speed-byte scaling |

The code preserves first-capture and byte-counter wrap behavior, saturation of
the shifted 24-bit interval, strict threshold comparisons and retained speed
branches. The two phase-window calculations differ at byte rollover; that
native distinction is retained. The full-period path keeps its two division
forms and its filter branch that leaves the previously published byte intact.

`OemRotation` contains the affected RAM and SFR values as ordinary data. It
never writes the standalone board's timers, PEC or interrupt registers. Its
mapping is recorded explicitly in `tests/test_oem_rotation.py`. The test runs
13,740 routine comparisons against the original ROM, including retained
trajectories. It also executes the actual Keil-linked replacement with
`--target`. This establishes the routine contracts, not an emulation of PEC
transfers, input edge acquisition or their full interrupt sequence.

The native capture ISR at `2B0B6` snapshots `CC15`, builds the high capture byte,
and branches on `FD08.6`: one path invokes `688BC`, while the other consumes a
completed PEC buffer and reloads a 30-word transfer. The synchronous task
`29082` calls `668B2`; the base task `29290` calls `66A10`. Therefore invoking
the new capture routine on every standalone tooth indefinitely would not
reproduce the original sequencing. Board observation, overflow qualification,
task ordering and the native rotation-loss/reset paths remain integration work.
The composed diagnostic API continues to require these native inputs explicitly.

## Vehicle-speed diagnostic producer

`oem_vss.c` ports `2A01E..2A296` (event68) and its clear-request reset at
`2A298`. Normal divider10 entry12 precedes supply-voltage entry13. The composed
API publishes its descriptor through the original event-manager contract and
shares `FD06` with coolant, `FD08` with MIL, and event68 with voltage. Source
descriptors are events18/19 (hex); source selector `94A3`, status bytes
`9477/947D`, `FD18.5`, `FD52.3` and `FD5E.13` remain explicit native inputs.
Their availability is not inferred from a plausible standalone speed value.

Immutable bytes `1001F..10026` are 75,75,100,80,50,144,8,8. The two physical
qualification branches require coolant above144 and `95A4` below1280. Branch A
also requires engine-speed byte strictly between75 and100 plus `FD18.5`;
branch B requires engine-speed byte above75, load byte above80 and `FD52.3`.
These are native coordinates, not RPM, Celsius, km/h or milliseconds. Failure
and recovery use increment-before-compare byte counters with threshold50,
including wrap. A qualified fault remains latched until qualified recovery;
its subtype changes from4 to8 when its immediate failure condition disappears.

The alternate-source path qualifies selector/status/descriptor failure through
a separate countdown. `FD06.8` selects which fault policy is used, while
`FD5E.13` supplies completion for the alternate path. Shared speed flags retain
the exact comparisons `95AA >= 640` and `95AA < 256`. Reset clears the two
physical counters and the latch only when the descriptor has bit7 set; it
retains the source countdown and other flags.

The supplied training document PDF p.44, technical p.38 section XXIV, describes
a 12 V Hall sensor at the gearbox output and gear inference with engine speed.
That is functional context, not proof of this image's source selection,
electrical thresholds or task periods. The separate input contract below now
provides `95AA/95A4`; board acquisition and runtime binding remain pending.

`test_oem_vss.py` passes 6,976 native/ROM and Keil-linked comparisons, including
strict operating thresholds, both source policies, counter wrap, retained
fault/recovery and reset. The composed suite separately executes complete
original event ingestion and checks shared state with the other producers.

## Vehicle-speed input conditioning

`oem_vss_input.c` ports initialization `29C64`, the full divider5 routine
`29CCC..29FAC`, and the state-changing CC14 ISR payload `29FC2..2A016`.
`OemVssInput` contains requested register values as ordinary data. These
functions do not write the standalone timer, capture or PEC registers.

The physical path consumes batched pulse timestamps. Initialization sets PEC5
source/destination to `FE9C` (CC14) and count1. The ISR records **T1 at FE44**
after the batch, shifts timestamp/batch history and reloads the next batch.
The task snapshots that history under native ATOMIC4, calculates the wrapped
period, chooses the next batch from1 to4, filters speed and publishes `9201`.
The port preserves native zero-divisor saturation, integer-division order,
fractional-filter borrowing, signed acceleration and conditional pulse-total
wrapping. It also keeps the distinct stale-input and restart branches: a new
batch after the longer gap discards that first interval, while the no-new-batch
path eventually drives the target to zero and resets the requested batch to1.

The alternate path selects `9ACE` or `9AD0`, scales by32/25 below51200, uses a
different filter, substitutes the stock zero value on source failure, and
bypasses its filter on the first recovery. Its acceleration byte has a separate
source/status gate and arithmetic-right-shift rounding. These paths retain
different state fields; they are not collapsed into a generic speed filter.

`oem_diagnostics_vss_input` publishes the resulting speed words, source-status
bit, shared `FD08` and context speed byte. The composed test executes the
original divider5 routine before applicable divider10 producer releases.
`test_oem_vss_input.py` adds 7,046 native/ROM/Keil comparisons, including a
1,200-step retained pulse/timeout/source history. Capture comparisons cover the
ISR payload, not its register-bank save/restore or physical PEC transfers.
The standalone owner must still acquire and serialize equivalent timestamp,
batch and source inputs at the native cadence; neither a software sample of
km/h nor one call per standalone VSS edge automatically provides that contract.

## Digital input filtering and publication

`oem_digital.c` ports initialization `72696`, complete sampling/filtering
`726CE..72792`, flag publication `2B8E8..2B9EC` and divider10 entry2 `2BA18`.
Both normal and alternate base tasks call the filter before publication. Input
bits0..6 correspond to P4.4, P6.3, P5.2, P5.3, P8.4, P5.4 and P8.6. Each
published bit changes only after three equal consecutive samples. Native
full-word history is preserved, including the unsampled upper bits; this is a
call-count contract rather than a fixed-time debounce.

Publication copies filtered P4.4 to `FD08.13`, P8.6 to `FD08.15`, P8.4 to
`FD0A.2`, and inverted P5.2 to `FD0A.3`. Divider10 separately copies inverted
P5.3 to `FD0A.4`. The P6.3 path retains its exact `81C4/81C5` transitions,
`FD08.14/FD0A.0` flags and one-time timestamp capture. Pending T1 overflow
increments the captured high word with native 16-bit wrap. Native DP2.6/7
direction requests and the conditional F881-to-F880 copy are represented as
data only; this routine never changes the standalone board's port directions.

`oem_diagnostics_digital` shares FD08 with MIL and the speed paths. Its clock
low word shares the native T1 identity with speed acquisition. The caller must
supply coherent port/clock observations; the pure state API does not acquire
them or emulate concurrent overflow. `test_oem_digital.py` passes 13,569
native/ROM/Keil comparisons, including all three-sample histories for each pin,
pulse-state combinations, timestamp retention/wrap and an 800-step history.
The composed test additionally executes the original routines and compares
their shared state with the other producers.

The supplied training document PDF p.9 lists power latch and autodiagnosis as
ECU functions; it does not establish these pin-to-connector assignments. The
proven P4.4-to-FD08.13 route also feeds the native alternate-task/shutdown
sequence (knowledge-base `alg.scheduler_mode_input`). Its connector identity
and the load on P3.12 remain unresolved. This implementation preserves those
facts without relabelling the raw signal as a proven ignition-switch input.

## Evidence and verification boundary

`tests/test_oem_records.py` compares complete records, live descriptors, phase
bytes, gate and request state against execution of the unchanged ROM. It covers
randomized states, callback traces and a retained sequence of competing events.
With `--target`, the same calls also execute the actual Keil-linked instructions
and compare their return values and complete state bytes with the native C DLL.
This exercises 16-bit arithmetic and far-pointer conventions.

The supplied Citroen training document, technical pp.44-47 (PDF pp.50-53),
describes equipment-dependent fault capacities, lamp applicability, associated
variables and automatic erasure after warm-up cycles. These are document-derived
functional expectations. They do not prove the reference image's exact producer
thresholds, phase transitions, scheduler timing or persistent storage layout.

The native task lists place maintenance in the normal background list and aging
in its alternate list. Drive-phase, warm-up and aggregation occur consecutively
at indices 20, 21 and 22 of `task.divider_20`; timestamp advancement is index 12
of `task.divider_100` (`engines/TU5JP/defs/task_lists.csv`). Those call-site facts
do not alone establish wall-clock periods, startup flags or shutdown ordering.

Still required: remaining monitor producers and their native conditioning,
remaining phase generation and scheduling, persistent storage/history, board
inputs for context conversion, protocol exposure and runtime integration. The
composed contracts are not yet wired into a complete running diagnostic
lifecycle. No physical MIL flashing is implemented, and no LTFT or fabricated
passing OEM equipment inputs are introduced.
