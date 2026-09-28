# Knock control: OEM-parity implementation plan

Goal: reproduce the TU5JP M7.4.4 knock control **as close to the OEM firmware
as the board allows**, with every OEM calibration value exposed as a
standalone tuning parameter.

The ROM evidence is in
[`engines/TU5JP/archive/36-knock-ic-cc195-init-and-calibration.md`](../../../engines/TU5JP/archive/36-knock-ic-cc195-init-and-calibration.md)
and [`30-knock-control.md`](../../../engines/TU5JP/archive/30-knock-control.md).
The reference image is `bins/M744_C167_FULL.bin` (SHA-256 `5710015f…37b3`).

## Status

| Phase | Content | State |
|---|---|---|
| 0 | Close the ROM and board questions that parity depends on | open |
| 1 | CC195 boot initialisation, identical to the OEM from reset | **done**: `board_knock_ic_boot`; proven by `tests/keil_knock_boot.py` (Keil, ≤ 0.25 µs per event) and `tests/test_knock_boot_target.py` (emulator) |
| 2 | IC access layer (injected AN15) and on-demand bench self-test | planned |
| 3 | Calibration block, validation, XDF/ADX, tuning client | planned |
| 4 | Native ports of the OEM knock routines, each proven against the ROM | planned |
| 5 | Real-time integration: window ISR, segment index, per-cylinder retard | planned |
| 6 | Persistence of learned values, telemetry, optional knock DTCs | planned |
| 7 | A/B verification against the OEM image, bench and engine acceptance | planned |

## Rules

1. **Port, then prove.** Every OEM routine gets a native C port that is
   compared with the unchanged ROM routine over randomized inputs, using the
   existing `tests/oem_harness.py` pattern. `--target` then repeats the
   comparison with the Keil-compiled code on the emulator.
2. **Deviations are listed, never silent.** Only the timer and interrupt
   allocation already owned by the standalone forces changes. Each deviation is
   in the register below, with its effect.
3. **Defaults are the ROM.** The calibration defaults are generated from the
   image, and a test fails if a default differs from its ROM byte.
4. **Units follow the ROM:** 0.75° crank per timing/window count, detection
   ratios in sixteenths of the adaptive noise reference, rpm axis = rpm/40.

## OEM → standalone mapping

