# Calibration workspace layout ? proposed, awaiting approval

28 September 2026. Planning only: no application, calibration definitions,
firmware or packaged executable changes are authorized by this document.

## Proposed experience

Restore the original wizard's workspace navigation and grouped settings forms,
using the current standalone calibration names and capabilities. Product name
remains **Tuning Wizard**. The interface remains free of the previous branding
and ECU identity indication, as previously requested.

The sidebar has two sections:

- Tools: Dashboard, Diagnostics, Autotune, Logs.
- Calibration: Tune, Startup / Idle, Sensors, Fuel Trim, Accel Enrich,
  Limits / Launch, Drivetrain, Expert.

Selecting a calibration workspace highlights it and shows its relevant table
and settings links below. The main area retains open editor tabs. Selecting a
workspace returns to its last selected editor; on the first visit it opens the
starting page specified below. Switching workspaces preserves edits and tabs.
Selecting a tab also selects its owning workspace. Tools retain their own tabs.

The current alphabetical tree of generated categories is replaced by this
short, intentionally ordered workspace list. Every table and parameter keeps
its exact current name in links, editor headings, search results and help.
Friendly labels apply to workspaces, settings-page containers and controls.

## Workspace contents

| Workspace | Settings pages and tables | First visit |
|---|---|---|
| Tune | Fuel setup / load mode; Injector setup; Ignition / trigger setup; Boost control. Tables: Running VE, Target AFR, Injector dead time, Ignition advance, Ignition dwell, IAT ignition retard, Boost duty. | Running VE |
| Startup / Idle | Starting / running detection; Idle settings; Outputs (fan, fuel-pump prime and coolant gauge). Tables: Cranking VE, Warm-up multiplier, After-start multiplier, After-start duration, Idle target, Idle base position, Idle start addition, Idle spark correction, Coolant gauge high duty. | Idle settings |
| Sensors | Sensor settings; TPS calibration; Oxygen / wideband setup. Tables: MAP sensor transfer, Coolant thermistor resistance, Intake thermistor resistance. | Sensor settings |
| Fuel Trim | Closed-loop enable, operating conditions, correction limits, narrowband settings and wideband settings, using the existing individual parameter names. | Fuel-trim settings |
| Accel Enrich | Detection / decay settings; Acceleration multiplier; Acceleration RPM modifier. | Detection / decay settings |
| Limits / Launch | Rev limiter; Overrun fuel cut; Overboost cut; Launch control; Anti-lag. | Rev limiter |
| Drivetrain | Vehicle-speed setup; gear ratios; final drive; tyre circumference. | Drivetrain settings |
| Expert | Axis breakpoints; Diagnostic configuration (event switches, individual-code switches, thresholds); Runtime limits (Plan age limit); searchable All calibration index. | Axis breakpoints |

The two entries in the existing combined output category that concern boost
(Boost duty and Boost control) belong to Tune. Its fan, pump and gauge entries
belong to Startup / Idle > Outputs, matching the original workspace structure.
Fan idle feedforward stays with Idle settings. Cranking advance has its primary
home in Tune > Ignition / trigger setup, with a shortcut from Startup / Idle.
Relevant axis shortcuts accompany table pages; Expert provides their full index.

Diagnostic configuration edits remain calibration pages under Expert. The
Diagnostics tool remains exclusively the fault-reader/clear/freeze-frame view.
Autotune retains its dedicated tab, collection controls and proposal review.

## Settings and editor controls

1. Group settings pages into **Options**, **Modes**, and **Calibration values**,
   like the original wizard. Use named subgroups on long pages (for example
   operating conditions, correction limits, narrowband and wideband).
2. Options use checkboxes; enumerated modes use dropdowns; numerical values use
   aligned numeric inputs, units and contextual help. Preserve exact field names,
   supported choices, precision, limits and dependency rules.
3. Keep disabled dependent settings visible with their reason. Show modified,
   invalid and stopped-engine-only states beside the relevant setting. Keep TPS
   endpoint capture with the TPS fields and preserve its fresh/stopped checks.
4. Add a breadcrumb above the editor, for example
   `Calibration / Tune / Running VE`, and a short page description.
