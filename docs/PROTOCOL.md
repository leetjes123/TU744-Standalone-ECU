# Calibration and Wizard integration

Request: `AA length payload checksum`. Response: `55 length payload checksum`.
Checksum is the low byte of length plus all payload bytes. Multi-byte values
are big-endian unless a field explicitly says otherwise. Requests contain at
most 36 payload bytes. Parser timeout is 100 ms; RX overflow discards the
incomplete stream. Responses are sent incrementally outside interrupts.

| Command | Request payload after command | Response |
|---|---|---|
| 00 | none | ASCII `TU744 0.0.1` (no terminating NUL) |
| 01 | none | Firmware update, LRE-B4 path: status byte, then the RAM handler owns the line; see [LIVE-MONITOR.md](LIVE-MONITOR.md) |
| 04 | offset:u16, count:u8, max 128 | Active calibration bytes |
| 05 | offset:u16, count:u8, data, max 32 | 00 accepted / 01 rejected |
| 10 | none | 98-byte legacy monitor envelope; see limitations below |
| 11 | none | Sixteen raw ADC u16 values |
| 13 | none | 40-byte compact live frame, version 2 (sync losses at 38..39, saturated u16); see [LIVE-MONITOR.md](LIVE-MONITOR.md) |
| 20 | none | protocol=3, schema=4, size:u16, generation:u16, max-write, max-read, exact-DTC-parity, board-released |
| 21 | none | Begin staging; status byte |
| 22 | none | Validate and atomically activate; status byte |
| 23 | none | Abort staging; status byte |
| 24 | none | Begin recoverable calibration save; status byte (backend from 2D) |
| 25 | none | 34-byte development status |
| 26 | event:u8 | event, support, live-descriptor:u16 |
| 27 | none | 24-byte lifecycle status, version1 |
| 28 | mode:u8: 0=disarm, 1=arm moving, 2=assert stationary and arm | status byte; no automatic retry |
| 29 | slot:u8, 0..19 | 24-byte native stored record; rejected before history boot |
| 2A | none | 8-byte reset observation, version1; read-only |
| 2B | none | 12-byte standalone DTC summary, version1 |
| 2C | id:u8, 1..6 | 16-byte standalone DTC record, version1 |
| 2D | none | 8-byte storage capabilities, version1 |
| 30 | none | Clear stored DTCs (engine stopped, key on); 00 accepted / 01 rejected |
| 31 | none | 12-byte TPS calibration snapshot, version 1 (all TU744 firmware) |
| 32 | offset:u16, count:u8 (1..16), data | Live map-cell write: status, generation:u16, live edits:u16 |

Command `31` is read-only: version at byte 0, flags at 1, raw TPS ADC:u16 at 2,
sample age in ms:u16 at 4 (saturated at 65535), closed ADC:u16 at 6, open ADC:u16
at 8, tune generation:u16 at 10. Flags: bit0 = a seen ADC sample no older than
100 ms and in 0..1023; bit1 = engine stopped, zero RPM and unsynchronized;
bit2 = qualified run permission; bit3 = valid calibration; bit4 = IAC homing.
This does not depend on the calibrated TPS percentage or expose sensor-quality
telemetry. The plugin requires bits0..3 set and bit4 clear before capture/apply.
The driver takes the raw sample/timestamp and endpoint pair under one lock.

TPS endpoints remain the big-endian words at calibration offsets `0x7B0` and
`0x7B2`. The plugin applies both in one normal begin/write/commit transaction,
then verifies generation and exact readback. Firmware validation requires
open >= closed + 100 and open <= 1023; these fields are structural and cannot
change while running or homing. Panel capture alone never writes calibration.
The SDK exposes no supported open-BIN editing API, so after panel apply the
plugin blocks emulation writes until a complete ECU BIN download. An ambiguous
write result also requires a download. Persistence remains explicit command24.

Command `2D`: version1 at0; calibration backend at1 (0 EEPROM, 1 NOR flash);
EEPROM capacity:u16 at2; calibration slot count at4; OEM-history snapshot count
at5; reserved zero at6/7. The stock profile returns `01 01 04 00 02 01 00 00`.
See [STOCK-95080.md](STOCK-95080.md) for its layout and erase/save behavior.

Commands 02 and 03 are not implemented: no raw EEPROM write is accepted.
Command 01 enters the firmware-update handler only with the engine stopped. A legacy RAM write outside an
explicit transaction performs begin/write/validate/commit internally. Invalid
input leaves the active generation unchanged. Multiple related writes must be
one explicit transaction. The ECU remains the authority for validation.

