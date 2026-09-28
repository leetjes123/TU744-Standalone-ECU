# OEM-style timing implementation

The standalone now uses PEC crank capture, deferred crank processing and
counter-to-timer ignition compares. Coil edges remain software controlled;
injector edges use compare-toggle hardware with start/end interrupt handlers.
The default build still inhibits engine outputs. No ECU was flashed.

## Evidence and scope

The reference image is `bins/M744_C167_FULL.bin`, SHA-256
`5710015f7c5c066c860a1757fd893f305701608c25af8ff23bfcb4fd1e4837b3`.
Addresses below are file offsets in this TU5JP image only.

Supplied functional evidence is Citroen technical training 1.3.277,
`docs/ecu Bosch me7.4.4.pdf`, PDF p17 / technical p11 (60-2 wheel and six
degrees per tooth), and PDF p30 / technical p24 (twin static ignition and
DEPHIA). Its SHA-256 is
`7aa7ac81f2ece26f378930c02fd3d7db3a3092bcbbd340f56540b7fe7a57cd69`.
The document explains function; the following implementation evidence comes
from the ROM, not the training document or another engine's firmware.

| Original TU5JP evidence | Implemented behavior |
|---|---|
| `2B0B6..2B120`: CC15 completion rearms PEC2 with 30 words on this ROM path and requests XP1 | PEC channel 6 (CC15 at level 15); three alternating 30-word IRAM buffers; aligned 30/28 delivery is a standalone adaptation; XP1 decodes completed blocks |
| `T01CON=414A`: T0 counts falling crank edges; CC15 captures free-running T1 | Same counter/capture arrangement; T0 and PEC transfer counts must agree |
| `37F5C..37F80`, `37E04..37E28`, `3813E..3814C`: eight fractions per tooth, borrow a tooth near the boundary | Portable angle split; extra CC6 running-path borrow at fraction two; missing positions anchor to the preceding real tooth |
| `388A8`, `3897E`, `38A9E`: CC0/CC6/CC4 refine T0 comparisons against T1 using the latest PEC interval | Coarse compare followed by fine compare; latest interval validation. The ROM path has no hardware coil toggle; the standalone final fire stage uses compare mode 1 (toggle) on CC0IO/CC1IO |
| CC6 clears a selected P2 bit and records T1; CC0 sets it and arms DEPHIA | Software charge/fire edges, CC9 charge feedback and existing CC8/DEPHIA ownership |
| Selected CC6 path `38A8C` programs firing from serviced T1 plus F7FC | Explicit starting-mode adapter uses charge-relative duration; normal running retains angular firing |
| `38BDC..38D5C`, CC9 `38D8C..38DB6` | Ported dwell correction, captured interval, missing-feedback increase, prior-fallback hold, ceiling, fall limit and minimum; preserve the ROM's old-correction duration in the fall-limited branch |
| CC30/29/28/23 start/end handlers | Hardware injector start and end edges, CPU handler installs the end compare and then parks it |

The older reverse charge/fire labels are retained as superseded provenance in
the knowledge-base ignition-edge question. Actual board polarity and the CC9
electrical signal still require measurement.

## Explicit standalone adaptations

This is the same architectural approach, not complete OEM scheduling parity.

- XP1 prepares events from the published engine plan. Fuel/ignition calculations
  still run in the existing 10 ms foreground pass; Bosch's complete ordered
  XP1 calculation chain has not been transplanted. XP1 uses priority 9/group 2
  here, below the 1 ms tick; the original uses hardware priority 4/group 2.
