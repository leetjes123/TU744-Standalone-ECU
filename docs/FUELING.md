# Fuel units, calibration schema 4

This is the standalone control policy requested for this project. It is not
an OEM fueling-equivalence claim. All percentages are whole percentages.

| Setting | Offset / encoding | Meaning |
|---|---|---|
| Running VE | 0x000, 16x16 u8 | 0..255% VE |
| Cranking VE | 0x490, 16 big-endian u16 | 0..1000% effective VE, coolant axis at 0x460 |
| Required fuel | 0x5DF, u16 | Microseconds per 720-degree cycle at reference charge |
| Warm-up | 0x480, 16 u8 | Running-fuel multiplier; 100% is neutral |
| After-start | 0x4B0, 16 u8 | Initial running-fuel multiplier, 100..255% |
| After-start duration | 0x4C0, 16 u8 | Hundreds of milliseconds; zero disables |
| Acceleration enrichment | 0x759, 6x8 u8 | Running-fuel multiplier, 100..255% |
| AE RPM modifier | 0x799, 8 u8 | 0..255% of enrichment above 100%; 100% retains full enrichment |
| AE decay | 0x90A, u16 | Milliseconds to return to 100%, 10..5000 |
| Injector dead time | 0x540, 8 u16 | Microseconds added after fuel multipliers |

Cranking VE replaces the running VE table during `ENGINE_CRANKING`. It is an
effective VE setting for tuning starting fuel, not a measured engine efficiency
or a percentage applied on top of the running VE cell. Cranking and running
both calculate fuel from required fuel, IAT density correction, selected VE,
measured MAP in speed-density mode, and the optional stoichiometric/target-AFR
ratio. Alpha-N omits the MAP multiplier. Required fuel is divided into two
paired injection events per 720-degree cycle.

Only running fuel then receives warm-up, after-start, STFT and AE multipliers.
Cranking does not receive those running enrichments. The after-start multiplier
decays linearly from the coolant-indexed initial percentage to 100%; its clock
starts on the qualified transition to running. Zero duration is immediately
neutral. Existing coolant interpolation and engine-state thresholds remain.

AE uses the existing TPS-rate/previous-TPS table axes and qualification delay.
Its initial multiplier is `100 + (table_percent - 100) * rpm_modifier / 100`.
For example, a 200% table cell and 50% RPM modifier produce 150% fueling.
A zero RPM modifier or a 100% table cell produces no enrichment. AE decays
linearly to 100%, with a new larger request restarting the decay. Invalid TPS,
non-running state or unusable control interval clears AE to 100%, including
its retained peak, so an old pulse cannot reappear after input recovery.
STFT eligibility uses the new neutral value of 100% for both enrichments.

All multipliers operate on fuel before dead time. For example, required fuel
5000 us, VE 80%, MAP 100 kPa, IAT 0 C, and unity AFR ratio produce 2000 us per
cranking event, then dead time is added. During running, a 2500 us base with
150% after-start and 150% AE produces 5625 us before dead time. Integer scaling
rounds down at each stage. Fuel demand is capped at 60000 us before dead time;
existing duty-cycle and 25000 us event limits still bound delivered pulses.
Zero calculated fuel gets no dead time. Consequently measured zero MAP also
computes zero cranking fuel in speed-density mode, without a sensor inhibit.

## Migration

The 3072-byte image now requires `4C 52 00 04` at 0x900. The capability response
is protocol 3 / calibration schema 4. Both the ECU validator and reference
client reject schema-3 calibration; old EEPROM slots are not accepted as tunes.
Missing compatible calibration still prevents engine start. A rejected update
keeps the currently active valid tune. No automatic conversion or base-map
replacement is performed.

Schema-3 after-start cells can be converted by adding 100 only if the result
fits the new 100..255 range. Larger old values require a deliberate retune.
Schema-3 cranking pulsewidth and additive AE cannot be converted to equivalent
percentages at every operating point: the new quantities depend on base fuel,
MAP/IAT and other active corrections. Those tables must be retuned in the new
units; changing just the schema marker is insufficient. The supplied LRE-B4
1.6 basemap has now been converted with an explicit cold-soak cranking reference,
table transposition, complemented spark reference and a per-change report under
`docs/audits/tu5jp-output-closure-2026-09-23/calibration/`. Native and linked C167
validation establish format/arithmetic, not an engine tune equivalence claim.

The generated example uses cranking VE 200%, after-start 110%, and neutral
AE cells of 100%. It is a software test fixture, not a validated engine tune.

## Monitoring and checks

The 98-byte command-10 envelope retains its size. Byte 18 is the after-start
multiplier (100 neutral); byte 29 indicates AE above 100%; byte 30 is AE percent
clipped at 255. Byte 5 is selected VE clipped at 255. Full selected VE and AE
percentages are big-endian u16 at bytes 76 and 78. Bit 2 of byte 49 indicates
after-start above 100%. Consumers must check the calibration schema before
interpreting these fields.

Native tests check percentage scaling, simultaneous enrichment, neutral values,
clock wrap, decay, recovery and schema validation. `tests/test_fuel_target.py`
executes the linked C167 fuel/AE routines, including VE above 255%, measured
zero MAP, IAT correction and demand saturation. These are software checks;
physical engine validation remains outstanding.

## Trigger-decoder assessment

Supplied functional evidence: the Bosch training document describes 58 present
teeth at six degrees each on PDF p.17 and twin-static ignition/DEPHIA on PDF
p.30. It does not specify the complete decoder algorithm.

TU5JP ROM-derived evidence is recorded separately in
[OEM-DIAGNOSTICS.md](OEM-DIAGNOSTICS.md#rotation-input-conditioning): recovered
capture, reset and speed routines have differential tests, while the complete
capture ISR/PEC sequence and synchronization-loss integration remain incomplete.
The native ISR switches between capture processing and a completed PEC buffer;
calling the recovered capture routine on every standalone tooth is insufficient.

Engineering judgment: fully reproducing and validating the original TU5JP input
and synchronization behavior is a strong reference path on the original hardware.
An adjustable TDC reference can remain a separate output-angle calibration.
Reliability depends on proving that complete path, including cranking, noise,
stall/restart and timer wrap, not just on copying the gap predicate. This fuel
change leaves the current decoder in place and makes no OEM-equivalence claim.