Begin (`21`), commit (`22`) and a legacy write outside a transaction (`05`)
run as steps of a few ms each, one per 10 ms control release. Their status
reply therefore arrives after one to three releases (about 10-30 ms) instead of
at once. Run in one pass they took about 26 ms and could let a running engine's
plan exceed its 30 ms age. Frames sent meanwhile wait in the receive buffer.

Staging expires after 5000 ms without a successful begin/write and is aborted
on qualified key-off. Only the volatile edit buffer is discarded; active and
durable calibrations remain unchanged. Read back before retrying an expired edit.
The shared temperature axis at `0x460..0x47F` is structural, just like the sensor
curves: it cannot be changed while running.

Command 25 fields: inhibits at 0, epoch at 2, calibration generation at 4,
validation-error offset at 6, storage result at 8, service latch at 9, IAC state
at 10, IAC fault at 11, IAC position/target at 12/14, requested/bounded fuel us
at 16/18, reserved zeros at 20–25 (retired sensor-quality telemetry), STFT enable at 26, idle
mode at 27, fault counter:u32 at 28, steady/flashing demand bytes at 32/33.
Storage result: 0 pending, 1 saved, 2 timeout/generation conflict, 3 IO failure.
Command 26 support=0 means pending, not a passing monitor. The exact-parity
capability is false. Command `30` clears stored DTCs; see below.

Command27: version at0; power state/block at1/2; analog-wideband state at3;
launch armed/stationary at4/5; estimated gear at6; diagnostic booted/initialized/
fault at7/8/9; native binding mask:u16 at10; ADC freshness:u16 at12; history
ready/dirty/phase/result/clear-pending/clear-durable at14..19; stored row count
at20; steady/flashing demand at21/22; exact lifecycle parity (false) at23.
Command29 returns native record bytes, including native byte orders; it is
not an OBD response. See LIFECYCLE.md for the availability boundaries.
Power states: 0 boot, 1 run, 2 draining, 3 saving, 4 holding power, 5 released,
6 waiting to restart, 7 reset requested. Physical release is blocked on this board.

Command2A: version1 at0; capture-available at1; raw boot WDTCON:u16 at2;
decoded mask:u16 (`0002`, WDTR only) at4; watchdog-indicated at6; reserved zero
at7. Unavailable capture returns zero raw/indication fields, with availability
false. A valid zero observation has availability true. The client returns
`None` for unavailable raw/indication values. Other bits remain raw because
the silicon step and reset wiring are unverified. This is the current boot's
observation, not a reset counter, retained log or OEM diagnostic response.

## Schema 4 and migration

Commands `2B`/`2C` are the private records documented in [FAULTS.md](FAULTS.md),
not OEM/OBD event responses. `2B` contains version/count at0/1, active/stored
masks:u16 at2/4, journal-loaded/dirty/phase/result at6..9 and reserved zero at10/11.
Journal result0=pending/uninitialized,1=ready or saved,3=IO/timeout failure.
`2C` contains version/id/active/stored/reason/reserved at0..5, occurrences:u16
at6, first/latest assertion uptime:u32 at8/12. Integers are big-endian. Sensor
reasons use quality flags2=stale,4=range,8=configuration. Calibration reasons
are1=unavailable and2=rejected invalid update. Historical records survive recovery;
active status is reevaluated each boot. Use the client's `faults` action to read
them. These commands do not add OEM parity or a DTC-clear operation.

The calibration is 3072 bytes. Offset 0x900 contains `4C 52 00 04`. Extension
words occupy 0x904–0x939; their definitions are in `include/ecu.h` and
`include/lifecycle.h`. Calibration
is explicitly encoded rather than serializing a compiler-dependent structure.

Additional optional policy fields (all zero in the example tune):

| Offset | Type | Meaning |
|---|---|---|
| 0x926 | u8 | Wideband policy: 0 disabled, 1 analog qualification; 0x927 reserved zero |
| 0x928 / 0x92A | u16 | Warm-up / continuous good-sample milliseconds |
| 0x92C / 0x92E | u16 | Accepted raw wideband minimum / maximum mV |
| 0x930 | u16 | Launch arm expiry ms; zero disables arming |
| 0x932 / 0x934 | u16 / u8 | Soft-launch start RPM / maximum cut percentage |
| 0x936 | u16 | Anti-lag one-shot maximum ms per arm |
| 0x938 / 0x939 | u8 / u8 | Anti-lag maximum coolant / inlet-air Celsius |

