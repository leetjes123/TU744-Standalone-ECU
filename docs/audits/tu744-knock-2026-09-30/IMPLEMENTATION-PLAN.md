# TU744 knock measurement control and monitoring implementation plan

Implement crank-synchronous knock measurement and **one global knock controller** in `C:/Users/leetj/Documents/TU744`. Any eligible detected knock event changes the retard used for every spark. Include **AN15 output voltage and a knock detected variable in ordinary monitor data**, with matching TunerPro and Wizard displays. Verify the compiled implementation in Keil using simulated AN15 input before bench and engine acceptance.

This plan specifies work to implement. No firmware changes or new Keil detection tests have been performed yet. The [audit and OEM evidence](README.md) provides image hashes, document references, measured boot results, timer allocation, and unresolved questions. Its per-window timing recommendations remain applicable; this plan makes telemetry, test semantics, and delivery requirements explicit.

## Behavior and evidence boundaries

The source of defaults is the unchanged TU5JP `M744_C167_FULL.bin`, SHA-256 `5710015f7c5c066c860a1757fd893f305701608c25af8ff23bfcb4fd1e4837b3`. File offsets must not be reused for other engine images. Citroen training PDF page 19, technical page 13, supplies M7.4.4's 3 degree attack, 12 degree maximum and progressive recovery; page 17 supplies the 60-2 wheel. See the audit for the verified PDF hash.

Keep OEM gate/sample ordering, offset and drift correction, gain ladder, normalized detection arithmetic and internal health checks. The shared reference/gain/threshold and global retard are intentional standalone changes. Do not describe the complete controller as an exact OEM port. Default shared threshold is the pointwise minimum of the four OEM threshold curves, subject to monitor-mode validation: combining firing-event noise distributions can change sensitivity even with a lower threshold.

AN15 is the CC195's **held integral voltage**, not knock audio. The chip requires MCU outputs on P3.1..3, P3.5, P3.6, P8.0 and P8.5. A simulated AN15 value tests ADC acquisition, numerical detection and control integration. It cannot prove the physical sensor, band-pass filter or IC discrimination. No per-cylinder output, physical cylinder identity, DEPHIA prerequisite, learned cylinder matrix or persistent knock adaptation is included.

Four operating modes are required: disabled, monitor-only, global control, and engine-stopped bench measurement. Internal self-test is a separately tagged sequence. Invalid or self-test/null samples do not become knock events. Monitor-only never alters ignition, and switching to control starts from an explicit controller initialization rather than importing a hidden accumulated monitor-mode retard.

## Phase 1 Source baseline and test infrastructure

1. Record TU744's current source/tune hashes and existing changes. The stored stock-profile build differs from seven current source files; rebuild before claiming current-source results. Work in an isolated copy under the reverse-engineering workspace until changes are ready to apply to TU744.
2. Give all OEM tests an explicit `--oem-repo` option or `TU744_OEM_REPO` environment variable. Validate the image hash and paths. Remove assumptions that TU744 is two levels below this repository. Fix the old `docs/KNOCK.md` link paths and replace its per-cylinder plan with the global design.
3. Preserve the existing boot comparison in the emulator and Keil. The old Keil boot harness allows 600 seconds; change its timeout to **180 seconds maximum per UV4 process**. Use headless scratch projects and no more than three concurrent UV4 instances.
4. Record compiler/toolchain, clock, profile, tune, image, map/listing and simulator workaround versions in each test manifest. Extract symbol addresses from each freshly linked image; never reuse standalone addresses from an older build.

Deliverable: a reproducible current build, working OEM test discovery, and baseline regression results for stock-95080 and applicable development profiles.

## Phase 2 State calibration and interfaces

Create `include/knock.h`, `src/knock.c` and `target/c167/knock_hw.c`. Keep detection and the global governor independent of MCU registers. Define bounded interfaces for configuration publication, reference events, opening/closing windows, injected samples, health updates, service jobs, reset/invalidation and an atomic telemetry snapshot.

