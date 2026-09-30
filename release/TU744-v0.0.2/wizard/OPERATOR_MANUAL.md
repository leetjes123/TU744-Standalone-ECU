# Tuning Wizard operator manual

Firmware 0.0.2 adds Engine Protection / Knock Control: operating mode,
automatic/manual gain, sensor gain, two hardware filter bands, and RPM-based
window/threshold/load curves. OEM values are starting values; mode defaults to
disabled. Actual filter frequencies depend on board straps and clock. Retard per
event, maximum retard and recovery speed are single settings. Knock changes
require a stopped engine. Voltage, detection, freshness, fault and requested
retard appear in the dashboard/logs; ignition timing remains the base command.
See [firmware migration and verification notes](../docs/RELEASE-0.0.2.md).

## Files and connection

Open a **3072-byte schema-5 calibration** or connect a 19200-baud K-line adapter
and select **ECU > Read calibration**. Full firmware images are separate files.
The wizard checks protocol and live-frame compatibility when connecting.
An unsupported calibration protocol is rejected. An older standalone live-frame
revision permits tune backup and firmware update only; it is never decoded as the
new sync-loss layout. There is no ECU identity indicator.

Use the sidebar to open maps or settings. Map rows are load; columns are RPM.
The VE, target AFR and ignition load axes follow speed-density (MAP) or alpha-N
(TPS). Boost always uses TPS. Hover over controls for definitions and ranges.
Shared axis breakpoints are available under **Advanced > Shared Table Axes**.
Table tools retain selection, paste, smoothing, interpolation and undo/redo.
Compare tune highlights differences against another schema-5 file.

The sidebar follows tuning workflow rather than alphabetical order. **Tuning**
contains Fuel, Ignition, Idle Control, Boost & Motorsport, and Engine Protection.
Fuel opens first with the Fuel VE and AFR Target tables, followed by closed-loop
control, acceleration enrichment and starting/warm-up. **Setup** contains the
engine/fuel system, sensors/calibration, auxiliary outputs and advanced settings.
**Diagnostics & DTCs** is last, including the Fault Viewer, thresholds, monitor
enables and individual code enables. The Fault Viewer works without a loaded tune.

Search matches current and previous table names, category names, and common
abbreviations such as ECT/CLT, IAT, STFT, WUE and AE. Matching branches expand
automatically. Boost, cooling fan, fuel pump and coolant gauge now have separate
pages; injector setup and trigger reference are separate from everyday map tuning.

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

Monitor channels use consistent full names in lists and CSV headers, with compact
labels on instruments and graph legends. Examples: Engine Speed (RPM), Manifold
Absolute Pressure (MAP), Coolant Temperature (ECT), AFR Actual, AFR Target and
Short Term Fuel Trim (STFT). Hover a channel for its meaning and legacy aliases.
Search also recognizes old labels such as CLT, Measured AFR and Applied trim.
Existing CSV files remain readable; recognized headers display the new names
while retaining their original units and values. Commanded injection, calculated
duty cycle, base ignition timing and requested knock retard retain their distinct
meanings. Raw flags, table indices/weights and calibration generation are grouped
under **Advanced / ECU Status**.

The visual dashboard has an RPM gauge, MAP/TPS bars, and a mixture panel with
measured AFR, target AFR and applied STFT. Standard layout adds temperatures,
battery and fuel/ignition details; Compact keeps the primary instruments and
engine status. Diagnostic adds the full signal table. Recent RPM/MAP trends are
expandable and show up to 200 accepted samples, not a fixed time interval.
Instruments reflow into fewer columns in narrow windows. Gauge shapes are vector
drawings; instrument fonts are rendered at their display sizes and rebuilt when
the window moves between displays with different DPI settings. Gauge ranges are
display scales, not engine protection thresholds. The RPM gauge maximum is the
hard rev limit plus 1000 RPM, using the verified ECU tune when available or the
loaded calibration otherwise. Without a valid tune limit it uses 8000 RPM.

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

The bottom monitor row stays visible across tabs and during operations, wrapping
to fit the window. It includes measured AFR, Lean/Rich/Stoich, VE, ignition timing
and signed STFT correction. Unavailable or stale readings show labeled dashes.
Mixture uses the ECU's narrowband classification, or measured wideband AFR within
0.1 of the active tune's stoichiometric AFR for Stoich (14.7 until the active tune
has been read). Ignition timing is planned advance, and STFT is applied trim.

Logging records accepted monitor samples, including sync state, sync-loss count,
status bitfields and map weights. Both live logging and opened CSV files use
stacked graphs with a shared time axis and cursor. Recorded times come from
accepted packets, so pauses and irregular packet timing remain visible.

Use **Channels** to search, enable traces, assign each trace to a graph, select
full-log, visible-window or manual ranges, and change colors. **Graph configuration**
controls graph names, adding/removing panels, grid lines, trace width and linked
scales for matching units. Click a trace's legend to display its Y axis; hover
the legend to inspect its range. Other traces retain their own scales.

Wheel over a graph to zoom around the pointer. With **Drag to zoom** enabled,
click and drag across a region, then release to zoom all graphs into that time
interval. Dragging works in either direction; a click without dragging sets the
cursor. Turn **Drag to zoom** off to pan with a drag. Double-click fits the complete
log. Shift-drag selects an interval across all graphs without zooming automatically
and shows each visible channel's minimum, maximum and average.
Choose **Zoom selection** to inspect that interval. Right-click a graph for
time/range fitting and cursor controls. **Fit graph heights** fills the available
space; turn it off to adjust individual panel heights and scroll through panels.

**Pause view** freezes a live snapshot while capture and CSV recording continue.
**Follow live** resumes the latest window. CSV files offer playback from 0.1x to
8x speed and a cursor/time slider. Short spikes remain visible when zoomed out;
unavailable measured AFR and interruptions appear as gaps. Clearing live history
preserves graph settings and does not interrupt an active CSV recording.

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
Click **Program firmware** to start directly, without a tune-backup Save As dialog.
Both calibration slots are erased. An embedded tune is used only if the image contains one.
The bare stock-profile build contains no tune: after flashing, reconnect, open your
saved schema-5 tune, Write active tune, Save tune to ECU flash, and key-cycle.
Migrate older schema-4 tunes with `tools/migrate_schema5.py` before opening them.
The wizard waits for each erase and checks handler failure status where supported.
Firmware page checksum checking is disabled.

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