Standalone DTC policy occupies `0x940..0x9EA`. `0x940..0x94D` is the
107-event enable bitmap. `0x94E..0x96B` contains fail/pass, trim, oxygen,
misfire, phase, output, VSS and sensor-slew thresholds; `0x962` is DEPHIA polarity.
`0x980..0x9EA` contains one low-nibble subtype mask per event (bits 0..3 select
descriptor subtypes 1/2/4/8). A zero subtype mask means all four for backward
compatibility; clear the event bit to disable all. Changing either control is a
stopped/structural calibration operation, and a disabled live selection is
recovered through the DTC lifecycle.

Launch soft mode still requires legacy0x7B4 bit3. Anti-lag uses bits6/7 and
0x7B8/0x7B9 timing/VE; see LIFECYCLE.md for precedence and acceptance limits.
Status reports the analog policy state separately from STFT eligibility.

Preserved table layout does not make an old tune directly usable. Required fuel
is per 720-degree cycle, paired injection divides it into two events per cycle,
and physical timer conversion is corrected. IAT correction, interpolation,
afterstart decay, bounded STFT and AE time semantics differ. Cranking VE and after-start/AE multipliers use the schema-4 units documented
in [FUELING.md](FUELING.md). Schema-3 images are rejected; changing only the
schema marker is not a conversion.
The legacy gauge executable interprets 0x5F0 as HIGH percent despite its source
layout comment saying LOW; this implementation follows the executable behavior.

Injection phase is a real-tooth position in tenths of degrees: 0..1620 in
increments of 60. Its partner is 1800 later. Positions corresponding to missing
teeth are rejected. The example uses teeth 27/57; it is not a measured OEM phase
or an approved engine calibration. Sensor curves, thermistor pull-ups, MAP
conversion, TPS endpoints and trigger reference require a reviewed migration.

Sensor/equipment/geometry extensions require stopped operation. Changes to them
reset affected sensor/controller history and require IAC requalification; homing
in progress rejects such a commit. D gain and AE second-derivative behavior
remain unimplemented. Anti-lag now has an explicit
bounded policy; see LIFECYCLE.md. The
development IAC waveform currently fixes the legacy timing settings to 5/6;
the separate 0x924 word sets the idle step controller's cadence, not coil timing.

Schema-4 fuel units and command-10 fuel telemetry fields are specified in
[FUELING.md](FUELING.md). After-start and AE are 100% when neutral; selected VE
and AE also have full-width u16 fields at monitor offsets 76 and 78.

The legacy monitor packet is only a transport envelope. Several old diagnostic
fields remain zero and IAC state semantics changed. The current Wizard must use
the capability/status extension before showing authority, IAC success or DTC
readiness. Full GUI migration and flashing recovery remain pending.

Command `10` retains its 98-byte length. Extension version1 at offset80 enables
full-range signed engineering values (big-endian): CLT:s16 at82, IAT:s16 at84,
advance:s16 in tenths of a degree at86. Offset81 flags clipping of the old signed
temperature bytes (bit0 CLT, bit1 IAT); those bytes now saturate instead of
wrapping above 127 C. Offset88 is the commanded physical MIL level, distinct
from steady/flashing demands in command25.
Extension version2 (firmware from 24 September 2026) adds the inhibit mask:u16 at
89 (same bits as command25 offset0), the native stored-DTC count at91 and the
number of those records currently failing at92, so a logger needs only this
command. ADX5.0 and plugin0.3.0 required extension2.

TU744 firmware **0.0.1** uses extension **3**. Byte93 is a mutually exclusive
narrowband state bitmask: `1` lean, `2` stoich, `4` rich, `0` unavailable.
Lean is below calibration `0x8C3 * 5 mV`; rich is above `0x8C4 * 5 mV`;
the inclusive interval between them is displayed as stoich. This is a voltage
transition band, not a quantitative AFR measurement. The value is unavailable
in wideband mode, with invalid calibration or invalid/stale oxygen data. It
updates independently of STFT eligibility, so it also works with closed loop off.
Bytes94–97 remain reserved. The old controller hysteresis byte22 is unchanged.
Command25 no longer exports sensor-quality values; internal sensor validation
and diagnostic behavior remain in use.

