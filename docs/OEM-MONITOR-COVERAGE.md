# TU5JP OEM monitor coverage

This is the acceptance boundary for reproducing the diagnostic monitors from
`bins/M744_C167_FULL.bin` in the standalone firmware.  A report-word lookup is
not a monitor implementation, and a ported producer is not complete runtime
parity.

## Evidence boundary

The reference image has SHA-256
`5710015f7c5c066c860a1757fd893f305701608c25af8ff23bfcb4fd1e4837b3`.
ROM evidence establishes:

- 107 internal event positions (`0x00..0x6A`);
- 73 positions with at least one nonzero report word;
- 94 distinct nonzero report words, including the decoded U-codes;
- 128 calls to the event manager at ROM offset `0x6A996`;
- 112 calls with an instruction-local event number and 16 data-flow calls;
- bounded control-flow attribution for every reportable event except `0x31`.

Event `0x31` reports P0232/P0231/P0230.  Separate ROM evidence identifies
event `0x31` as the P8.1 fuel-pump relay diagnostic id, but the exact
table-driven path from the output-monitor family into the generic event manager
has not yet been reduced to a single callsite.  It remains explicit rather than
being counted as resolved.

The generated source of truth is [dtc-parity.json](dtc-parity.json).  The full
event/subtype/code/configuration/callsite join is
`engines/TU5JP/defs/dtc_events.csv`.  Both are regenerated from the exact ROM;
neither is a hand-written list.

The supplied Citroen training document 1.3.277 lists the vehicle-level
diagnostic functions and distinguishes M7.4.4 from ME7.4.4 and equipment/
emissions variants (technical pp.45-46, PDF pp.51-52).  It separately specifies
associated variables and automatic erasure after warm-up cycles (technical
pp.47, PDF p.53).  Those are supplied functional expectations.  They do not
prove event numbers, thresholds, calibration gates, RAM addresses, task cadence
or that a particular optional item is fitted to this TU5JP vehicle.

## What counts as reproduced

An event may be marked `parity_complete=true` only when all of these are true:

1. The event/subtype/report-word mapping is extracted from the reference ROM.
2. Every assertion, recovery, completion and subtype path is attributed to its
   producer control flow, including variant gates.
3. The producer and all native conditioning it consumes are ported without
   guessed engineering-unit thresholds or fabricated passing inputs.
4. Its native task release, ordering, initialization, reset and phase callbacks
   are integrated.
5. It uses the ported OEM record, confirmation, aging, clear, readiness and MIL
   lifecycle with the required shared-state aliases.
6. Retained trajectories compare the C implementation with the unchanged ROM,
   covering fail, pass, debounce boundaries, subtype changes, counter wrap,
   unavailable inputs and reset/clear behavior.
7. The linked C167 target is exercised, and the running application exposes the
   event through its diagnostic protocol.

Physical electrical validation is recorded separately.  Passing a ROM
differential suite proves software behavior for supplied inputs; it does not
prove the ECU board, harness or sensor circuit supplies the same inputs.

## Standalone applicability

Six input-event families start from physical observables which the firmware
acquires directly:

| Event | Public reports | Standalone evidence |
|---:|---|---|
| `0x1B` | P0123/P0122/P0120/P0121 | throttle potentiometer, AN8 |
| `0x43` | P0108/P0107/P0109/P0106 | manifold pressure, AN0 |
| `0x5B` | P0113/P0112/P0110/P0111 | intake-air temperature, AN11 |
| `0x61` | P0118/P0117/P0115/P0116 | coolant temperature, AN10 |
| `0x65` | P0563/P0562/P0560/P0561 | supply voltage, AN5 |
| `0x68` | P0503/P0502/P0500/P0501 | timestamped vehicle-speed capture |

These producers now run and include calibrated signal-slew range/performance
selection. They do not fabricate a passing input when an observation is
unavailable. The complete generated policy uses these states:

| State | Count | Meaning |
|---|---:|---|
| `implemented` | 15 | running producer in both oxygen modes |
| `implemented_bench_gate` | 8 | cylinder misfire producer exists; phase polarity needs board acceptance |
| `implemented_adapted` | 1 | running overall P017x STFT-limit monitor, without OEM LTFT |
| `implemented_narrowband` | 4 | running only with an actual narrowband sensor |
| `hardware_pending` | 14 | P6 driver-feedback transport is not safely ported |
| `manifest_pending` | 1 | P0605 awaits a post-link whole-image checksum manifest |
| `unresolved` | 2 | report mapping is known; vehicle function/observable is not yet proved |
| `unavailable` | 28 | required equipment/vehicle contract is absent, the strategy is intentionally absent, or the event is excluded by scope |

Thus 28 event IDs have running producers. Fourteen injector/coil/fan/fuel-pump/
MIL/heater-power events and P0605 remain pending for the concrete reasons above.

The output-event correction is supported directly by this ROM and pin audit.
OEM routine `0x6489C` clocks eight diagnostic bits with P6.5 and samples P6.7;
P6.6 participates in the companion control sequence. The OEM makes P6.5/P6.6
outputs and P6.7 an input. Current standalone `board_init` sets only P6.2 and
P6.4 as outputs (`P6 |= 0x14; DP6 |= 0x14`), so it has omitted the diagnostic
interface rather than proved that load feedback is unavailable. This interface
must be reverse engineered, safely initialized and differentially tested before
those fourteen events are activated. Downstream-heater output events remain
unavailable because the corresponding OEM load is not fitted.

