# Tuning Wizard operator manual

## Files and connection

Open a **3072-byte schema-4 calibration** or connect a 19200-baud K-line adapter
and select **ECU > Read calibration**. Full firmware images are separate files.
The wizard checks protocol and live-frame compatibility when connecting.
An unsupported calibration protocol is rejected. An older standalone live-frame
revision permits tune backup and firmware update only; it is never decoded as the
new sync-loss layout. There is no ECU identity indicator.

Use the sidebar to open maps or settings. Map rows are load; columns are RPM.
The VE, target AFR and ignition load axes follow speed-density (MAP) or alpha-N
(TPS). Boost always uses TPS. Hover over controls for definitions and ranges.
Axis breakpoints are also available as tables under **Axis breakpoints**.
Table tools retain selection, paste, smoothing, interpolation and undo/redo.
Compare tune highlights differences against another schema-4 file.

**Save file** writes the local calibration to disk. It does not change the ECU.
Opening a file does not silently migrate or normalize its calibration.
Read operations preserve the existing file until the whole ECU read succeeds.
Local changes are guarded before opening, reading or closing. Safety backups
are kept under `%LOCALAPPDATA%\TuningWizard\backups` with a `.twbackup` extension.

## Write and save

**Write active tune** updates ECU RAM. When the ECU baseline is unknown, the
wizard first reads it while preserving your open file, then computes the changes.
Map-only edits use live cell writes, up to 16 bytes within one map. Settings and
axes use a validated transaction. A successful write includes readback; failures
invalidate the host baseline, and the next write reads it again.

**Live tuning** sends local map edits as you make them, while connected and
synchronized. It pauses if you change a setting or axis. Use Write active tune
for those changes. Structural settings require a stopped engine and completed
idle homing; the ECU also enforces its own acceptance conditions.

The dashboard shows **Unsaved tune in ECU RAM** when persistence is needed.
Stop the engine and select **Save tune to ECU flash**. The wizard polls until the
ECU reports success, failure or timeout. **A key cycle is required afterwards**:
service mode leaves outputs off until reset. A timeout is an unknown result,
not a successful save. Local edits must be written to the active tune first.

## Live data and logs

The dashboard shows RPM, MAP, TPS, temperatures, battery, measured/target AFR,
oxygen voltage, trim, VE, planned pulse width and advance, enrichment, idle,
speed, gear, fuel-plan cells/weights, sync, sync losses, cuts and output inhibits.
The frame remains exactly 40 bytes. Sync loss is a cumulative counter since ECU
reset, saturated at **65535** in this frame; the dashboard displays **65535+**.
CSV values of 65535 have the same saturation meaning.

Planned injector duty is derived from pulse width and RPM (`us * RPM / 600000`)
using the standalone firmware's per-revolution pulse period. It costs no frame bytes.
Planned pulse width, duty and advance do not prove an output fired: admission,
inhibits and cuts still apply. A zero measured AFR means unavailable, including
narrowband mode. The dashboard shows a dash; CSV retains zero and a separate
AFR-valid signal. Target AFR is never substituted for measured AFR.

Logging records accepted monitor samples, including sync state, sync-loss count,
status bitfields and map weights. Open a CSV log for plots, zoom and comparison.
Unavailable or stale live data is not shown as fresh. A lost connection requires
reconnecting; live tuning does not resume automatically.

## Autotune suggestions

Select wideband input and turn closed-loop trim off. The separate **Autotune** tab collects
samples only with warm, steady running, acquired sync, no cuts, no output inhibits
and neutral enrichment. Set the exhaust delay for your installation. Each cell
requires at least 20 samples and 10 weighted samples. AFR errors outside 20%
are rejected; proposed VE changes are bounded to 2% per application.

Review the proposed cells, then choose **Apply proposals locally**. Write active
tune or Live tuning sends them through the normal verified path. Autotune never
saves to flash. Changing a tune or losing eligibility clears or gates collection.
This is a software implementation; exhaust-delay settings and engine behavior
still require validation on the installation.

## Diagnostics and freeze frames

Open **Diagnostics** from its tab, the sidebar or View menu. No calibration
needs to be loaded. Connect and select **Read fault memory**. The scan shows
stored DTCs, currently failing records and the MIL demand at scan time. These
are snapshots, not continuously polled live values. Counts include separately
identified controller faults; they are not OBD pending/permanent categories.

Search codes/descriptions and filter current or stored-not-failing records.
Click any DTC card to open its **Freeze frame**: engine speed, MAP, throttle,
coolant, intake temperature, battery, speed, fuel correction and engine state.
The snapshot belongs to the first confirmed subtype/code, which can differ
from the latest code. Subsequent occurrences retain that frame. The occurrence
clock is approximate ECU on-time, not a date or wall-clock timestamp. Older
records without fuel correction show **Unavailable**. Controller-specific
records show their recorded reason/count/ticks and explicitly have no engine
freeze frame. No live values are substituted for missing stored data.

**Clear DTCs...** opens a confirmation. Keep the key on and stop the engine.
The wizard checks stopped state again, sends clear once and reads the memory
back. Acceptance is not proof of persistent storage: history saves at key-off.
The clear resets native records, freeze frames and readiness. Faults can
requalify; active controller faults remain. A lost acknowledgement is reported
as uncertain and never causes an automatic second clear. Failed or changing
scans retain the previous result and label it accordingly. Reconnecting discards
the previous ECU's snapshot. Diagnostics pauses live tuning and sample
collection; resume these explicitly after the scan.

## Firmware update

Choose **ECU > Update firmware** and a 512 KiB raw image with a valid reset vector.
The default option reads and saves the current ECU tune before programming.
Both calibration slots are erased. An embedded tune is used only if the image contains one.
The bare stock-profile build contains no tune: after flashing, reconnect, open your
saved schema-4 tune, Write active tune, Save tune to ECU flash, and key-cycle.
The wizard waits for each erase and verifies handler failure status and page sums.
A handler without verification support is explicitly reported as unverified.

On an uncertain programming result, the wizard leaves the RAM handler running
and reports failure rather than resetting an incomplete image. Physical flashing
and recovery must still be validated on an ECU with a recovery path available.
The supplied stock-profile image remains labelled **EXPERIMENTAL**.

## Functional evidence

Bosch training document 1.3.277 (September 2000) supplies terminology: PDF p.15
(printed p.9) MAP/IAT; PDF p.17 (printed p.11) 60-2 speed reference; PDF p.26
(printed p.20) coolant; PDF p.39 (printed p.33) upstream oxygen polarity and
equipment differences. These are supplied vehicle-function references, not proof
of standalone addresses or strategies. Standalone behavior comes from this
repository's firmware source. M7.4.4 and ME7.4.4 equipment variants remain distinct.

For diagnostics, supplied Bosch PDF p.52 / printed p.46 describes scan-tool
fault reading, associated contexts and erasure; PDF p.36 / printed p.30
describes fault-memory saving during power latch. The standalone command and
record contracts above are proven by this repository, not inferred from the
training document. TU5JP event/subtype P-code labels are imported from the
ROM-derived report table, with unmapped entries displayed explicitly.