- Ignition scheduling is the ROM's own segment scheduler, ported literally
  (2026-09-26): `src/oem_ignition.c` is sub_37CA0 (37CA0..387B4) with
  sub_3886E. `tests/test_oem_ignition.py` runs the unchanged ROM routine and
  the port on identical inputs, 40,000 cases with all state identical.
  `board_ignition_segment` runs one pass at each boundary capture (teeth 1 and
  31, as the ROM's 30/28 segments), before the block is decoded, as the ROM
  runs sub_37CA0 straight after CC15INT. Inputs use the ROM's units: T1 per
  0.75-degree count from the measured segment period, the fire position 144
  counts after the boundary minus the advance, and the dwell. The tune's
  trigger10 is honoured. The ROM's CC0 (fire) and CC6/CC4 (charges) map onto
  per-coil channels. Coarse stages compare T0 and the fine stages are the ROM
  formulas (`oem_angle_refine`). Where the ROM sets a compare's request while
  its low-priority pass runs, a hook applies that handler at the same point.
  The charge compares are cancelled where the ROM cancels them (38036). Maps
  and the ROM/port Keil timing comparison are in
  `docs/audits/tu744-high-rpm-2026-09-26/oem`. Behaviours kept from the ROM:
  - the lead is adaptive: the worst measured capture age F800 + 250 ticks;
  - events closer than the lead go straight onto T1, or are serviced at once;
  - the coarse tooth is clamped away from the missing teeth and the first
    tooth after the gap;
  - a dwell that cannot be started in time moves the next fire later
    (at most 192 counts) instead of shortening the charge;
  - a charge that should already have started charges at once and fires by
    time.
- Regular fast delivery uses aligned 30/28 capture blocks. Fast acquisition also
  uses 30-word blocks without asserting their phase. Slow input uses individual
  captures. The 1092-tick switch is a standalone boundary. Choosing it from the
  fastest accepted interval in a block prevents a malformed gap from switching
  to single-word completions while a large block is still being decoded.
- Fire compares are CC0 and CC1, with CC6 and CC4 for charge starts, one per
  coil. The OEM shares CC0 and switches coil pins in software. Here the timed
  fire stage toggles CC0IO (P2.0, coil A) or CC1IO (P2.1, coil B) in hardware
  at the compare instant. A pass's fire is therefore installed only while its
  coil charges; otherwise it is kept until the charge starts. Until the pass
  delivers the angular fire, a charging coil holds a fallback timed fire after
  its dwell. Every charge still passes the shared safety owner (`coil_admit`).
- Injector start and end both use free-running T7. Bosch changes from T7 to
  T8 and accounts for capture age; standalone T8 remains allocated to boost.
  One pending start per pair can coexist with its active pulse. After both end
  handlers finish, the queued start is admitted using the current plan's pulse
  width, freshness, epoch and cuts. Queue cancellation follows fuel cancellation.
  Starts less than 64 ticks ahead at this handoff raise `INH_DEADLINE`; immediate
  late starts and continuously overlapping pulses are not implemented. This
  borrows the OEM's pending-start ownership, not its complete T7/T8 algorithm.
- The standalone rpm-dependent early coarse entry (<= 250 T1 ticks, i.e.
  from 5,000 rpm, documented as about 4,000) is removed. It coincided with a
  reported 4,750-5,000 rpm ceiling (`docs/audits/tu744-high-rpm-2026-09-26`).
  The ROM's adaptive lead replaces it.
- The feedback arithmetic and stock TU5JP constants are ported. Base dwell
  remains tunable. Byte 0x93A explicitly enables CC9 correction; zero selects
  calibrated dwell. Missing/invalid feedback resets correction, and command 0x2E
  reports it. Starting/overlap eligibility remains a standalone adaptation.
- Injection keeps the standalone claim-once-per-revolution scheduler after the
  block decode. Coarse compare installation rereads T0 and requests service if
  the counter reached the target while the compare was being programmed. A
  lost charge compare is rescheduled by the next segment pass (38036), not a
  deadline fault. A fine stage serviced one tooth late anchors to its own
  tooth's capture; later than that it is dropped and counted in `late_events`.
- Every rejected electrical crank edge revokes synchronization, including in
  single-capture mode: T0 already counted that edge even when the decoder rejects
  it. Reacquisition is required before scheduling resumes.
- Nominal dwell retains the existing 500..6000 us range. The old requirement
  for another full 500 us *remaining after ISR delay* is removed. Dwell is
  additionally limited to 464 eighth-tooth units, reserving two teeth between
  consecutive uses of a coil. This board admission bound is not an OEM constant.
- T1 remains at /16. A fine stage at or beyond half a timer wrap is rejected
safely. Extremely slow rotation
  with a firing position deep in the missing-tooth gap needs further work;
  complete low-speed OEM equivalence is not claimed.

A counter/capture mismatch (lost PEC transfer) revokes angle and
resynchronizes without a reset. Queue exhaustion latches DEADLINE until reset.
Deadline/output faults require reset. A malformed gap cancels fuel and
future charges, retaining an active coil's predicted firing deadline. Other soft
revocations also drain the charge; output/board faults and traps remain immediate
electrical safing. Fresh synchronization and atomic foreground condition/plan
publication permit recovery of recoverable inhibits. Coil
watchdogs remain interrupt driven, so they do not protect against every CPU fault.

`ecu_poll()` continues watchdog service while clock and foreground make bounded
progress, including with `INH_DEADLINE`/`INH_OUTPUT` latched. It does not turn an
output latch into an automatic reset. A stopped clock/foreground still prevents
service. The emulator does not model the watchdog timeout. An emergency off edge
can still spark; the physical coil stage remains unmeasured. Soft revocation's
retained deadline removes the reproduced arbitrary early turn-off.

## Verification

`test_oem_timing.py` compares 5,920 dwell cases and retained-state transitions
against the unchanged ROM. `--target --engine-experimental` also compares the
actual Keil-compiled routine with the native implementation. The independent
OEM interrupt tests cover the original compare stages, CC9 and selected fallback.

`test_oem_scheduler_target.py` executes linked firmware with advancing timers,
PEC, interrupt arbitration, vector entry/RETI and pin transitions. Its 16 cases
cover 200 RPM starting, 1,000/4,000/10,000 RPM, 500/3,000/6,000 us requested dwell,
ADC/tick interference, early injection, altered advance, 16-bit and 32-bit clock
wrap. It checks output edges, injector widths, firing error, inhibition,
capture loss and masked intervals. `test_oem_scheduler_faults.py` exercises
lost captures, wrong gaps, recovery and stale-plan cancellation.

`test_scheduler_regressions.py` adds exact start/completion counts across all
four injector and both coil pins, with complete observation windows and trailing
pulse drainage. It covers long pulses including 83% duty, post-gap dwell starts,
both coarse fire install races, an alternate match-on-install test model,
expired charge descriptors, single-edge noise (including during charging), and
actual framed tuning commits under live scheduler/T6 interrupts. Native tests
exercise pending-start ownership, cancellation, cuts, stale plans and timestamp
wrap, plus the original running-calibration protected-byte contract. The tuning
optimization scans only protected byte ranges; schema validation and atomic
publication remain in place.

The previous isolated/frozen-clock scripts are retained in the dated audit;
their entry points now run the live scheduler or fault tests. Their old
25/2610 result describes the previous per-tooth implementation, not this one.

The original live harness supplies the plan and foreground heartbeat. Additional
`test_output_closure_target.py` cases exercise capture ownership, feedback loss
and changing speed. `test_foreground_target.py` runs actual reset/main/control
code with emulated storage/ADC: no supplied plans, heartbeat or inhibit clearing.
It checks output continuity, live tuning, faulted watchdog service and rotating
restart requests. None qualifies physical bus waits, interrupt entry timing,
coils, injector drivers or feedback electrics. Runtime ROM
integrity, firmware recovery, engine abstraction and hardware validation remain
separate open work. See the [audit evidence](../../../docs/audits/tu5jp-standalone-oem-scheduler-2026-09-22/FINDINGS.md).

The emulator fixes used by these tests follow the
[C167CR User's Manual](https://www.keil.com/dd/docs/datashts/infineon/c167cr_um.pdf):
CAPREL reload (section 10), programmed ADC timing (section 17.2), and interrupt/
PEC exclusion during EXT instructions (section 21.9). Capture timestamps now
sample the advancing timer at the edge, and external counter matches request
their interrupt at that edge. These changes have separate peripheral tests.