Runtime state needs: mode and health; normal/null/test sample type; raw 10-bit AN15; offset-corrected 8-bit amplitude; reference, gain index/code, ratio and threshold; measured null/drift state; window phase and capture epoch; sample/calibration generations; timestamps and sequence; knock count; last-knock time; requested and scheduled global retard; recovery hold; ADC timeout, stale-sample and missed-window counters. Use explicit integer widths. Timestamp/sequence snapshots must be coherent on the 16-bit MCU.

Reserve and document a knock calibration block within `0xA00..0xBFF`, after checking all current consumers. Store the 16-point RPM axis, start/length curves, shared threshold curve, load-enable curve, attack/max/hold curves, gain ladder/seeds, offset/drift and diagnostic limits, reference filter divisor, enables, mode and monitor latch interval. OEM start-map columns are identical in this image, so a single curve is sufficient for initial global operation; retain provenance for the original map.

Generate copied defaults from the hashed image. Derived defaults, such as the minimum across four threshold curves, need a test for the stated derivation. Bump calibration schema, preserve the 3072-byte size, and provide schema-4 migration that retains existing settings and initializes knock enable/control **off**. Update `cal_validate`, generators, XDFs, Wizard definitions, basemaps and storage round trips. Reject zero divisors, invalid gain indexes/codes, invalid axis order, excessive angles, attack above maximum and timing settings outside measured bounds.

Current ISRs consume copied plans, not calibration-bank pointers. Continue that ownership rule: evaluate and publish a coherent knock configuration snapshot in foreground; a window retains its configuration generation through sample consumption. No ISR may keep a pointer to a calibration bank that a subsequent transaction can reuse. Geometry/filter/mode changes are stopped-engine changes. If curves become live-tunable, split `structural_ranges` narrowly and define which window first uses the new generation.

Minimum coolant remains native code `0x68`, with the appropriate OEM hysteresis, until `q.temperature_code_offset` is resolved. The OEM load gate is cylinder filling, not MAP kPa or TPS percent. Define and validate a standalone conversion/enable curve; document this deviation. Omit unresolved forced-enable and alternate OEM flags explicitly. Do not add unproven knock fuel enrichment; retain `q.knock_fuel_enrichment_join`.

## Phase 3 ADC hardware and bench jobs

Keep existing boot initialization. Enable ADWR and ADCIN while preserving the existing scan timing and channel order. Implement `hal_knock_sample` using the OEM save/select/request/read/restore ADDAT2 sequence, with serialization and a bounded completion deadline. Derive the timeout from programmed ADC timing plus measured preemption, rather than borrowing an arbitrary OEM polling limit. Always restore state on timeout and expose failure. Do not block indefinitely or hold the global interrupt mask over conversion.

Keep the continuous scan's `ecu.adc[15]` separate from the window-synchronous knock sample. A later scan must not overwrite the measurement used by detection. Idle voltage can come from the scan; running knock voltage comes from the injected sample with source and age recorded.

Reserve proposed command **`0x34` for knock details** and **`0x35` for stepped bench jobs**, after rechecking all clients and tools. `0x31` remains TPS calibration. Bench operations are start normal measurement, start internal/null/gain self-test, query job results and stop/cancel. Add `tune_client.py` actions `knock-details`, `knock-bench-start`, `knock-selftest`, `knock-job`, and `knock-stop`. Do not expose a production command that sets a simulated ADC value or forces the detection variable.

Only start bench jobs with the engine stopped, no turning crank, permitted service state and no competing calibration/storage/update job. Step jobs without starving foreground progress. If crank activity appears, cancel immediately. Restore MF low, KTI off, sensor selection and normal gain on completion/cancel. Respect the existing service inhibit lifecycle; stopping a bench job must not silently clear latched engine safety inhibits.

Normal bench mode repeatedly opens a documented fixed-duration window and reports voltage/ratio/events. A tap may produce a detection here but is not combustion knock. Null and internal-test jobs use OEM durations/gain conditions. Judge start offset, drift and test shift using their separate OEM limits; the pulse shift limit is 179 eight-bit counts. Use three-result diagnostic debounce where the corresponding OEM health test requires it. Self-test results are returned by the job packet and never drive global retard.

