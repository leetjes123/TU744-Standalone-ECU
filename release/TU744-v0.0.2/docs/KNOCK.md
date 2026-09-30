# TU744 0.0.2 global knock control

This follows the restored [audit plan](audits/tu744-knock-2026-09-30/IMPLEMENTATION-PLAN.md),
with the user-approved simplified calibration interface. It supersedes the old
per-cylinder learning/persistence proposal. One shared noise reference and gain
feed a global retard controller. No knock DTCs or persistent adaptation are added.

## Engine tuning

All knock settings require a stopped engine. The supplied tune defaults to disabled.

| Setting | Schema-5 offset | Initial value |
|---|---|---|
| Mode | A00 | 0 disabled; 1 monitor; 2 global control |
| Filter band | A01 | 16 (BF2 high, OEM); 0 selects BF2 low |
| Minimum RPM | A02, BE u16 | 600, fixed 120 RPM hysteresis |
| Minimum coolant | A04 | OEM code 104, fixed 5-code hysteresis |
| Gain index | A06 | 4: x32; indexes 0..6 select x2/x4/x8/x16/x32/x64/x128 |
| Gain mode | A10 | 0 automatic (OEM); 1 manual |
| Retard per event | A11 | 4 counts = 3 degrees |
| Maximum retard | A12 | 16 counts = 12 degrees |
| Recovery speed | A13, BE u16 | 100%; allowed 25..400% |
| Shared RPM axis | A20, 16 BE words | 400..6400 RPM |
| Window start | A40, 16 bytes | OEM curve; 0.75 degrees/count after reference |
| Window length | A50, 16 bytes | OEM curve; 0.75 degrees/count |
| Detection threshold | A60, 16 bytes | Minimum of four OEM curves; ratio/16 |
| Minimum filling | A70, 16 bytes | OEM curve; 0.75%/count |

BF2 on P8.5 is the only software-controlled filter pin. Two bands are selectable;
their physical frequencies depend on BF0/BF1/BF3 straps and IC clock. The stored
OEM selector 16 does not establish 16 kHz on the fitted board. Wizard displays
two bands. Coolant remains in the native OEM code because its Celsius offset
is unresolved. Gain selection seeds autoranging in automatic mode, or holds
the selected factor in manual mode; background tracking continues in both.

The measurement reference is 18 captured teeth after the segment boundary,
nominally 108 crank degrees. Start is measured after that reference, not labeled
ATDC. Relative to the tune trigger angle, nominal start is
`108 + start_count * 0.75 - trigger10 / 10` degrees after TDC. Confirm the
trigger relationship physically. CC2 uses T0; CC16 uses T7 at fCPU/16. Delays
retain OEM fCPU/64 quantization. Late compares and acquisition are bounded.

Filling is the standalone estimate `VE * MAP/100 * 273.15/IAT_kelvin`, in percent,
converted to 0.75% units. Valid MAP and IAT are required even with alpha-N.
Cuts, invalid synchronization and service state suppress eligible detection.

Internal defaults remain in the image for validation/provenance but are omitted
from tuning pages: divisor 16, noise seed 51, 500 ms indicator, 150 ms freshness,
offset/drift limits, health debounce and gain ladder. A80/A90 retain original
attack/maximum curves; the controller uses scalars A11/A12. AA0 supplies the
OEM recovery schedule. At 100%, every 0.75-degree recovery step needs four times
its interpolated hold count in valid quiet half-turn samples; 200% halves the
hold. Null, test, stale and ineligible samples do not advance recovery.

Null and internal-test samples never become knock events. Persistent rails and
debounced null/drift/pulse failures invalidate sensing. Running failures retain
retard and freeze recovery until a stopped reset. Ignition subtracts global
retard before final clipping, identically for all slots, without changing an
already committed coil deadline. Shared adaptation and recovery are intentional
standalone deviations, not exact OEM controller parity.

## Bench jobs

`tools/tune_client.py PORT knock-bench-start` starts normal engine-off sampling.
`knock-selftest` runs three null/pulse pairs and seven gain measurements.
`knock-job` reads results; `knock-stop` cancels. The bench window is 4096 T7 ticks
(3.2768 ms at 20 MHz), from the OEM stopped seed of 128 fCPU/512 ticks. Pulse
windows use x16. Gain measurements require a controlled external sensor stimulus
for interpretation; their raw results are reported without an invented pass limit.

