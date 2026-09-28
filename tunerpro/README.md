# TU744 / TunerPro RT

Current delivery: **TU744 firmware 0.0.1**, **plugin 0.5.0**, **ADX 7.1**.
Targets stock-M95080, protocol 3 and calibration schema 4. Monitor extension 3
retains the narrowband lean/stoich/rich indicators without sensor-quality data.

## Wideband display

ADX 7.1 adds **Wideband lambda (14.7 reference)**, **Target lambda (14.7
reference)** and **Oxygen input voltage** to the **Engine** data list, alongside
**Wideband measured AFR**. Reload `TU744_schema4.adx` after replacing the file.

Lambda is the transmitted AFR divided by 14.7 (packet bytes 15/16 contain AFR
times ten, so the ADX equations are `X/147`). This matches the supplied basemaps.
The monitor does not transmit the configurable stoichiometric AFR: if your
controller/tune uses another AFR reference, change both lambda equations to
divide by ten times that reference. The three displayed decimals do not add
precision beyond the packet's 0.1 AFR steps.

Measured AFR/lambda require wideband mode and matching controller voltage/AFR
calibration, and should be read after the controller warms up. They are not
valid mixture measurements with a narrowband sensor; the packet does not
provide a wideband-ready flag to hide invalid readings. Oxygen input voltage
uses the unfiltered 10-bit ADC at packet offset 62 (`X*5/1023`); measured AFR
comes from the firmware's filtered input.

These are standalone protocol mappings (`src/protocol.c`, `src/sensors.c`),
not OEM wideband support. Supplied Bosch training section XVIII (PDF p.39 /
printed p.33) describes the original switching oxygen sensor.

## TPS calibration

1. Switch the ignition on with the engine stopped. Wait for idle-valve homing
   to finish. Open the plugin configuration panel, **TU744 controls**.
2. Release the accelerator fully and click **Capture closed**.
3. Fully press the accelerator and click **Capture open**. Release it again.
4. Check the captured ADC counts and click **Apply TPS**. Both endpoints are
   applied together to ECU RAM and read back. The open value must be at least
   100 counts above closed. You can also type known endpoint counts in the boxes.
5. Select **Download BIN from Emulator** and save that BIN on the PC. Further
   uploads/live edits are blocked until a full download, so the old PC values
   cannot overwrite your calibration.
6. Click **Save ECU tune to flash**, wait for verification, then key-cycle/reset.
   Apply TPS and saving a PC BIN alone do not make the ECU change permanent.

**Read TPS** displays the current raw ADC and the ECU's existing endpoints.
Capturing only stages values in the panel; Apply TPS changes the ECU.
The ADX has a **TPS calibration - engine stopped** data list with RPM, raw TPS
ADC and calibrated percent. Verify about 0% released and 100% fully pressed.
Both XDFs group the editable endpoint constants under **TPS calibration**.

This is a manual endpoint calibration for M7.4.4's throttle potentiometer.
The supplied Bosch training document, section XXIII (PDF p.44 / printed p.38),
describes its variable-voltage position signal. The plugin workflow and ADC
endpoint rules are standalone implementation choices, not the ME7.4.4 motorised
throttle learning procedure.

## Install and connect

1. Copy `TU744.dll` (from `release/TU744-v0.0.1/tunerpro/`) into your TunerPro plugin
   directory (normally `Documents/TunerPro Files/Plugins`), then restart TunerPro RT.
2. Select **TU744 schema-4 RAM tuning** as emulation hardware and
   **TU744 schema-4 logging** as the acquisition interface. Configure the adapter's
   COM port in the plugin. It uses 19200 baud, 8N1; close other applications using
   that port. The plugin does not scan ports.
3. Open a **3072-byte schema-4 calibration BIN**, and select
   `TU744_schema4_speed_density.xdf` or `TU744_schema4_alpha_n.xdf` to match its
   fueling mode. Selecting an XDF does not change the ECU mode. A 512 KiB firmware
   image or an OEM map is not a calibration BIN for these definitions.
4. Select **Initialize Emulation Hardware**. If the ECU already has a tune, use
   **Download BIN from Emulator** and save a local copy before editing. Then select
   **Enable Emulation** for live edits. Uploads and live edits activate validated RAM transactions.
   Invalid updates leave the previous active tune intact.
5. Load `TU744_schema4.adx` for engine values, status flags and DTC counts.
   Select **Acquisition > Start/Stop Data Scan**, then
   **Acquisition > Show Data Lists > Engine** or **Status and blocks**. Logging and emulation
   share the plugin connection; logging does not require Enable Emulation.
6. Opening plugin configuration shows a persistent **TU744 controls**
   panel alongside TunerPro. It provides **Apply port**, **Read ECU status**, and
   **Save ECU tune to flash** shortcuts. Reopening configuration focuses the same
   panel. To retain changes, stop the engine, select **Save ECU tune to flash**,
   wait for the saved-and-verified result, then physically reset/key-cycle the ECU
   before starting. Saving a BIN on the PC does not save the ECU tune.