5. Keep table editing, heatmaps, axes, live-cell highlighting, comparison, undo,
   redo, copy/paste and transforms. Retain the existing names and units.
6. Restore an Edit menu with Undo/Redo and the corresponding shortcuts; add a
   Ctrl+P search palette across tables, numeric values, options and modes. Search
   opens the correct workspace/page, expands its subgroup and highlights the
   exact matching entry. Search also works by the current category names.
7. Restore tab controls for Close, Close others and Close all editor tabs. Keep
   the sidebar calibration status (No calibration / Ready / Modified) visible
   without overlapping navigation at smaller sizes.
8. Provide a consistent action strip: Read calibration, Write active tune, Save
   tune to ECU flash, and Live tuning. Existing file-save controls remain
   separately labelled. Keep current standalone write/save semantics and gates.

The original direct EEPROM actions, memory-writing controls and legacy-only
features are outside this layout change. Existing compact telemetry, sync-loss
reporting, connection protocol, firmware flashing and calibration validation
remain unchanged. No new raw-memory editor is proposed.

## Implementation after approval

1. Add a desktop-only workspace/page registry, separate from generated schema
   categories. Entries resolve by definition type, offset and flag mask where
   applicable. This prevents navigation wording from changing firmware data or
   the generator's category-based dependency construction.
2. Adapt the existing scalar/flag/dropdown renderers to accept the registry's
   explicit entry selections. Preserve editing buffers, undo, validation and
   stopped-engine checks. Splitting a generated category must not accidentally
   include unrelated settings on a page.
3. Replace sidebar navigation; add workspace ownership to editor tabs, default
   destinations, breadcrumbs and shared page controls. Retain the current
   diagnostics and autotune tab implementations.
4. Add navigation/search for every current definition. The All calibration
   index is also a visible fallback for any future unmapped definition, while
   coverage checks require explicit mapping before release.
5. Verify coverage and unchanged names/metadata; exercise grouped inputs,
   dependency visibility, search destinations and tabs. Compare before/after
   calibration bytes for representative edits through the same definitions.
6. Build and run the existing checks, inspect desktop screenshots with a loaded
   tune (including narrow and high-contrast views), update the manual and
   package the approved application.

Acceptance: all current entries have exactly one primary home; optional
shortcuts resolve to the same editor. No entries disappear. All existing table
and parameter names, offsets, masks, sizes, units, scaling, ranges and validation
rules are preserved. A sidebar/page-selection action cannot modify calibration
bytes or initiate an ECU write.

## Evidence and scope

Navigation reference: the original application's `src/app.cpp`,
`activateWorkspace`, `drawSidebar` and grouped scalar-page rendering. This
reference informs interaction and grouping only.

Current definition inventory: `src/cal_defs.cpp`, generated by
`tools/wizard_definition.py`. The proposed mapping below contains all 214
editable definitions: 29 tables (including axis tables), 104 scalar values,
77 flags and 4 dropdowns. Counts are definitions, not independent byte ranges.

Supplied functional terminology reference consulted: Bosch training document
1.3.277, PDF p.15 / printed p.9 (MAP/IAT), PDF p.17 / printed p.11 (engine-speed
reference), PDF p.26 / printed p.20 (coolant), PDF p.39 / printed p.33 (upstream
oxygen), PDF pp.50?52 / printed pp.44?46 (diagnostic operations and equipment
variants). These are supplied references, not proof of standalone addresses,
feature availability or calibration semantics. The layout preserves functions
and names already in the standalone definitions; it assigns no new pin functions
or vehicle behavior and does not transfer ME7.4.4 equipment assumptions.

## Complete proposed primary-home inventory

### Tune (17 entries)

Tables:

- Running VE
- Target AFR
- Injector dead time
- Ignition advance
- Ignition dwell
- IAT ignition retard
- Boost duty

Scalars:

- Stoichiometric AFR
- Required fuel per 720 degrees
- Maximum injector duty
- Injection phase
- Cranking advance
- Trigger reference offset

Flags:

- Use target AFR in fuel calculation
- Alpha-N fueling (TPS load axis)
- Use CC9 dwell feedback
- Boost control

