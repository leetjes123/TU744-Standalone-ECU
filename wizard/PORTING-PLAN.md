# Tuning Wizard port: plan and implementation record

Updated 28 September 2026. Product name: **Tuning Wizard**, executable
`TuningWizard.exe`. The original copied application is unchanged at its source.

## Requirements fixed before implementation

- No original product/company branding in the interface, help, installer,
  errors, icon or user data directory.
- No ECU identity/security indicator or fabricated legacy telemetry.
  Internal capability checks still gate connection and writes.
- Keep the fast live frame at **40 bytes** and include sync loss.
- Preserve schema-4 firmware validation and the standalone RAM/flash model.

## Live-data priority and exact layout

Command 13 now returns **version 2**. Bytes 1..37 retain their previous meanings;
bytes 38..39 replace the live-edit count with the sync-loss counter. The firmware
counter is u32; its transmitted u16 representation **saturates at 65535**. The
UI shows 65535+. Command 32 still reports the live-edit count in its reply.
The monitor rejects unsupported versions and lengths. A compatible calibration
protocol with an older live frame permits backup/update only; version 1 must never be decoded
as version 2. The 98-byte command-10 monitor remains unchanged.

| Bytes | Retained priority data |
|---|---|
| 0 | Version 2 |
| 1..6 | RPM, MAP kPa, TPS tenths-percent (u16 each) |
| 7..9 | CLT + 40 C, IAT + 40 C, battery tenths-volt |
| 10..12 | Wideband AFR x10 (0 invalid), oxygen mV/5, target AFR x10 |
| 13..20 | Applied trim /1024, selected VE %, planned pulse us, signed advance tenths-degree |
| 21..26 | Warm-up, after-start, acceleration %, IAC position, idle target /10, speed |
| 27..30 | Fuel-plan RPM/load indices and fractions /256 |
| 31 | Sync, cranking, running, overrun, closed loop, fan, pump, launch |
| 32 | Rev limit, fuel cut, spark cut, anti-lag, unsaved tune, save busy, MIL, transaction |
| 33 | Narrowband band (low nibble), gear (high nibble) |
| 34..37 | Output inhibits and calibration generation (u16 each) |
| 38..39 | Sync losses since reset, u16 saturated at 65535 |

Planned injector duty is derived on the desktop from pulse width and RPM,
consistent with `src/fuel.c` (`pulse_us * RPM / 600000`), without extra frame bytes.

This retains the main engine state, fueling, ignition, sensor, output-safety and
cell-tracking data. Dwell, boost duty, raw ADC and detailed faults remain secondary
diagnostics outside this compact frame. No zero-valued substitutes are invented.
Planned outputs are labelled as planned; actual admission/cuts still apply.

## Implemented

1. `tools/wizard_definition.py` generates tables, axes, scalars, flags,
   dropdowns, descriptions and offset-based dependencies from the XDF generator.
   It also generates the all-issues desktop validator directly from every
   firmware validation predicate and imports the structural ranges.
2. Calibration files are exactly 3072 bytes with a checked schema marker.
   File saves remain atomic. Legacy migrations, mirrors and EEPROM writes are removed.
3. Commands 13/20/25/31/32 are supported with strict parsing, big-endian fields
   and engineering units. Reads use 128 bytes; transaction writes use 32 bytes.
   TPS endpoint capture requires fresh, stopped-engine snapshot data.
4. `TuneTransfer` reads the ECU baseline before writing an opened file, uses
   bounded live writes for maps or 21/05/22 for other changes, verifies generation
   and readback, and aborts failed transactions. Failures invalidate host sync.
5. Live tuning sends map changes from the update loop. Flash save uses 24 and
   polls 25 byte 8, with a 15-second timeout and the required key-cycle message.
6. Dashboard, logging and log viewer use only supported data, including sync
   state and saturated sync-loss count. Fuel-map highlighting uses reported cells.
7. Firmware update validates image size/vector, erases both tune slots, probes
   erase completion and checks 07/08 status/page sums, including erased blank pages.
   Tune backup is offered before flashing. Failure leaves the handler available.
8. Branding and identity indications are removed. Help/manuals describe the
   standalone contracts and distinguish supplied Bosch evidence from firmware facts.
9. Autotune has its own tab and sample-coverage visualization. It collects weighted, delayed AFR errors behind steady-state gates,
   offers bounded local VE proposals and uses the ordinary live-write path.
   Persistence is always an explicit separate action.
10. Diagnostics is a separate top-level tab, available without a calibration.
    Read stored/current native DTCs through 27/29 and controller faults through
    2B/2C; show MIL requests, searchable fault cards and first-confirmation
    freeze-frame popups. Clear with 30 after a fresh stopped-engine check,
    then refresh; distinguish acceptance, uncertain replies and persistence.
    Keep previous scan results on failure and label them as previous data.

The new desktop application retains table tools, undo, comparison, log viewing,
CSV recording and themes. Navigation is driven by generated calibration categories.
Opening/writing a tune no longer relies on the copied application's EEPROM model.

## Validation and remaining acceptance

- Definition bounds and overlap checks; dynamic/fixed axes; schema marker and
  atomic file roundtrip; strict frame length/version and signed/large values.
- Desktop tested against the compiled firmware parser, including 340 differential
  validator cases, live writes, transactions, lost commit replies, rejected writes,
  save status, readback, autotune gates and minimum flash erase scope.
- Firmware regression vectors include losses 0, 1, 255, 256, 0x1234, 65535,
  65536 and 0x12345678, and verify unchanged command-10 saturation/layout.
- Stock-profile Keil build and native firmware tests are required before packaging.
- Physical ECU flashing, K-line timing, engine tuning and exhaust-delay acceptance
  remain open. The stock-profile output-enabled image remains experimental.
- Faster baud/streaming is deferred; no unverified transport switch is introduced.
- Existing board question remains: source-provenance labels for P3.5 conflict
  (legacy security label versus OEM-supported CC195 KTI). No pin assignment is
  changed by this port; hardware confirmation remains required.

Supplied reference consulted and SHA-256 verified: Bosch training document
1.3.277, September 2000. PDF p.15/printed p.9 (MAP/IAT), PDF p.17/printed p.11
(60-2 speed reference), PDF p.26/printed p.20 (coolant), PDF p.39/printed p.33
(upstream oxygen polarity and equipment variants). These supply terminology;
frame offsets and standalone behavior come from the firmware source.

Diagnostic functional reference: Bosch document PDF p.52 / printed p.46,
Chapter 3 describes scan-tool fault reading, associated contexts and erasure;
PDF p.36 / printed p.30 describes fault-memory saving during power latch and
distinguishes M7.4.4/ME7.4.4 timing. These are supplied functional references.
The wizard uses the standalone protocol, not standard OBD mode packets.
Record offsets and clear/persistence semantics come from standalone
`src/protocol.c`, `src/oem_history_owner.c`, `src/faults.c` and the protocol
documentation. P-code/subtype labels are generated directly from the TU5JP
ROM-derived `engines/TU5JP/defs/dtc_events.csv`; unsupported subtype combinations
remain explicitly unmapped. No ECU pin assignments change.
