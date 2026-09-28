# Current timing implementation

The [OEM-style scheduler](OEM-SCHEDULER.md) replaces the per-tooth implementation
measured below. PEC captures timestamps, XP1 prepares events, and short compare
handlers perform staged output timing. Current continuous-test results and
artifact identities for the September 23 fixes are in
[VERIFICATION.md](VERIFICATION.md). The earlier
[implementation audit](../../../docs/audits/tu5jp-standalone-oem-scheduler-2026-09-22/FINDINGS.md)
is retained as historical evidence.

After building an output-enabled profile:

```powershell
python firmware/tu5jp_standalone/tests/test_oem_scheduler_target.py
python firmware/tu5jp_standalone/tests/test_oem_scheduler_faults.py
python firmware/tu5jp_standalone/tests/test_scheduler_regressions.py
python firmware/tu5jp_standalone/tests/test_output_closure_target.py
python firmware/tu5jp_standalone/tests/test_foreground_target.py --stock-95080 --rotating --scenario tuning
python firmware/tu5jp_standalone/tests/test_oem_timing.py --target --engine-experimental
```

Use `--stock-95080` for the stock-storage profile. Physical timing acceptance
remains open. Full-boot/foreground tests now cover rotating acquisition, real
control plans, tuning writes, watchdog service with latched faults and rotating
run-permission reassertion. Their scope is digital emulator execution. The material below is
retained historical evidence; its old scheduler and timing scripts were superseded.

The scheduler regressions require every expected event, including trailing
pulses, rather than accepting a short alternating edge sequence. Use `--case`
to select fuel, load, gap, race, noise or tuning. `OEM_SCHEDULER_BUILD` selects a
preserved HEX/map directory for negative controls. Reports under
`build/oem-scheduler/<profile>/` identify the exact linked image.

---

# Capture timing investigation, 22 September 2026

**Timing acceptance remains open.** The
[timing-fix investigation](../../../docs/audits/tu5jp-standalone-timing-fix-2026-09-22/FINDINGS.md)
supersedes the fixed-phase counts below. The optimized source clears the original
2610-case fixed-phase sweep, but still overruns when an injection start coincides
with immediate dwell admission. Advancing-timer probes also demonstrate the
minimum-dwell output-fault shutdown; a passing frozen-clock cost sample alone
cannot establish a running-engine contract.

The follow-up uses bounded word arithmetic in the decoder, direct MULU/DIVLU
angle conversion, one angle calculation for both coil channels, separate fast
and slow scheduling paths, and fewer redundant pointer/clock/wait operations.
Cut, epoch, stale-plan, absolute-deadline and slow-cranking wait checks remain.
These changes reduce software cost; advance event preparation and a complete
interrupt/edge-latency budget remain necessary. See the linked investigation
for current hashes, measured costs, stack tradeoffs and the concrete repair plan.

Additional reproducible probes after building an output-enabled profile:

```powershell
python firmware/tu5jp_standalone/tests/test_timing_target.py --require-budget
python firmware/tu5jp_standalone/tests/test_timing_target.py --coincident --require-budget
python firmware/tu5jp_standalone/tests/test_timing_progress.py
```

Add `--stock-95080` for that profile. The coincident command intentionally still
returns nonzero. The progression test checks cancellation and fixed deadlines,
not schedulability; it has no interrupt dispatcher or physical compare outputs.

## Earlier continuation (retained evidence)

In the earlier image, the highest-priority crank interrupt could
still exceed the exercised 100 us tooth interval in the instruction-cost model.
Neither isolated costs nor this expanded sweep establish a safe RPM limit.

The continuation changes two exact calculations:

- `us_ticks(us)` uses `us + (us >> 2) + (us % 4 != 0)`. It preserves
  `ceil(5 * us / 4)` and the original unsigned 16-bit narrowing for all 65536
  inputs, without software long multiplication.
- For normal periods up to 1092 ticks and angles up to 3599 tenths, scheduling
  decomposes the angle into whole teeth and a remainder before multiplication.
  Each product and the final result fit in 16 bits. Slow-cranking periods retain
  the extended arithmetic and staged waits across timer wraps. The output
  admission, epoch, cut and absolute fire-deadline checks remain in place.

The unused scaled bounds are no longer calculated on the fast path, and a dwell
wait reuses its tick conversion. Native checks exhaust the 1093 x 3600 fast-path
domain and every unsigned 16-bit microsecond input. Compiled C166 checks exercise
the fast-path boundary, fractional angles and narrowing boundaries separately.
Existing register-model tests cover delayed dwell, timer wrap, slow gap waits,
cuts, noise rejection and output cancellation.

The original audit's isolated high-RPM `board_schedule` maximum was 2074 modeled
CPU cycles. The same sample now measures 1828. A new `test_timing_target.py`
sweep calls the actual linked `capture_isr`, including rotation decoding,
diagnostic capture, scheduling and immediate output admission. It covers 2610
combinations of tooth, period, dwell and advance per output-enabled profile.
It reports 25 modeled overruns, with a worst high-RPM sample of 2658 cycles
against a 2000-cycle interval. These samples deliberately start with idle output
channels; they are sampled entry states, not a continuously simulated engine.

The budget uses the inherited 20 MHz CPU / 1.25 MHz capture-clock assumption:
16 CPU cycles per capture tick, with three normal intervals after tooth 57.
Timers remain scripted during execution. Costs exclude interrupt entry/RETI,
external-memory wait states, competing interrupts and physical input/output
latency. Reported stack use is an isolated call high-water mark and excludes
hardware interrupt entry and nested-interrupt interference. **All these limits
must be included before timing release.**

Run after building the corresponding profile:

```powershell
python firmware/tu5jp_standalone/tests/test_timing_target.py
python firmware/tu5jp_standalone/tests/test_timing_target.py --stock-95080
```

The default command verifies arithmetic and records the open timing gate.
Add `--require-budget` to exit nonzero when any sampled path reaches or exceeds
its modeled interval. Results include the linked HEX hash and each observation
under `build/audit-regressions-target/*-capture.json`. A budget pass would still
be insufficient for physical WCET acceptance.

Supplied evidence: the Bosch training PDF, SHA-256
`7aa7ac81f2ece26f378930c02fd3d7db3a3092bcbbd340f56540b7fe7a57cd69`,
PDF p17 / technical p11 describes the 60-2 wheel and six-degree tooth spacing.
It does not establish these standalone clock settings, costs or schedulability.
The measurements above are linked standalone software evidence, not OEM-ROM
behavior or evidence about TU1JP/TU5JP4 images.