## Phase 4 Running windows detection and global control

Use a free **CC2 compare on T0** for the reference at boundary counter +18, and **CC16 on T7** for open/close. Proposed priorities are level 6/group 3 and level 5/group 3. Verify channel/vector allocation from the fresh linked image and modify only the relevant CCM nibbles. Preserve spark/charge channels, boost CC19 and T7's existing time base.

Arm from the captured crank boundary counter/stamp in `capture.c`, checking that the reference has not already passed. On opening, raise MF and schedule closing relative to the previous compare before running processing. On closing, lower MF and perform the injected sample. Tag the sample with epoch, configuration, gain, type and time. Match OEM previous-sample processing order first; any move to a deferred worker requires a demonstrated deadline before the next required gain/ignition preparation. Budget the path under all higher-priority work and existing `hal_lock` sections.

Use 32-bit periods/products for `angle_count * segment_period / 240`. If matching OEM quantization, compute in OEM timer units before conversion to T7. Check wrap, late installation, short windows and RPM ramps. Confirm the absolute crank angle relative to TU744's tune `trigger10`; the prior inferred ATDC label is not sufficient evidence. Cancel pending windows and samples on sync/epoch loss, stop, update, service or invalid configuration.

For ordinary valid samples, port saturating offset subtraction, normalized ratio, threshold equality and overload detection, reference adaptation and gain autorange from the OEM. Handle ratio overflow and reference zero deliberately; use the ROM differential tests to determine exact arithmetic before introducing standalone limits. Do not route AN15 through generic sensor rejection that discards every high sample: the OEM overload branch can deliberately interpret a high valid integral as knock. Persistent rail values need separate health qualification.

Global governor: each eligible event adds 4 counts (3 degrees), ceiling 16 counts (12 degrees), reloads the quiet hold, and prevents recovery on that event. Recovery removes 1 count (0.75 degree). Use the previously specified standalone hold of four times the interpolated OEM hold count, ticked once per 180-degree reference event with valid sensing. Counter width is at least 16 bits. At 4000 RPM, H=90 becomes 360 references, approximately 2.7 seconds. This recovery clock is a stated deviation from unresolved OEM flag-qualified recovery.

Invalid/null/test samples neither attack nor establish quiet sensing. Null/test windows must not accelerate recovery; a hold can advance only while sensing health remains qualified. After a running health failure, freeze adaptation/recovery and retain already applied retard until a specified healthy requalification or stopped reset. Disabled and monitor-only operation report protection inactive. Expose limit reached and repeated knock at the ceiling.

At `oem_prepare`/`board_ignition_segment`, retain the unclipped tune-derived base in 0.75-degree units, subtract shared retard once, then apply the existing final timing bounds and fill all `oem.adv[]` entries identically. Preserve epoch/admission/spark-cut checks. Apply new decisions to newly scheduled events; do not rewrite an active coil's committed fire deadline in the first implementation. Report requested versus scheduled retard, since clamps and scheduling latency can make them differ.

## Phase 5 Ordinary monitor data and user displays

Keep command `0x10` at **98 bytes** and preserve bytes 0..93. Bump `TU744_MONITOR_EXTENSION` at byte 80 from 3 to **4**. Use the existing reserved tail:

| Bytes | Encoding | New field |
| --- | --- | --- |
| 94..95 | u16 big endian | `knock_an15_mv`, last relevant AN15 voltage; `0xFFFF` means unavailable |
| 96 | u8 bit flags | Knock detection and measurement/control status defined below |
| 97 | u8 | Global requested retard in 0.75-degree counts, display `X * 0.75` |

Byte 96 bits: b0 `knock_detected` (recent-event indicator); b1 voltage fresh/valid; b2 global control active; b3 monitor-only; b4 sensing fault; b5 bench measurement active; b6 last eligible sample detected knock; b7 valid normal running window available. Detection is **not** inferred from voltage or retard by the host.