The 60-2 capture now feeds learned 180-degree crank-window misfire monitoring.
Individual-cylinder P0301..P0304 reporting uses DEPHIA phase attribution: the
standalone ignition path arms CC8 and its ISR captures the timing. The Citroen training
document describes the independent vehicle-level function: coil-derived PHASE
logic 1 identifies cylinder 1 compression and logic 0 identifies cylinder 4
(technical pp.24-25, PDF pp.30-31). Paired injection may remain intentional;
it does not prevent retaining 720-degree cylinder identity for diagnostics.

Knock-subsystem events `0x32..0x38` (P1303) and `0x39`
(P0328/P0327/P0329/P0326) are explicitly unavailable by owner scope. ROM
`0x4B374` onward derives the seven P1303 events from per-channel knock status
and counters. P1327 events `0x1A/0x4B` remain in scope because their ROM
producer belongs to the DEPHIA phase path, not the knock subsystem.

Event `0x27` is the adaptation candidate for P0170/P0171/P0172 in both oxygen
modes. A sustained, qualified STFT-limit/error monitor can legitimately report
the overall rich/lean regulation failure after validation. Events `0x25`,
`0x26`, `0x4E` and `0x4F` are not substitutes: ROM evidence maps them to the
four OEM LTFT cells (high/low-load multiplicative and speed-scaled/fixed
additive). Because the standalone intentionally has no LTFT, those four events
are unavailable rather than being driven from duplicated STFT state.

Four upstream oxygen events are narrowband-only candidates: `0x2D` P0130,
`0x3F/0x40` P0133 and `0x45` P0132/P0131/P0134/P0130. A stable linear wideband
lambda output is not narrowband switching or “no activity,” and the sole analog
wire has no controller-ready or fault contract, so those meanings cannot be
reused in wideband mode.

Event `0x2E` P0135 is a port candidate in both modes. In narrowband mode it
monitors the upstream sensor-heater circuit. Owner-supplied hardware evidence on
22 September 2026 establishes that in wideband mode both OEM upstream-heater
conductors—ECU 12 V and heater ground—power the Spartan controller. P0135 can
therefore retain the honest external-circuit meaning: an electrical fault in the
ECU output, wiring, ground path or controller supply/load, once OEM output
feedback is ported and validated. It must not claim detection of the Spartan's
internal sensor heater or heater-control electronics.

The 28 unavailable events have concrete present-installation/design/scope blockers:

- purge-canister `0x60` and secondary-air `0x53`: equipment absent;
- A/C pressure `0x1D`: no connected pressure input;
- six U-code events: no OEM BSI/gearbox/ESP communication state, and the target
  startup disables both C167 CAN modules;
- five downstream-heater/signal/catalyst events: no downstream OEM sensor is
  bound;
- four OEM LTFT-cell events: standalone intentionally has no LTFT;
- eight knock-subsystem events `0x32..0x39`: explicitly excluded by owner scope;
- two OEM identity/immobilizer events: that OEM contract is intentionally not
  reproduced by the standalone.

The two unresolved manufacturer-specific events are not exclusions: event
`0x30` is a second-lamp output circuit whose vehicle-level lamp function is not
identified, and event `0x62` P1608 compares command/feedback state whose
vehicle-level owner is not yet proved. The idle-actuator mapping is no longer in
that bucket: events `0x4D` and `0x56` belong to the idle-stepper/driver
supervisor and are port candidates because the standalone already captures
L9935 response, open/short and thermal state.

The policy source is `tools/dtc_policy.py`. Generation fails if any reportable
event lacks an applicability decision. Only `unavailable` events have no allowed
subtypes; candidates remain visible without pretending they are implemented.

The training document's diagnostic list (technical pp.45-46, PDF pp.51-52)
supports the vehicle-level distinction between sensor, actuator and optional
equipment diagnostics.  It does not prove that command-only standalone outputs
have the OEM's electrical feedback.

## Current status

The OEM-derived record, aging, persistence and steady-MIL lifecycle now runs
standalone producers every 10 ms. Implemented producer groups are sensor inputs,
crank/VSS, DEPHIA phase, crank-window misfire, overall trim regulation,
narrowband activity/response, IAC/L9935 and the P0606 processor subset. Both a
whole-event bitmap and subtype masks are calibration-controlled and disabling a
live selection performs lifecycle recovery.

This does not make `exact_oem_dtc_parity` true. Cylinder misfire reporting needs
a board waveform test to set phase polarity. Output circuit events need the
P6.5/P6.6/P6.7 diagnostic transport. P0605 needs a post-link image manifest.
The absent-equipment, network, immobilizer, LTFT-cell and owner-excluded knock
events remain unavailable. Exact triggers and current boundaries are maintained
in `OEM-DTC-EVENT-MATRIX.md`.

## Porting order

Recommended implementation order:

1. Port and differential-test TPS event `0x1B` with the native raw ADC input,
   conditioning, prerequisites, debounce and all four subtype paths.
2. Port and differential-test MAP event `0x43` on the same basis.
3. Complete real native input bindings for the four existing sensor/VSS ports,
   then validate their scheduling, persistence, protocol exposure and MIL/
   readiness interaction in the running application.
4. Recover and port the P6.5/P6.6/P6.7 output-driver diagnostic protocol, then
   enable only the output events whose loads are actually fitted.
5. Port aggregate misfire/crank diagnostics; enable cylinder-specific codes only
   after phase attribution is proved.
6. Add mode-specific trim-failure monitoring, port the IAC diagnostic groups,
   and investigate the two unresolved manufacturer-specific producers.
7. Add per-subtype enable controls only after each producer passes its evidence
   gate. Disabling a subtype must reconcile live/stored state; it must not stop
   the producer's internal conditioning.

Unavailable equipment is not offered as a switch. A future hardware change can
change that decision only after its observable and producer contract are
recorded and tested.