| OEM (ROM address) | Standalone |
|---|---|
| Port init `sub_32886`, knock init `sub_493BC`, filter select `sub_493DC` | `board_init` + `board_knock_ic_release` (phase 1, done) |
| T7 counts falling crank teeth on P2.15; CC26 compare at `F7A8 + 18` (`sub_2B03E`) | T0 already counts the same falling teeth on P3.0 (`T01CON = 0x414A`). A free CAPCOM1 channel on T0 in compare mode 0 fires at the same tooth count after the segment reference. |
| T8 window timer (`fCPU/64` running, `/512` low-res); CC16 compare mode 0, `CC16IC = 0x17` | CC16 re-allocated to **T7** (standalone's free-running `fCPU/16`), compare mode 0, `IRQ(5,3)` (same level/group as the OEM). T8 stays with boost. |
| Window ticks `933D·F806/240`, `933E·F806/240` (`sub_491BE`) | Compute `F806` and the ticks in OEM units, then convert to T7 ticks, so angle quantization is identical |
| CC16 ISR `0x2AE42`: MF edges, null-start sample after 25 T1 ticks, test-pulse gain `011`, KSA3 schedule, sample, next gain | Same ISR structure and order |
| Injected conversion `sub_2C156` (`ADCON = 0x062F`: `ADWR = ADCIN = 1`) | Enable ADWR/ADCIN on the standalone scan and use the same injection sequence |
| Task chain `0x2914C` inside the window-open ISR | Same place; native ports of `sub_49296`, `sub_49522`, `sub_4A084`, `sub_4AB20` (+ diagnostics) |
| Window angles and self-test scheduler `sub_48FA6` in `task.divider_10` (≈100 ms nominal) | Foreground at the same nominal rate |
| Segment counter `F8D1` (0..3): seeded at the gap from `94FF`, corrected by DEPHIA on P2.8 to 1/3 (`0x67DBA`) | A rotation segment index with the same semantics. The standalone already captures DEPHIA. |
| Slot `F882 = (F8D1+3) mod 4`; retard `9378[slot]` added at `0x36DFA` using index `9203 = F8D1` | The same index drives detection and the ignition event's retard |
| Coolant `950E`, rpm `F8AC`, load `F86E` | `950E`/`F8AC` from the existing native OEM producers; load from the standalone load model (deviation D4) |

## Phase 0: questions to close first

* **Flag producers** that change knock behaviour: `FD44.2`, `FD44.7`
  (threshold multipliers and filter constants), `FD44.6` (forced enable),
  `FD42.0/.2/.4/.5/.6/.12/.13/.14`, `FD1E.9`, `8606` bits, `9708` (detection
  inhibit). Also the `93B0` threshold trim, `sub_4AB20` (extra retard `F894`),
  `sub_49A0A` (coolant condition), and the second threshold set
  `0x18C8C…0x18CBC`.
* **Persistence:** are `93B7[]` and `A997[]` stored in the 95080? Which
  routines read and write them, and when?
* **Window reference:** run the OEM image synced on the emulator (the
  `HANDOVER.md` recipe) and measure which tooth after the gap CC26 fires on, and
  the P8.0 open/close angles at several rpm. Compare with the standalone's TDC
  (`trigger10`). This fixes the standalone window reference as an angle, not an
  assumption.
* **Slot ↔ cylinder:** which `F8D1` value is cylinder 1 (for labelling only;
  control is self-consistent regardless).
* ~~Real OEM boot hold~~: measured in the Keil simulator (phase 1): 320 836
  CPU states = 16.042 ms. A stock-ECU scope trace of P3.5/P3.6 is still worth
  taking, because Keil does not charge external-RAM (BUSCON1) wait states.
* **Board straps** on the CC195 (processor held in reset): `BF0/BF1/BF3`,
  `T0..T2`, the `X1` frequency, `KSA1/KSA2`, `TP0..TP2`. These give the two
  filter frequencies P8.5 can choose between, and whether the boot state is the
  CC195 Reset code.

## Phase 1: boot initialisation (done)

The CC195 is not serial; its "init" is a pin timeline. The standalone
reproduces the OEM timeline **from reset**, verified in the Keil C166 simulator
against the unchanged ROM.

### Lines

| C167 | Dir | CC195 pin | Function |
|---|---|---|---|
| P3.1 | out | G0 (9) | gain select bit 0 |
| P3.2 | out | G1 (11) | gain select bit 1 |
| P3.3 | out | G2 (10) | gain select bit 2 |
| P3.5 | out | KTI (4) | test pulse enable |
| P3.6 | out | KSA3 (18) | 1 = sensor inputs disconnected |
| P8.0 | out | MF (27) | measurement window, 1 = integrate, 0 = reset/hold |
| P8.5 | out | BF2 (8) | band-pass select bit 2 |
| AN15 | in | KI (28) | integrator output (read later by injected conversion) |

All seven outputs are push-pull (ODP3/ODP8 bits 0). Before they are driven,
the CC195's internal 50 kΩ pull-ups hold them high.

### Timeline at the IC (20 MHz CPU states from reset)

| # | OEM ROM | OEM (Keil) | Standalone code | Standalone (Keil) | Pins after the step |
|---|---|---:|---|---:|---|
| 0 | reset: all ports inputs | 0 | reset (`START167.A66` starts T1 = fCPU/16) | 0 | all high via pull-ups: G=111, KTI=1, KSA3=1, MF=1, BF2=1 |
| 1 | `0x3289A` P3 latch `B5FE`, `0x3289E` DP3 `35FE` | 1 090 693 | `board_knock_ic_boot`: wait for T1 overflow + `KNOCK_IC_PORT_T1`, `P3 \|= 0x6E`, `DP3 \|= 0x6E` | 1 090 692 | G2..G0, KTI, KSA3 now driven high (no level change) |
| 2 | `0x328CC` P8 latch `AE`, `0x328D0` DP8 `AF` | 1 090 768 | 3 NOP passes, P8 latch, `DP8 \|= 0x21` | 1 090 772 | **MF low** (integrator reset), BF2 driven high |
| 3 | hold: EEPROM read, 1244 bytes, init list | 320 836 | `(u16)(T1 − mf_low) < KNOCK_IC_HOLD_T1` | 320 827 | unchanged |
| 4 | `0x493D0` `BSET P3.1`, `0x493D2` `BCLR P3.2` | 1 411 604 | `PIN_KNOCK_G0 = 1; PIN_KNOCK_G1 = 0` | 1 411 599 | **G = 101** (gain x32, index 4) |
| 5 | `0x493D4` `BSET P3.3`, `0x493D6` `BCLR P3.5` | 1 411 610 | `PIN_KNOCK_G2 = 1; PIN_KNOCK_KTI = 0` | 1 411 605 | **KTI low** (test pulse off) |
| 6 | `0x493D8` `BCLR P3.6` | 1 411 613 | `PIN_KNOCK_KSA3 = 0` | 1 411 608 | **KSA3 low** (sensor connected) |
| 7 | `0x49450` `BMOV P8.5, FD40.9` (16 kHz → 1) | 1 412 266 | `PIN_KNOCK_BF2 = bit2(code(16))` | — | BF2 stays 1 |

Every observable event is within 5 CPU states (0.25 µs) of the OEM on both
release profiles, and steps 4–6 keep the OEM's 6- and 3-state spacing. The
reference is time since reset: T1 is started in the startup code, so profile
and data-layout differences in startup time do not move the timeline.
`board_knock_ic_boot` runs after `iac_disable()` and before any EEPROM traffic.
Before that point the knock lines are untouched inputs, exactly as under the
OEM before its port init.

Tests: `tests/keil_knock_boot.py` (Keil, bus wait states, from reset, both ROM
and standalone) and `tests/test_knock_boot_target.py` (c167re emulator, pin
states and BF2 against the ROM jump table for 0..19 kHz).

**Not included (MCU-internal, no effect on the IC):** `sub_493BC` also sets
CC16 to compare mode 0 on T8 with `CC16IC = 0x17`, and CC26 to compare mode 0
on T7 with `CC26IC = 0x58` (enabled). `sub_48E3C` and `sub_4987C` seed knock
RAM state, and `0x2C1F8` sets `ADCON = 0x062F`. These belong to the running
knock loop (phases 2 and 5). CC26 in particular cannot be enabled without its
ISR, and the standalone's T7 is a timer, not the OEM tooth counter
(deviations D1–D3).

**Limits:** Keil charges the BUSCON0 wait states but not BUSCON1 (external
RAM), so a real stock ECU may be slightly slower. Timing also assumes the same
20 MHz clock. A scope comparison on the bench is the final check (phase 7).

## Phase 2: IC access and bench self-test

* **ADC.** Set `ADWR = 1` and `ADCIN = 1` as the OEM does (`ADCON = 0x062F`
  semantics), keeping the standalone scan. `hal_knock_sample()` then reproduces
  `sub_2C156`: save ADDAT2, write `channel << 12`, set ADCRQ, wait, read
  `& 0x3FF`, restore. All existing ADC/sensor suites must pass unchanged.
* **Service command `0x31` "knock IC self-test"**, engine stopped and
  job-stepped so plan age and foreground progress are respected. The OEM's own
  checks, on demand:
  1. idle KI with MF low;
  2. null window (KSA3 = 1), start sample after 25 T1 ticks and end sample;
  3. test-pulse window (KTI = 1, gain `011`);
  4. test pulse at gain codes 0, 1, 2, 3.

  Results are judged with the OEM limits: start within ±25 of `0x25`
  (`0x11095`), test-pulse integral ≥ 179 (`0x11097`).
* `tools/tune_client.py knock-selftest`.

## Phase 3: calibration and tuning

Block `0xA00…0xBFF` (512 bytes, currently unused). About 420 bytes hold all
OEM knock items from archive 36 section 5:

* filter kHz;
* window start map 16×4 and window length curve;
* four per-cylinder threshold curves, the second threshold set, threshold
  multipliers;
* attack, max retard, both hold curves, spread, fallback;
* enable load curve, rpm, coolant;
* load-zone curves, gain table/initial index/reference, filter constants,
  drift limits;
* learned-cell scalars and self-test limits;
* rpm and load axes.

Two standalone-only switches are added: **knock enable** and **monitor only**
(detect and log, no retard).

* Defaults are generated from the ROM (as `scripts/gen_knock_cal.py` does now),
  with a test that every default equals its ROM byte.
* `cal_validate` ranges. `structural_ranges` is split so the tables are
  live-tunable; filter kHz and the enable switches need a stopped engine.
* XDF category "Knock" in `tools/tunerpro_definition.py`, with displayed units
  (degrees, ×noise ratio, kHz, events). ADX logging channels.
* Schema bump and `convert_lre_b4.py` migration (knock block = OEM defaults,
  enable off).

## Phase 4: algorithm ports, ROM-proven

One native module `src/knock.c` with OEM state layout names. Each item gets
≥ 1000 randomized ROM-parity cases, then `--target` Keil parity.

| ROM | Function |
|---|---|
| `sub_49458`, `sub_49940` | rpm-curve evaluation into thresholds and governor limits |
| `sub_48FA6` | window angles and the self-test/null scheduler |
| `sub_491BE` | window ticks, integrator offset and drift |
| `sub_49296` | sample latch, offset subtraction, test/null markers (`933C`) |
| `sub_49522` | ratio, threshold, decision, reference filter, gain autorange |
| `sub_4A084` (+`4A1AE`) | retard governor, learned cell/matrix, cross-cylinder clamp, output `9378[]` |
| `sub_4AB20` | additional retard `F894` |
| `sub_49D88`, `sub_49A0A`, `sub_49FB2` | enable conditions and load zones |
| `sub_48E3C`, `sub_4987C`, `sub_4FD6C` | initialisation and adaptive reset |
| `sub_4B4BE`, `sub_4B7B0`, `sub_4B872`, `sub_4BA3C` | IC health diagnostics |

`engines/TU5JP/src/knock_control.c` becomes documentation only once this
exists. Its self-test and offset model is known to differ from the ROM.

## Phase 5: real-time integration

* **Reference compare** on a free CAPCOM1 channel allocated to T0 (confirm
  which of CC2/CC3/CC5/CC7/CC10–CC13 is unused), at the tooth count found in
  phase 0, `IRQ(6,3)`.
* **CC16 on T7**, window ISR as `0x2AE42`. The task chain runs in the
  window-open ISR at level 5, below crank (15), compares (12–14) and the tick
  (10). Worst-case ISR time is measured in the Keil simulator with wait states.
* **Segment index** with `F8D1` semantics. The same index goes to detection and
  to `board_schedule`.
* **Per-cylinder retard** in `board_schedule`: each coil event's angle uses
  `advance10 − retard10[slot]` for the slot that event fires, with
  `retard10 = 9378 × 7.5` (exact: `× 15 / 2`). The standalone advance clamps
  apply after the sum, as the OEM adds before its own clamps (`sub_36F90`).
* Knock enrichment stays out until `q.knock_fuel_enrichment_join` is resolved.

## Phase 6: persistence, telemetry, DTCs

* Persist learned trims/matrix exactly as phase 0 finds the OEM does, within
  the stock 1 KiB 95080 budget.
* Telemetry (new command or monitor extension): per-slot noise reference, gain
  index, last ratio, retard, knock count, IC health, `933C` markers.
* Optional: knock DTC events `0x32…0x39` through the existing OEM DTC
  lifecycle. The owner excluded these earlier; confirm before enabling.

## Phase 7: verification and acceptance

1. **Emulator A/B.** The same 60-2 crank and the same AN15 stimulus (scripted
   `adc.set_volts` per window) go into the OEM image and the standalone. Compare:
   * P8.0 open/close angles (equal after unit conversion, ±1 tick);
   * the gain-code sequence per slot on P3.1–3;
   * the sample instants;
   * per-slot retard over the scenarios: quiet, single-cylinder knock, all
     cylinders, decay, gain autorange, self-test and null cycles, enable
     hysteresis.
2. **Keil simulator.** Window-edge latency and task-chain time with bus wait
   states, at 1000–7000 rpm with ignition and injection active.
3. **Bench** (crank simulator `firmware/esp32_s3_crank_sim`): the checks below,
   plus a scope on MF/KI and the gain lines side by side with a stock ECU on
   the same stimulus.
4. **Engine.** Monitor-only first: noise references should settle in
   `0x10…0x33` with gain index around 4, as in the OEM. Then enable retard;
   stepped extra advance at load is used only under controlled conditions.

## How to verify the IC talks back

The CC195 has no digital output. It "talks back" only through `KI` on AN15, so
each check commands a mode and reads the analog answer:

| # | Command | Expected answer | Proves |
|---|---|---|---|
| 1 | nothing after boot (MF low, no window yet): `tune_client.py <port> knock-ic` (available now) | AN15 ≈ 0.72 V, 8-bit `0x19…0x30`, nominal `0x25` (VDD/7) | IC powered, integrator reset, S&H output alive, AN15 wiring |
| 2 | null window: KSA3 = 1, MF high for T | end ≈ start + small drift (±30 mV/ms spec, OEM limit `0x15`) | integrator running, input disconnect works |
| 3 | test pulse: KTI = 1, gain `011`, MF high | integral ≥ 179 counts (≥ 3.5 V) above start, the OEM pass limit | out of Reset, KTI line, clock (the test pulse and filter need X1), band-pass, rectifier, integrator |
| 4 | test pulse at gain codes 0…3 | integral roughly doubles per code until saturation | G0…G2 wiring and order |
| 5 | sensor connected, tap the block near the sensor | integral rises with tapping | sensor input and harness |

Check 1 is valid only until the first window runs; after that, KI holds the
last integral. Checks 2–5 need the phase 2 self-test command. If check 1 fails,
look at P8.0 (must be low) and the IC supply. If check 3 fails while check 1
passes, suspect the IC is still in Reset (P3.5/P3.6 and the `KSA1/KSA2`
straps), or a missing X1 clock.

## Deviation register

| # | Deviation | Why | Effect |
|---|---|---|---|
| D1 | Window timer T7 `/16` instead of T8 `/64`/`/512` | T8 drives boost | Ticks are computed in OEM units, then scaled, so angles are identical. Edges stay ISR-driven. |
| D2 | Reference compare on T0 (CAPCOM1) instead of T7 (CAPCOM2) | Standalone T7 is the injector timer | Both count the same falling teeth, so the same tooth |
| D3 | Reference interrupt `IRQ(6,3)` instead of `(6,0)` | `(6,0)` is taken by CC19 | Only the arbitration order within level 6 |
| D4 | Load input from the standalone load model | The standalone has no OEM `F86E` | Enable and window-map load axes need re-derivation |
| D5 | ~~Boot hold 30 ms~~ | resolved: full timeline from reset matched to the Keil-measured OEM | ≤ 0.25 µs per event |
| D6 | ADC `ADCTC/ADSTC = 11` versus OEM `00` | Standalone scan timing | Slower conversion; KI is held, so the value is unaffected (verify) |