Define `knock_detected` as true for **500 ms after the most recent eligible detection**, retriggered by subsequent detections. This avoids losing a one-window event between monitor polls. It is not read-to-clear, so multiple clients get the same state. Counters preserve events even if polling pauses. Detail data also exposes the instantaneous last-sample decision and last-event age. On disabling/resync, invalidate the appropriate decision context; retained counts must not make the current flag true. A null/test pulse never sets either detection bit.

Voltage display is an estimate with a documented reference: compute millivolts from raw code using the project's declared ADC convention and a nominal 5000 mV reference, with 32-bit arithmetic. Use one conversion definition in firmware and tests; initially `(raw * 5000 + 512) / 1024`. It is not a measurement of the actual ADC reference rail. Before running windows, show a fresh idle scan voltage with b7 clear. While running, show the most recent normal window; null/test windows do not replace it. Bench normal windows use the same voltage field with b5 set. If unavailable show N/A; if stale show the retained value as stale, not zero or healthy quiet operation.

Command `0x34` returns a versioned coherent detail snapshot including raw ADC, voltage source/type, sample age/time/sequence, offset, amplitude, reference, gain, ratio, threshold, normal/test/null counters, last decision, last-knock age/count, global requested/scheduled retard, hold, health reason, missed-window and ADC-timeout counters, rotation epoch and calibration generation. Keep it within the current 128-byte payload limit and freeze a documented byte layout before implementation. A separate job snapshot supplies the null/test/gain experiment results.

Update `tools/tune_client.py`, `tools/tunerpro_logging.py`, generated ADX, `tunerpro/protocol.hpp`, plugin tests and metadata, and monitoring documentation together. TunerPro adds **AN15 voltage**, **Knock detected**, **Global knock retard**, **Knock sensing valid**, and **Knock fault** displays/logging. The Python decoder accepts the explicitly supported extension-3 and extension-4 layouts; old firmware reports new fields unavailable. Updated plugin/ADX must validate extension 4 before publishing knock fields; do not silently decode an old frame's reserved zeros as a working detector.

The Wizard uses compact command `0x13`, not the legacy monitor. Extend it from v2/40 bytes to **v3/44 bytes**, appending the same four bytes at offsets 40..43. Update `wizard/src/protocol_codec.*`, `protocol.*`, `MonitorData`, dashboard, CSV/log channel definitions, and tests. Explicitly support `(v2,40)` with knock unavailable and `(v3,44)` with knock fields; reject unknown pairs. Keep separate measurement freshness and communication freshness. The added four bytes add about 2.1 ms of wire time at 19200 baud. Label existing advance as base/planned advance; distinguish shared requested retard from actual scheduled timing rather than displaying the uncorrected plan as corrected ignition.

## Phase 6 Native and compiled arithmetic tests

Add `tests/test_knock.c`, `tests/test_knock_oem.py`, target parity fixtures, and protocol/logging tests. Follow `tests/oem_harness.py` for unchanged-ROM comparisons. Compare copied measurement/detector routines over boundary and randomized inputs; test the intentionally global governor against its specification instead of expecting a per-cylinder OEM governor to match.

Cover gain transitions/reference rescaling, threshold equality, all ADC codes at selected states, ratio overflow, saturating subtraction, zero reference, offset clamps/drift sign, null/test exclusion, enable hysteresis, hold reload and expiry, mode changes, stale samples, sync/config generation changes and timestamp wrap. Model quiet-after-knock reference adaptation, so a constant injected ADC value is not assumed to remain above a moving threshold.

Protocol tests feed real C packet generation with distinct neighboring values, verify exact lengths/versions/endian/checksums, voltage conversion, flags, latch expiry, count retention and unavailable/stale behavior. ADX tests validate offsets, formulas, flag references and generated artifact freshness. Wizard tests check old/new compact versions and CSV/dashboard availability. Snapshot tests must catch mixing voltage from one sample with ratio/threshold from another.