Supplied functional evidence: Bosch training document 1.3.277, section XVIII,
printed p.33 / PDF p.39 describes high upstream-sensor voltage as rich
(0.6–0.9 V) and low voltage as lean (0.1–0.3 V). The configurable thresholds
and middle-band display above are standalone implementation choices, not
behavior proven in the OEM ROM.

Use **TU744.dll 0.5.0** and **TU744_schema4.adx 7.0** together with this firmware.
The plugin and reference client's `monitor` reject earlier extensions so missing
flags cannot look like real measurements. Planned injection duration is
not proof that fuel was delivered; admission/cuts still apply.

## Native DTC records and freeze frames

Command `29 slot` returns one of the stored native records (the row count is
command27 byte20). The reference client's `dtcs` action reads and decodes all of
them. Record byte0 is the event; the little-endian word at2 holds bit0 currently
failing, bits8..11 the latest subtype and bits12..15 the first subtype, which
select the P-code (OEM-DTC-EVENT-MATRIX.md). The little-endian word at4 has bit3
steady and bit4 flashing MIL. Byte22 counts occurrences. The big-endian word at20
is the native on-time clock, one count per 36.1 s of ECU on-time.

Bytes9..19 are the freeze frame captured when the record is created: coolant+40,
intake+40, TPS in 0.4%, MAP kPa, battery in 0.1V, RPM/32, intake+40 (twice, as in
the native layout), speed km/h, engine mode (0 stopped, 1 cranking, 2 running)
and fuel trim in 1/128 (128 = no correction). Firmware before 24 September 2026
stored 255 for the trim. A later occurrence updates only the count and clock.
Command `30` (firmware from 24 September 2026) clears all stored DTCs. It is
accepted only with the key on, the engine stopped and diagnostics running, and
only once until the request has been serviced. The diagnostic runtime performs
the ported clear on its next 10 ms release: records, freeze frames, live
descriptors, MIL demand and readiness, and the standalone monitors restart their
debounce. Private LRE records that are no longer active are removed as well.
History is saved at key-off like any other change. A fault that is still present
is stored again once it re-qualifies. The plugin's **Clear DTCs** button and the
reference client's `clear-dtcs` action use this command.

## Reference client

Command `2E` (request length 1) returns 18 bytes of output health. Byte 0 is
version 1, byte 1 is the published dwell-feedback enable, byte 2 is a two-bit
draining-coil mask, and byte 3 is zero. Big-endian u16 pairs at 4/6 count missing
CC9 feedback and at 8/10 count invalid feedback; signed s16 at 12/14 reports
correction in 0.8 us ticks. The u16 at 16 counts late output stages. Counters
saturate at 65535 and reset at boot; they are not persistent DTCs. The reference
client action is `outputs`. Stopped-only calibration byte `0x93A` accepts 0
(fixed calibrated dwell, default) or 1 (qualified CC9 correction). Missing or
invalid feedback resets correction rather than growing dwell.

Command `2F` (request length 1) returns 14 bytes of timing health since boot:
byte 0 is version 1, byte 1 is zero, then big-endian u16 values at 2, 4, 6, 8
for the longest foreground pass, the longest interval between foreground passes,
the largest tick service gap and the oldest plan age observed while outputs
were permitted (all ms), at 10 the capture-overrun count and at 12 the late
output-stage count. `2F 01` reports and then clears the four maxima. Values
saturate at 65535. Reference client actions: `timing`, `timing-clear`. Use it on
the bench to confirm headroom: the interval and plan-age maxima must stay well
below the tune's plan age (`0x922`) and the 50 ms foreground bound, and the tick
gap must stay at 3 ms or less.

`tools/tune_client.py` exposes `Client(exchange)` for host integration and a
serial CLI using pyserial. It tests transactions against the actual compiled
C parser. Commands must be explicitly invoked; none runs as part of a build.

```powershell
python firmware/tu5jp_standalone/tools/tune_client.py COM5 caps
python firmware/tu5jp_standalone/tools/tune_client.py COM5 reset
python firmware/tu5jp_standalone/tools/tune_client.py COM5 dump saved-schema4.bin
python firmware/tu5jp_standalone/tools/tune_client.py COM5 activate reviewed-schema4.bin
python firmware/tu5jp_standalone/tools/tune_client.py COM5 save
```

`activate` writes RAM only, validates once, checks generation and reads back.
`save` persists the active calibration and leaves service mode latched until
reset. Do not infer success from a lost commit response; query generation and
read back before deciding what to do next. No retry blindly repeats a commit.
