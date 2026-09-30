# Planned changes for the next firmware release

Status: implemented for firmware 0.0.2 on `firmware/0.0.2`; see
[delivery and verification notes](RELEASE-0.0.2.md). Physical bench and engine
acceptance remain open. Original requirements below are retained for traceability.

## Knock control (after v0.0.1)

Include the work described in
`docs/audits/tu744-knock-2026-09-30/IMPLEMENTATION-PLAN.md` in the next
firmware build. The audit plan has been restored and reconciled with the
[global knock implementation](KNOCK.md). The approved tuning interface retains
gain, filter bands, windows and engine calibration with OEM starting values.

The existing plan covers IC access and bench self-test, calibration and
tuning support, OEM routine ports, knock-window scheduling and per-cylinder
retard, persistence and telemetry, and OEM A/B, timing, bench and engine
verification. CC195 boot initialisation is already complete. Resolve the
remaining ROM and board questions before implementing the dependent work.
Knock DTCs remain optional and excluded from the build unless separately
approved, as recorded in the existing plan.

## DFCO and idle detection (after v0.0.1)

Problem: the engine has trouble dropping into idle. In v0.0.1:

- Idle closed loop (`src/idle.c`) only runs when RPM ≤ idle target + 500.
  Above that it stays in `IDLE_RETURN`, which only holds or steps the IAC down
  to the base position and has no PID or spark correction. If the warm base
  position keeps RPM above target + 500, the engine hangs there.
- "Throttle closed" means two different things. Idle uses `0x5E6`, while DFCO
  uses a fixed 2.0 % (`tps.value <= 20`) in `src/engine_state.c`.
- The DFCO delay `0x603` is 2.0 s in the basemap, and its resolution is 0.1 s.
- DFCO has a MAP gate (`0x5EB`), and exit is at a fixed idle target + 200.

### Changes

1. **One closed-throttle flag.** Compute it once in the engine-state or sensor
   update, and use it for both idle mode selection and DFCO. Replace the fixed
   2 % in DFCO.
   - Optional: make the threshold RPM-dependent like OEM (a small curve that
     rises with RPM), with no debounce.
   - Keep the rule that there is no idle and no DFCO while the TPS is invalid.
     This already matches OEM.
2. **Remove the +500 RPM gate on idle feedback.** With the throttle closed and
   DFCO off, run `IDLE_FEEDBACK` so the integrator pulls the IAC down and idle
   spark retards on overspeed (OEM does this). If a gate is still wanted, make
   the margin a calibration value that can be set to 0. Keep the bumpless
   integral re-init on mode change, which already exists.
   - Check that the idle spark table (`0x610` axis ±250 rpm, `0x620` values
     −6…+6°) retards enough at overspeed. OEM pulls spark to about 10° ATDC at
     100 rpm over target.
3. **DFCO delay stays a tuning parameter** (`0x603`). OEM uses about 20–140 ms
   for reference, so revisit the 2.0 s basemap default.
4. **DFCO exit margin as a tuning parameter.** Replace the fixed idle target +
   200 with idle target + a calibrated margin (default 200).
5. **DFCO entry RPM stays a simple tuning parameter** (`0x5E9`). No OEM-style
   hysteresis or decaying margin.
6. **Remove the DFCO MAP gate.** Drop the `0x5EB` condition from
   `src/engine_state.c`, its validation, and the "Overrun maximum MAP" XDF/ADX
   and wizard entries. Leave the offset reserved.

Not planned: coolant-dependent DFCO (the fixed CLT ≥ 60 °C gate stays),
recovery enrichment after DFCO, idle target slewing, and off-idle IAC opening.

Each item needs calibration validation (`src/calibration.c`), XDF/ADX entries,
basemap defaults, wizard support where relevant, and tests.

### OEM reference (TU5JP M7.4.4 ROM, from disassembly and Keil runs)

**Closed throttle / idle detection**

- There is no idle switch; the throttle pot alone detects no-load.
- Throttle byte `93AC` = (ADC − learned closed position `ABCA`) >> 8,
  which is about (ADC − closed ADC) / 4.
- The closed flag `FD44.11` is set when `93AC` < the RPM curve at `0x18DD5`:

  | rpm   | ≤1440 | 2080 | 3360 | 4000 | 4640 | ≥5000 |
  |-------|-------|------|------|------|------|-------|
  | limit | 2     | 3    | 5    | 6    | 7    | 8     |

  In ADC counts above closed, that is about 8 to 32. There is no debounce and
  no MAP or load gate.
- A TPS fault forces the closed flag clear, so there is no idle and no DFCO.
- The learned closed position defaults to ADC ≈ 204 (about 1.0 V), with a
  plausibility floor of ADC 32.
- Qualified idle `FD28.2` (IAC side) requires:
  - the throttle closed;
  - no IAC or TPS fault;
  - a few other flags clear.

  It uses a ±120 rpm band around target. Idle adaptation needs 5 s inside that
  band.
- Idle mode `FD1E.10` (idle ignition map `0x14DA2` plus the idle regulator) is
  a demand-ratio test, not a throttle test. A slightly open throttle at 750 rpm
  stayed in idle mode.

**DFCO entry**

The cut is requested when all of these hold:

- throttle closed for about 20–140 ms (countdown `0x10E34`, depends on RPM and
  an index `9267`);
- the optional delay `0x10E04` has expired (index `9261`, possibly gear):
  0, 420–320, 330–250 or 200–120 ms;
- IAT above −30 °C;
- no inhibit (load-rise edge, diagnostic flags);
- rpm > restart speed + margin (760 → 480 rpm, as described in item 5).

**Restart speed** (`9299`, in 40 rpm units):

`9299 = ((9297 + target/10) >> 2) + coolant_add [+10 if FD62.5] [+5 if 9267 == 1]`

- `9297` is +50 rpm when steady, up to +600 rpm when RPM is falling fast
  (curve `0x14E5A` on decel rate).
- The coolant add is 2800, 1320, 640, 600, 560 and 560 rpm at codes 24, 44, 91,
  117, 184 and 224. Temperature °C = 0.75·code − 48; this offset is uncertain,
  and the alternative is −38.

Examples:

| Case                         | Restart speed | Entry speed     |
|------------------------------|---------------|-----------------|
| Warm (target 800), steady    | ≈ 1400 rpm    | 1880–2160 rpm   |
| Warm, fast decel             | ≈ 1960 rpm    |                 |
| 20 °C                        | ≈ 1680 rpm    |                 |
| −30 °C                       | ≈ 4040 rpm    | effectively off |

**DFCO exit** happens on any of: rpm ≤ restart speed, the throttle opening, or
an inhibit. The actual cut is also blocked by a torque-limit band. When it is
active, all four injectors are cut.

**Recovery enrichment:** the factor is `93F8`/128, and each step lasts 4
injection events. The closed-throttle table `0x1137A` goes 1.20 → 1.00; the
open-throttle table `0x1135A` goes 1.38 → 1.00.

**IAC and idle regulator**

- The target is max-selected against floors (warm: 800 rpm asked, table 750)
  and slewed.
- Chain: speed error → gain-scheduled regulator → persistent trims → requested
  air → inverse curve `0x15A80` → IAC steps (5–220). The motor moves one step
  per call and waits 5 calls before reversing.
- There is no dashpot table. The IAC always follows modelled air, and just off
  idle it is 2–3 steps more open.
- At 100 rpm overspeed with the throttle closed, the IAC dropped to 21–47 steps
  and spark went to 9.75° ATDC through the torque ceiling.
- On entry to idle mode the regulator states are reloaded from the current
  values (bumpless).