## Phase 7 Keil simulated AN15 detection tests

Create `tests/keil_knock_detection.py` and an analyzer, with versioned scenario inputs and machine-readable expected results. Reuse the established `engines/TU5JP/ve/keil_sweep/oemsim.py` engine/EEPROM/timeline machinery for the OEM reference, and a scratch TU744 project with its fresh HEX/listing for the standalone. Refactor helpers to accept repository, firmware and output paths; importing the old helper currently creates files in its own runs directory. Do not transplant OEM PC addresses or scan-buffer fixes into TU744.

Every UV4 process runs headless with a 180-second wall timeout, at most three processes concurrently. Reuse a single stopper SIGNAL and event-table SIGNAL. Fail on unexpected traps, resets, incomplete scenarios or missing logs; a normal process exit alone is not a pass. Time/count checkpointing must use relative delays or handle the 32-bit debugger cycle wrap. Record applied workarounds in the result manifest.

### Input injection and instrumentation

Drive the 60-2 inputs and keep other engine inputs valid. Set `AIN15` to scripted voltages **before the relevant conversion**, with sufficient settle time for the programmed converter. An IC-output fixture follows sample type: idle/null start near 0x25, null end with known drift, ordinary end with chosen integral, and test end with known shift. Keep the analog output held through the conversion. Do not simulate knock by writing a detected flag, ratio or retard into firmware RAM.

Use two explicitly named test lanes:

1. **Native Keil ADC lane:** drive AIN15 and let the simulator finish conversions. Verify requested channel, result and scan interaction. If the installed Keil version reproduces its known injection bug, record this lane as simulator-blocked rather than a passing hardware test.
2. **Keil ADC completion-model lane:** adapt the documented workaround to the *standalone's freshly linked injected-ADC polling locations*. Obtain requested channel from ADDAT2, calculate the result from AIN15, populate ADDAT2 and clear ADCRQ. Log every model-served request. Preserve normal ADC/request code; this validates the compiled downstream path but bypasses converter completion/timing. Separately test timeout by deliberately withholding completion.

For the unchanged OEM use its existing CP-restore, ADC injection, scan-buffer and boot workarounds. They are simulator repairs, not firmware patches. The OEM and standalone have different RAM layouts and acquisition paths. Do not compare global controller output to an OEM per-cylinder trace as if equality were expected.

Observe gate open/close, raw injected sample, gain code, sample type, offset/reference/ratio/threshold, decision, requested/scheduled retard, epochs and fault counters. Also record actual P2 coil edges and dwell. Capture the real command-10/13 monitor response via the UART model where supported; otherwise record the exact serialized reply buffer before transmission and use separate UART/framing tests. A RAM-only decision trace is insufficient evidence for the monitor feature.

### Fixed arithmetic fixtures

For exact boundary cases only, seed reference=32, offset=37, threshold=40, normal sample type and all enable gates true. Suppress adaptation only in the harness by re-seeding state before each isolated case; no production freeze switch. Drive the analog pin using `AIN15 = (raw + 0.5) * 5 / 1024` in the completion-model lane, which targets a code bin rather than a conversion boundary.

| Raw 10-bit ADC | Raw eight-bit value | Corrected amplitude | Ratio in sixteenths | Expected detection |
| --- | --- | --- | --- | --- |
| 148 | 37 | 0 | 0 | No |
| 276 | 69 | 32 | 16 | No |
| 464 | 116 | 79 | 39 | No |
| 468 | 117 | 80 | 40 | Yes, equality |
| 476 | 119 | 82 | 41 | Yes |

For the overload boundary use threshold=80 and reference=64, keeping the ratio below threshold: raw 904 gives eight-bit 226, `raw8-null=189`, so the overload branch does not assert; raw 908 gives 227, difference 190, so it asserts. These are detector fixtures, not an engine calibration. Check all stated results against the ROM arithmetic before accepting the port.

### Required running scenarios

