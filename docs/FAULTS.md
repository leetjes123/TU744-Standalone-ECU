# Sensor faults without engine inhibition

The owner requested that TPS, coolant, intake-temperature, battery and
speed-density MAP faults be recorded without inhibiting engine operation.
The subsequent clarification retains the missing/unreadable calibration
start inhibit and requires using the faulty sensor's actual output, without
replacement values. This is a standalone policy, not an OEM fallback port.

The control pass no longer sets `INH_SENSOR` for these five inputs. It preserves
their invalid quality and records a standalone DTC. Valid samples retain the
existing filter. Invalid, out-of-range or stale samples bypass that filter and
use the latest captured raw ADC count through the normal calibration conversion.
There are no assumed temperatures, assumed voltage, estimated manifold pressure,
last-good substitutions or automatic switches to alpha-N. Battery range checking
also examines the raw converted voltage, so the filter cannot hide a bad reading.

When capture is stale there is no new measurement: the last captured raw count
continues to be used and remains marked stale. Before any capture, ADC storage
is zero-initialized and quality is unavailable/out-of-range; that zero is not
claimed as a valid measurement. Existing calibration endpoint clamping, fuel
arithmetic, duty limits, dwell limits and output deadline checks still apply.
Consequently an actual zero pressure input can calculate zero fuel; logging a
DTC does not force a nonzero pulse. The separate quality gates for feedback,
AE, DFCO, launch and boost remain in place. Faulty measurements are not relabelled
as valid to enable those features.

Missing/unreadable/invalid saved calibration still prevents engine start through
`INH_CAL`. A rejected invalid calibration transaction records a fault and leaves
the previous validated active calibration intact. No default tune is activated.
Sync, run permission, service and output/deadline inhibits are unchanged.

## Standalone records and access

These are private LRE identifiers, **not SAE P-codes or native OEM event IDs**:

| Code | Meaning |
|---|---|
| LRE-0001 | Calibration unavailable, or rejected invalid calibration update |
| LRE-0002 | TPS input invalid/stale |
| LRE-0003 | Coolant input invalid/stale |
| LRE-0004 | Intake-temperature input invalid/stale |
| LRE-0005 | Battery input invalid/stale |
| LRE-0006 | MAP input invalid/stale while configured for speed-density |

Each record has active/stored status, the latest fault reason, a saturating
occurrence count and first/latest assertion uptime in milliseconds. A new
assertion or change of reason increments the count; repeatedly observing the
same fault does not. Recovery clears active status but retains the stored record.
After reboot stored records remain, while active status is reevaluated from the
current observations. Uptime fields can refer to different boots and are not
wall-clock timestamps. No automatic historical clear or new MIL policy is added.

Use the reference client:

```powershell
python firmware/tu5jp_standalone/tools/tune_client.py COM3 faults
```

Commands `2B`/`2C` expose these records independently of the unfinished native
diagnostic runtime. The OEM support/parity flags remain unchanged. Normal OBD
scan-tool access is not implemented. Command `30` (clear DTCs) removes records whose
fault is no longer active; an active fault keeps its record.

## Persistence

The new independent journal uses EEPROM bytes `3104..3199` and `7200..7295`,
between each calibration slot and native-history slot. Each 96-byte snapshot
contains its schema, sequence, stored mask, six records, CRC and final commit
byte. The inactive slot is invalidated before writing and verification; commit
is written last. Both load candidates are validated before sequence selection.
Compile-time checks prevent overlap with calibration and native-history storage.

Faults are recorded immediately in RAM. Their EEPROM snapshot is saved during
key-off after rotation and IAC activity have stopped, before the native-history
save. Fault logging does not enter service mode or start an EEPROM write while
the engine is running. All journals share the existing exclusive EEPROM lease.
A fault arriving during a snapshot remains dirty for a subsequent snapshot.
An unreadable journal is not replaced with an empty history, and failed writes
remain visible without an automatic retry loop. Sudden loss of power before
shutdown persistence can lose newly recorded RAM faults.

## Evidence

Supplied functional reference: Citroen technical training 1.3.277, PDF pp.15-16
describes the pressure/intake-temperature inputs and their use in air-charge
calculation; PDF pp.51-52 distinguishes diagnostic functions and equipment
variants. These supplied claims do not establish the policies above, ADC pin
assignments, or OEM fault-code equivalence. No TU5JP4 behavior is transferred.

`test_faults.c` checks engine-output admission with each failed sensor, measured
ADC rail values, stale raw values differing from the last filtered value,
recovery/reassertion, missing calibration, rejected updates, combined shutdown
ownership and all 99 prefix-write interruption points. `test_faults_target.py`
executes the linked C166 control/sensor code with explicit ADC states. The client
test checks the actual C parser and private-record reader. These tests do not
qualify real sensor circuits or prove that an engine runs well with bad inputs.