Jobs enter the existing service inhibit lifecycle. The first captured crank edge
cancels the job. Completion/cancellation lowers MF/KTI, restores normal gain and
sensor selection, and retains the service inhibit. Reset/key cycle before engine
operation. A turning crank or competing calibration/storage activity rejects or
cancels jobs.

## Monitor and protocol

AN15 is held integral voltage, not audio. Millivolts use nominal 5 V reference:
`(raw * 5000 + 512) / 1024`. Continuous scan and synchronous samples have separate
owners. Null/test samples do not overwrite normal voltage/time/decision. Stale
voltage remains visible as stale; FFFF means unavailable.

Legacy command 10 remains 98 bytes, extension 4 at byte 80. Bytes 94..95 are
millivolts, 96 flags and 97 requested retard in 0.75-degree counts. Compact 13
is version 3 / 44 bytes; append the same fields at 40..43. Wizard accepts old
version 2 / 40-byte frames with knock unavailable. Flags: b0 recent detection
(500 ms), b1 fresh voltage, b2 active control, b3 monitor, b4 fault, b5 bench,
b6 last normal decision, b7 normal running window available.

Command 34 returns a 64-byte, big-endian detail packet:

| Offset | Meaning |
|---|---|
| 0 | Version 1 |
| 1..4 | Ordinary four-byte knock monitor |
| 5, 7 | u16 raw ADC, sequence |
| 9, 13, 17, 21 | u32 sample time, age, count, last-event age (FFFFFFFF if none) |
| 25..36 | u8 source, type, offset, amplitude, current adapted reference, gain index/code, last ratio/threshold/decision, scheduled retard, fault |
| 37, 39, 41 | u16 hold, epoch, calibration generation |
| 43, 45, 47, 49 | u16 ADC timeouts, missed windows, stale samples, ceiling events |
| 51, 55, 59 | u32 normal/null/test counts |
| 63 | u8 last null-start ADC8 |

Source: 0 unavailable, 1 idle scan, 2 running, 3 bench normal. Fault: 0 none,
1 ADC, 2 stale, 3 window, 4 reference, 5 null, 6 pulse, 7 rail, 8 drift.
Command 35 actions: 0 stop, 1 normal bench, 2 self-test, 3 status. Start/stop use
standard status. Query returns 86 bytes: the detail packet, version 1 at 64,
state at 65 (0 idle, 1 running, 2 complete, 3 failed, 4 canceled), completed
windows at 66, retained fault at 67, u16 null/pulse raw at 68/70, and seven u16
gain raw values at 72..85. Requested and scheduled retard are distinct.

## Evidence and reproduction

Set `TU744_OEM_REPO` or pass `--oem-repo` to OEM tests. The helper verifies
`bins/M744_C167_FULL.bin` SHA-256
`5710015f7c5c066c860a1757fd893f305701608c25af8ff23bfcb4fd1e4837b3`.
`tools/generate_knock_defaults.py --check` verifies defaults and the threshold
derivation. Original evidence lives in that workspace under
`engines/TU5JP/archive/36-knock-ic-cc195-init-and-calibration.md` and
`docs/audits/tu744-knock-2026-09-30/`.

Native tests cover arithmetic, governor, packets and migration. The unchanged
ROM comparison covers the standard detector. Linked C166 tests exercise its
integer widths, pointers and monitor serialization. Running-window tests execute
production capture/compare/ADC/coil paths at 600..12000 RPM with a supplied
eligible foreground plan and modeled analog input.

The Keil harness links a simulator-only entry point against production objects;
every UV4 process starts hidden and has a 180-second limit. Native injected ADC
completion is simulator-blocked in the installed version. The labeled completion
model converts AIN15 to ADDAT2 at the freshly linked polling site. It verifies
compiled downstream behavior, not physical ADC timing or knock discrimination.
Physical filter frequency, ADC/reference, gate/coil timing under worst-case
traffic and instrumented-engine validation remain acceptance work. See
[delivery notes](RELEASE-0.0.2.md).