7. The panel's **Last BIN transfer** shows transfer progress and retains the
   final result. Upload success means RAM activation and readback matched;
   Verify success means the requested ECU bytes match the open BIN. A mismatch
   identifies the first differing address and bytes. Communication errors are
   reported separately. An upload does not save calibration to flash.

Flash save sends one request and polls status. It is not performed after every
cell edit. If communication fails during a write/save, its outcome can be
unknown: reconnect and read the ECU tune/status before retrying. Structural
calibration changes require stopped operation; related axis changes may need a
complete BIN upload so the ECU can validate them together.

## Storage and initial tune

All 3072 calibration bytes remain available. RAM edits disappear at reset unless
saved. Two NOR sectors retain alternating tune copies. The stock 1024-byte EEPROM
holds one full OEM-format history snapshot and two copies of standalone DTC
records. Interrupted history writes can lose that single OEM snapshot, while
calibration remains separate.

Generic builds do not bundle or substitute a base map. The dated stock-M95080
delivery package includes the user's explicitly supplied, converted LRE-B4 tune;
see its README and conversion report. Missing/unreadable calibration still
prevents start. A valid schema-4 tune can be uploaded and saved while stopped.

Cranking cells are VE percent. After-start and acceleration use multiplication
percentages with 100% neutral. Required fuel and injector dead time remain time
quantities.

## 2026-09-23 connection correction

Historical correction (superseded by plugin 0.4.0 / ADX 6.0): DLL 0.2.0 omitted
`TPP_EMU_CAP_CHIPEMULATION`: TunerPro detects the ECU but disables its emulation
toolbar, including download. The original ADX used sequential identifiers where
TunerPro requires name hashes; its command and display references do not resolve
correctly. The corrected generator matches the native host routine and all 44
identifiers in TunerPro's supplied ELM327 ADX. Component metadata now advertises
version 0.2 consistently with the ADX dependency. These changes require replacing
PC files and restarting TunerPro, without reflashing the ECU.

The follow-up fixes three remaining PC-side problems: ADX replies lacked the
publish-data flag, so valid packets did not update readings or the rate counter;
VerifyData returned S_OK (0) for matching data instead of the SDK's TRUE (1);
and transfers ignored the SDK progress interface. A persistent transfer result
now appears in the plugin panel. No ECU reflash is needed.

## XDF layout (fileversion 5.0)

Items are grouped by function (fuel, ignition, limiters, launch, idle, closed
loop, sensors, DTCs and so on). Every description gives the calibration offset
and the range the ECU accepts. On/off settings, including bits packed into
`0x5D4`, `0x7B4` and `0x916`, are checkboxes. Axis breakpoints are one-row tables.
Earlier XDFs numbered categories from 1, so TunerPro showed each item one
category off. Categories are now numbered from 0, as TunerPro expects.

**DTC switches** has one checkbox per diagnostic event with a running
standalone monitor, named by the P-codes it can raise. It writes the event
bitmap at `0x940`. Events without a standalone monitor are not listed,
because their bits have no effect. **DTC switches - individual codes** writes
the subtype masks at `0x980`. Use it only for events that raise several codes.
Do not clear every code of one event: the ECU treats an all-zero mask as all
codes on (schema-4 compatibility). Turn the event off in **DTC switches** instead.

## Plugin 0.3.0 / ADX 5.0 (24 September 2026)

Requires firmware from 24 September 2026 or later (monitor extension 2). The ADX
now polls one command per cycle instead of three, so values update faster. It has
two data lists. **Engine** shows RPM, MAP, TPS, temperatures, battery, AFR/target,
fuel trim, VE, injector pulse, advance, enrichments, idle target/valve position,
speed, gear and the number of active/stored DTCs. **Status and blocks** shows
named flags: crank sync, cranking, closed loop, overrun cut, rev limiter, fuel/spark
cut, launch, anti-lag, fan, pump, check-engine lamp, and one line per start/output
block (no sync, no valid tune, sensor, stale data, reset after save, deadline,
power, outputs not released, output fault). Raw quality bytes, tune generation,
save status and the private LRE fault masks were removed from the lists; the
plugin panel still reports them.

The plugin panel has **Read DTCs** and **Clear DTCs**. Read lists every stored DTC
with its P-code and description, whether it is active now, the check-engine lamp
state, how often it occurred, and the freeze frame captured when it was first set:
RPM, MAP, TPS, coolant and intake temperature, battery voltage, speed, engine
state and fuel trim. Reading works with any firmware that has the diagnostic
history. **Clear DTCs** asks for confirmation, needs the engine stopped with the
key on, and requires firmware from 24 September 2026. It then reads the list
back. A fault that is still present is stored again.