| Scenario | Assertions |
| --- | --- |
| Idle and bench | Idle voltage near simulated baseline, detector inactive; normal bench stimulus changes voltage/decision; test/null do not set detected. Bench start rejected while crank is turning. |
| Quiet steady engine | Normal windows and samples continue, valid voltage is logged, reference/gain settle, no global retard. |
| Single eligible knock | Correct raw/ratio/decision and event count; recent flag persists across polls for 500 ms; requested retard=3 degrees; both coils receive the shared change on newly scheduled events. |
| Repeated eligible events | 3, 6, 9, 12 degree progression and clamp; hold restarts every time; high threshold crossings still counted at the ceiling. Generate amplitudes relative to the evolving reference or use deterministic isolated controller fixtures. |
| Quiet recovery | No early decay, one 0.75 degree step at each specified hold expiry; monitor latch and retard expire independently. |
| Monitor-only | Identical detection/logging path but no spark edge change and applied retard zero. |
| Null/internal pulse | Baseline/drift and self-test pass/fail limits respected; no knock count or retard caused by test amplitude. |
| Gain/offset changes | The same modeled sensor energy with changed gain produces the expected code and adaptation behavior; drift compensation does not become a false event. State explicitly whether the fixture models gain or merely supplies fixed voltages. |
| Invalid path | Withheld ADC completion, late sample, missed window, stuck rail qualification, sync loss and calibration change produce visible health/status changes and no stale attack or quiet recovery. |
| Timing and load | Run at 600, 1000, 2000, 4000, 6400 RPM and configured standalone maximum, plus ramps, timer wrap, storage/serial traffic, boost and ADC scanning. Preserve coil admission and existing deadline guarantees. |
| Telemetry | Command-10 extension 4 and compact v3 contain the sampled voltage and firmware detection state, with correct endianness/scaling/status. Old decoders fail safely or mark knock unavailable. |

Specify expected event and scheduling latency for each scenario. A detection from a combustion window cannot retroactively retard its already-fired spark. Fail if it misses the next defined newly schedulable event, not by assuming zero-latency correction. Establish maximum allowable window jitter against measured crank reference and IC timing; do not declare arbitrary +/-1-tick acceptance without measuring interrupt preemption.

Deliverables: source/image/tune manifests, stimulus tables, raw logs, per-window CSV, monitor packet examples, coil/gate timing plots and a JSON pass/fail summary. No simulator workarounds may conceal an unexpected standalone reset or deadline failure.

## Phase 8 Bench and engine acceptance and delivery

On the ECU bench, verify supply/reference, IC identity/straps/clock, pin connectivity and boot. Scope MF and KI while running null/internal/gain tests. Compare voltage at KI/AN15 with logged voltage, including ADC completion and scan interaction that Keil's completion model cannot prove. Use a controlled sensor stimulus or taps to verify normal-window response and outside-window rejection. Apply simulated eligible events with the crank simulator and confirm actual spark retard on both coils and recovery timing.

On the engine, monitor-only first. Compare logged detections against an independent knock listening or cylinder-pressure reference across RPM/load. A tap or synthetic AN15 event does not validate combustion discrimination. Validate the shared noise model and threshold curve before control is enabled. Establish false-positive behavior, sensor/IC fault response and the absolute window position under controlled testing. Do not deliberately induce knock on an uninstrumented engine.

Complete existing native, target, ADC/sensor, ignition, injection, lifecycle, storage, protocol, TunerPro and Wizard regressions appropriate to changed ownership and schemas. Build normal and stock profiles, inspect memory/stack/vector budgets, and test migration/save/reload/power interruption. Produce matching firmware, tune, XDF/ADX, plugin and Wizard artifacts with hashes and instructions. Preserve existing DTC scope: expose knock health in telemetry initially; do not silently enable the previously excluded OEM knock DTC events.

The software milestone is reached when current-source compiled detection, global control and monitor serialization pass the specified native/Keil scenarios. Production acceptance also requires the bench and instrumented-engine results above. Report these statuses separately; simulated AIN15 alone cannot establish a production-ready physical knock system.