### Startup / Idle (26 entries)

Tables:

- Cranking VE
- Warm-up multiplier
- After-start multiplier
- After-start duration
- Idle target
- Idle base position
- Idle start addition
- Idle spark correction
- Coolant gauge high duty

Scalars:

- Idle start duration
- Idle TPS threshold
- Idle RPM deadband
- Idle proportional gain
- Idle integral gain
- Idle step interval
- IAC maximum position
- IAC home timeout
- IAC homing steps
- Fan idle feedforward
- Fan on temperature
- Fan off temperature
- Fuel pump prime
- Running threshold
- Cranking re-entry threshold
- Running qualification

Dropdowns:

- Idle mode

### Sensors (25 entries)

Tables:

- MAP sensor transfer
- Coolant thermistor resistance
- Intake thermistor resistance

Scalars:

- Wideband AFR at 0 V
- Wideband AFR at 5 V
- Wideband warm-up time
- Wideband good-signal time
- Wideband minimum valid signal
- Wideband maximum valid signal
- TPS closed ADC
- TPS open ADC
- Coolant thermistor pull-up
- Intake thermistor pull-up
- TPS filter
- MAP filter
- Coolant filter
- Intake air filter
- Oxygen sensor filter
- Battery filter
- Sensor freshness limit
- Minimum battery voltage

Flags:

- Wideband signal qualification
- Upstream heater relay fitted
- Downstream heater relay fitted

Dropdowns:

- Oxygen input

### Fuel Trim (17 entries)

Scalars:

- STFT maximum (lean correction)
- STFT minimum (rich correction)
- STFT update interval
- STFT delay after start
- STFT minimum RPM
- STFT maximum RPM
- STFT minimum coolant
- STFT maximum TPS
- Narrowband trim step
- Narrowband lean threshold
- Narrowband rich threshold
- Narrowband minimum target AFR
- Narrowband maximum target AFR
- Wideband trim gain
- Wideband AFR deadband
- Wideband maximum trim step

Flags:

- Short-term fuel trim

### Accel Enrich (5 entries)

Tables:

- Acceleration multiplier
- Acceleration RPM modifier

Scalars:

- Acceleration TPS-rate threshold
- Acceleration qualification time
- Acceleration decay

### Limits / Launch (27 entries)

Scalars:

- Hard rev limit
- Rev-limit recovery
- Soft-cut start RPM
- Soft-cut maximum
- Overrun entry RPM
- Overrun maximum MAP
- Overrun delay
- Overboost cut MAP
- Launch RPM limit
- Launch maximum speed
- Launch minimum TPS
- Launch arm time
- Soft-launch start RPM
- Soft-launch maximum cut
- Anti-lag maximum time
- Anti-lag maximum coolant
- Anti-lag maximum intake air
- Anti-lag ignition advance
- Anti-lag VE

Flags:

- Soft rev cut
- Overrun fuel cut
- Launch control
- Soft launch
- Anti-lag
- Anti-lag cuts spark instead of fuel

Dropdowns:

- Rev-limit cut
- Launch cut

### Drivetrain (8 entries)

Scalars:

- VSS pulses per metre
- Final drive ratio
- Gear 1 ratio
- Gear 2 ratio
- Gear 3 ratio
- Gear 4 ratio
- Gear 5 ratio
- Tyre circumference

### Expert (89 entries)

Tables:

- RPM axis
- MAP axis
- TPS axis
- Temperature axis
- AE TPS-rate axis
- AE previous-TPS axis
- AE RPM axis
- Idle RPM-error axis

Scalars:

- Plan age limit
- DTC fail samples
- DTC pass samples
- Fuel-trim limit time
- Narrowband activity window
- Narrowband slow-response time
- Misfire crank-window deviation
- Misfire occurrences per 128 observations
- VSS maximum plausible speed
- TPS slew limit
- MAP slew limit
- Temperature slew limit
- Battery slew limit
- DEPHIA capture timeout
- DEPHIA minimum delay
- DEPHIA maximum delay
- DEPHIA direction delta
- Output feedback fail count

Flags:

- P0106/P0107/P0108/P0109 MAP sensor
- P0110/P0111/P0112/P0113 Intake air temperature sensor
- P0115/P0116/P0117/P0118 Coolant temperature sensor
- P0120/P0121/P0122/P0123 Throttle position sensor
- P0130 Upstream O2 sensor circuit (narrowband only)
- P0130/P0131/P0132/P0134 Upstream O2 sensor signal (narrowband only)
- P0133 Upstream O2 slow response, lean to rich (narrowband only)
- P0133 Upstream O2 slow response, rich to lean (narrowband only)
- P0171/P0172 Fuel trim at its lean/rich limit
- P0300 Misfire, multiple cylinders - level 1
- P0300 Misfire, multiple cylinders - level 2
- P0301 Misfire cylinder 1 - level 1
- P0301 Misfire cylinder 1 - level 2
- P0302 Misfire cylinder 2 - level 1
- P0302 Misfire cylinder 2 - level 2
- P0303 Misfire cylinder 3 - level 1
- P0303 Misfire cylinder 3 - level 2
- P0304 Misfire cylinder 4 - level 1
- P0304 Misfire cylinder 4 - level 2
- P0335 Crankshaft position sensor circuit
- P0336 Crankshaft position range/performance
- P0501/P0502/P0503 Vehicle speed sensor
- P0560/P0561/P0562/P0563 System voltage
- P0606 Processor self-test
- P1327 Cylinder phase (DEPHIA) - no capture
- P1327 Cylinder phase (DEPHIA) - implausible delay
- P1523/P1524/P1525 Idle stepper driver (L9935)
- P1526/P1529 Idle stepper position supervisor
- P0123 Throttle position circuit high (out of range, high side)
- P0122 Throttle position circuit low (out of range, low side)
- P0120 Throttle position sensor circuit (no conversion, stale or misconfigured)
- P0121 Throttle position range/performance (changes faster than its slew limit)
- P0108 MAP sensor circuit high (out of range, high side)
- P0107 MAP sensor circuit low (out of range, low side)
- P0109 MAP sensor circuit intermittent (no conversion, stale or misconfigured)
- P0106 MAP sensor range/performance (changes faster than its slew limit)
- P0113 Intake air temperature circuit high (out of range, high side)
- P0112 Intake air temperature circuit low (out of range, low side)
- P0110 Intake air temperature circuit (no conversion, stale or misconfigured)
- P0111 Intake air temperature range/performance (changes faster than its slew limit)
- P0118 Coolant temperature circuit high (out of range, high side)
- P0117 Coolant temperature circuit low (out of range, low side)
- P0115 Coolant temperature circuit (no conversion, stale or misconfigured)
- P0116 Coolant temperature range/performance (changes faster than its slew limit)
- P0563 System voltage high (out of range, high side)
- P0562 System voltage low (out of range, low side)
- P0560 System voltage malfunction (no conversion, stale or misconfigured)
- P0561 System voltage unstable (changes faster than its slew limit)
- P0502 Vehicle speed sensor circuit low (pulses lost after the vehicle was moving)
- P0501 Vehicle speed sensor range/performance (implausible change between samples)
- P0171 System too lean (trim held at its positive (lean) limit)
- P0172 System too rich (trim held at its negative (rich) limit)
- P0132 Upstream O2 sensor circuit high (signal at the upper rail)
- P0131 Upstream O2 sensor circuit low (signal at the lower rail)
- P0134 Upstream O2 sensor no activity (no switching during the activity window)
- P0130 Upstream O2 sensor circuit (stale signal)
- P1529 Manufacturer specific - idle actuator range/performance (homing did not finish in time)
- P1526 Manufacturer specific - idle actuator circuit (stepper bridges could not be switched off)
- P1525 Manufacturer specific - idle actuator group (no SPI response from the driver)
- P1525 Manufacturer specific - idle actuator group (repeated open-load response)
- P1524 Manufacturer specific - idle actuator group (driver diagnostic code 10b)
- P1523 Manufacturer specific - idle actuator group (driver diagnostic code 00b)
- DEPHIA cylinder-1 polarity inverted

