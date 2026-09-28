# TU5JP standalone DTC event decision matrix

Generated from the exact TU5JP ROM report table and producer inventory. The ROM proves the event/subtype/report-code mapping and the listed callsites. Trigger text identifies the strongest presently supported upstream condition; where the physical monitor or threshold has not been reduced, it says so rather than inferring from a P-code name.

At the generic event manager, descriptor bit 0 asserts/clears the event and descriptor bits 8..11 select subtype `1/2/4/8`. Therefore the subtype-to-code column below is the exact public-code selection. Debounce, completion, aging and MIL policy are subsequently applied by the shared OEM lifecycle.

Mode decisions: `Both modes` means independent of oxygen-sensor selection; `Both, adapted` means the public fuel-trim condition is reproduced without copying the OEM LTFT algorithm; `Narrowband only` requires an actual narrowband sensor; pending rows are not activated; `No` is deliberately unavailable for this installation/scope.

| Event | Exact subtype → code | OEM assertion source | Standalone mode | Decision | Producer |
|---:|---|---|---|---|---|
| `0x03` | 1→P0300; 2→P0300; 4→P0300; 8→P0300 | OEM misfire bank A asserts after missing expected crank acceleration; aggregate event 03, cylinders 1/3/4/2 in 04/05/06/07 | Both modes | `implemented` | `700DC;702A4;70422;708EC` |
| `0x04` | 1→P0301; 2→P0301; 4→P0301; 8→P0301 | OEM misfire bank A asserts after missing expected crank acceleration; aggregate event 03, cylinders 1/3/4/2 in 04/05/06/07 | Both; bench gate | `implemented_bench_gate` | `70114;70316;7045C;7093A` |
| `0x05` | 1→P0303; 2→P0303; 4→P0303; 8→P0303 | OEM misfire bank A asserts after missing expected crank acceleration; aggregate event 03, cylinders 1/3/4/2 in 04/05/06/07 | Both; bench gate | `implemented_bench_gate` | `70114;70316;7045C;7093A` |
| `0x06` | 1→P0304; 2→P0304; 4→P0304; 8→P0304 | OEM misfire bank A asserts after missing expected crank acceleration; aggregate event 03, cylinders 1/3/4/2 in 04/05/06/07 | Both; bench gate | `implemented_bench_gate` | `70114;70316;7045C;7093A` |
| `0x07` | 1→P0302; 2→P0302; 4→P0302; 8→P0302 | OEM misfire bank A asserts after missing expected crank acceleration; aggregate event 03, cylinders 1/3/4/2 in 04/05/06/07 | Both; bench gate | `implemented_bench_gate` | `70114;70316;7045C;7093A` |
| `0x08` | 1→P0300; 2→P0300; 4→P0300; 8→P0300 | OEM misfire bank B asserts after missing expected crank acceleration; aggregate event 08, cylinders 1/3/4/2 in 09/0A/0B/0C | Both modes | `implemented` | `7014C;706FC` |
| `0x09` | 1→P0301; 2→P0301; 4→P0301; 8→P0301 | OEM misfire bank B asserts after missing expected crank acceleration; aggregate event 08, cylinders 1/3/4/2 in 09/0A/0B/0C | Both; bench gate | `implemented_bench_gate` | `70184;70732` |
| `0x0A` | 1→P0303; 2→P0303; 4→P0303; 8→P0303 | OEM misfire bank B asserts after missing expected crank acceleration; aggregate event 08, cylinders 1/3/4/2 in 09/0A/0B/0C | Both; bench gate | `implemented_bench_gate` | `70184;70732` |
| `0x0B` | 1→P0304; 2→P0304; 4→P0304; 8→P0304 | OEM misfire bank B asserts after missing expected crank acceleration; aggregate event 08, cylinders 1/3/4/2 in 09/0A/0B/0C | Both; bench gate | `implemented_bench_gate` | `70184;70732` |
| `0x0C` | 1→P0302; 2→P0302; 4→P0302; 8→P0302 | OEM misfire bank B asserts after missing expected crank acceleration; aggregate event 08, cylinders 1/3/4/2 in 09/0A/0B/0C | Both; bench gate | `implemented_bench_gate` | `70184;70732` |
| `0x0D` | 1→P0338; 2→P0337; 4→P0335; 8→P0336 | Crank-input electrical/range monitor selects subtype high, low, no-signal or range/performance | Both modes | `implemented` | `67948` |
| `0x0E` | 1→U1109; 2→U1109; 4→U1109; 8→U1109 | OEM communications monitor asserts for its particular missing/invalid peer-message channel; peer identity remains unresolved | No | `unavailable` | `5CD18` |
| `0x10` | 1→U1000; 2→U1000; 4→U1000; 8→U1000 | OEM communications monitor asserts for its particular missing/invalid peer-message channel; peer identity remains unresolved | No | `unavailable` | `5DF98;5DFCC` |
| `0x11` | 1→U1118; 2→U1118; 4→U1118; 8→U1118 | OEM communications monitor asserts for its particular missing/invalid peer-message channel; peer identity remains unresolved | No | `unavailable` | `5E068;5E0B2` |
| `0x12` | 1→U1109; 2→U1109; 4→U1109; 8→U1109 | OEM communications monitor asserts for its particular missing/invalid peer-message channel; peer identity remains unresolved | No | `unavailable` | `5E068;5E0B2` |
| `0x13` | 1→U1113; 2→U1113; 4→U1113; 8→U1113 | OEM communications monitor asserts for its particular missing/invalid peer-message channel; peer identity remains unresolved | No | `unavailable` | `5E068;5E0B2` |
| `0x16` | 1→U1109; 2→U1109; 4→U1109; 8→U1109 | OEM communications monitor asserts for its particular missing/invalid peer-message channel; peer identity remains unresolved | No | `unavailable` | `5E068;5E0B2` |
| `0x1A` | 1→P1327; 2→P1327; 4→P1327; 8→P1327 | DEPHIA/CC8 phase monitor asserts when coil-derived cylinder-1 phase acquisition is invalid or unavailable | Both modes | `implemented` | `67C52` |
| `0x1B` | 1→P0123; 2→P0122; 4→P0120; 8→P0121 | Throttle ADC monitor selects signal high, low, circuit fault or range/performance | Both modes | `implemented` | `4C01A` |
| `0x1D` | 1→P0533; 2→P0532; 4→P0530; 8→P0531 | A/C pressure monitor selects signal high, low, circuit fault or range/performance | No | `unavailable` | `5E770` |
| `0x20` | 1→P0201; 2→P0201; 4→P0201; 8→P0201 | OEM output-driver feedback reports injector cylinder 1 circuit fault | Both; feedback pending | `hardware_pending` | `661B0` |
| `0x21` | 1→P0203; 2→P0203; 4→P0203; 8→P0203 | OEM output-driver feedback reports injector cylinder 3 circuit fault | Both; feedback pending | `hardware_pending` | `661F4` |
| `0x22` | 1→P0204; 2→P0204; 4→P0204; 8→P0204 | OEM output-driver feedback reports injector cylinder 4 circuit fault | Both; feedback pending | `hardware_pending` | `66238` |
| `0x23` | 1→P0202; 2→P0202; 4→P0202; 8→P0202 | OEM output-driver feedback reports injector cylinder 2 circuit fault | Both; feedback pending | `hardware_pending` | `6627E` |
| `0x25` | 1→P0171; 2→P0172; 4→P0170; 8→P0170 | OEM high-load multiplicative LTFT channel ABDA/B26E reaches its lean/rich limit or reports an adaptation fault | No | `unavailable` | `59CD2;5A64E` |
| `0x26` | 1→P0171; 2→P0172; 4→P0170; 8→P0170 | OEM low-load multiplicative LTFT channel ABDE/B270 reaches its lean/rich limit or reports an adaptation fault | No | `unavailable` | `59CE0;5A65C` |
| `0x27` | 1→P0171; 2→P0172; 4→P0170; 8→P0170 | Overall lambda-regulation descriptor B272 selects lean, rich or regulation-malfunction subtype; its complete OEM upstream predicate remains to be reduced | Both, adapted | `implemented_adapted` | `5A686` |
| `0x28` | 1→P1520; 2→P1520; 4→P1520; 8→P1520 | Downstream-heater/P6.4 state machine asserts its manufacturer-specific control fault | No | `unavailable` | `4598E` |
| `0x29` | 1→P0480; 2→P0480; 4→P0480; 8→P0480 | Low-speed fan output/feedback monitor asserts | Both; feedback pending | `hardware_pending` | `5F552` |
| `0x2A` | 1→P0481; 2→P0481; 4→P0481; 8→P0481 | High-speed fan output/feedback monitor asserts | Both; feedback pending | `hardware_pending` | `5F560` |
| `0x2B` | 1→P0141; 2→P0141; 4→P0141; 8→P0141 | Downstream oxygen-heater monitor asserts (two OEM monitor paths, same public code) | No | `unavailable` | `4314C` |
| `0x2C` | 1→P0141; 2→P0141; 4→P0141; 8→P0141 | Downstream oxygen-heater monitor asserts (two OEM monitor paths, same public code) | No | `unavailable` | `6535C;653B4` |
| `0x2D` | 1→P0130; 2→P0130; 4→P0130; 8→P0130 | Upstream oxygen regulation/circuit monitor asserts; public report is P0130 for every subtype | Narrowband only | `implemented_narrowband` | `42FBE` |
| `0x2E` | 1→P0135; 2→P0135; 4→P0135; 8→P0135 | Upstream heater output/feedback monitor asserts; in wideband mode this is the Spartan controller supply/ground circuit, not its internal heater | Both; feedback pending | `hardware_pending` | `653F8;65454` |
| `0x2F` | 1→P0420; 2→P0420; 4→P0420; 8→P0420 | Catalyst comparison monitor asserts efficiency below threshold | No | `unavailable` | `4474E` |
| `0x30` | 1→P1110; 2→P1110; 4→P1110; 8→P1110 | Manufacturer P1110 producer asserts; physical monitor identity/threshold is unresolved | Pending identity | `unresolved` | `65D06` |
| `0x31` | 1→P0232; 2→P0231; 4→P0230; 8→P0230 | Fuel-pump relay feedback selects circuit high, circuit low or generic circuit fault | Both; feedback pending | `hardware_pending` | `unresolved` |
| `0x32` | 1→P1303; 2→P1303; 4→P1303; 8→P1303 | One of seven knock-subsystem diagnostic channels asserts from its per-channel status/counter (exact subchannel meaning remains unresolved) | No | `unavailable` | `4B3D0` |
| `0x33` | 1→P1303; 2→P1303; 4→P1303; 8→P1303 | One of seven knock-subsystem diagnostic channels asserts from its per-channel status/counter (exact subchannel meaning remains unresolved) | No | `unavailable` | `4B41C` |
| `0x34` | 1→P1303; 2→P1303; 4→P1303; 8→P1303 | One of seven knock-subsystem diagnostic channels asserts from its per-channel status/counter (exact subchannel meaning remains unresolved) | No | `unavailable` | `4B468` |
| `0x35` | 1→P1303; 2→P1303; 4→P1303; 8→P1303 | One of seven knock-subsystem diagnostic channels asserts from its per-channel status/counter (exact subchannel meaning remains unresolved) | No | `unavailable` | `4B4B4` |
| `0x36` | 1→P1303; 2→P1303; 4→P1303; 8→P1303 | One of seven knock-subsystem diagnostic channels asserts from its per-channel status/counter (exact subchannel meaning remains unresolved) | No | `unavailable` | `4B72C` |
| `0x37` | 1→P1303; 2→P1303; 4→P1303; 8→P1303 | One of seven knock-subsystem diagnostic channels asserts from its per-channel status/counter (exact subchannel meaning remains unresolved) | No | `unavailable` | `4B7A8` |
| `0x38` | 1→P1303; 2→P1303; 4→P1303; 8→P1303 | One of seven knock-subsystem diagnostic channels asserts from its per-channel status/counter (exact subchannel meaning remains unresolved) | No | `unavailable` | `4BA34` |
| `0x39` | 1→P0328; 2→P0327; 4→P0329; 8→P0326 | Knock-sensor monitor selects signal high, low, intermittent or range/performance | No | `unavailable` | `4B210` |
| `0x3F` | 1→P0133; 2→P0133; 4→P0133; 8→P0133 | One of two upstream narrowband switching-rate monitors asserts slow response | Narrowband only | `implemented_narrowband` | `436F6` |
| `0x40` | 1→P0133; 2→P0133; 4→P0133; 8→P0133 | One of two upstream narrowband switching-rate monitors asserts slow response | Narrowband only | `implemented_narrowband` | `437F6` |
| `0x43` | 1→P0108; 2→P0107; 4→P0109; 8→P0106 | MAP ADC monitor selects signal high, low, intermittent or range/performance | Both modes | `implemented` | `3EDFC` |
| `0x44` | 1→P0138; 2→P0137; 4→P0140; 8→P0136 | Downstream oxygen ADC monitor selects signal high, low, no activity or circuit/range fault | No | `unavailable` | `42850` |
| `0x45` | 1→P0132; 2→P0131; 4→P0134; 8→P0130 | Upstream narrowband ADC monitor selects signal high, low, no activity or circuit/range fault | Narrowband only | `implemented_narrowband` | `451A2` |
| `0x47` | 1→P0650; 2→P0650; 4→P0650; 8→P0650 | MIL output-driver feedback disagrees with the commanded lamp state | Both; feedback pending | `hardware_pending` | `65500` |
| `0x48` | 1→P0485; 2→P0485; 4→P0485; 8→P0485 | One of two fan power/ground feedback paths asserts | Both; feedback pending | `hardware_pending` | `655F4` |
| `0x49` | 1→P0485; 2→P0485; 4→P0485; 8→P0485 | One of two fan power/ground feedback paths asserts | Both; feedback pending | `hardware_pending` | `65666` |
| `0x4A` | 1→P0338; 2→P0337; 4→P0335; 8→P0336 | Crank-input electrical/range monitor selects subtype high, low, no-signal or range/performance | Both modes | `implemented` | `67786` |
| `0x4B` | 1→P1327; 2→P1327; 4→P1327; 8→P1327 | DEPHIA/CC8 phase monitor asserts when coil-derived cylinder-1 phase acquisition is invalid or unavailable | Both modes | `implemented` | `67C44` |
| `0x4D` | 1→P1528; 2→P1527; 4→P1529; 8→P1526 | Idle-stepper position/supervisor monitor selects circuit high, circuit low, range/performance or generic circuit subtype | Both modes | `implemented` | `69BFA` |
| `0x4E` | 1→P0171; 2→P0172; 4→P0170; 8→P0170 | OEM speed-scaled additive LTFT channel ABE2/B2C0 reaches its lean/rich limit or reports an adaptation fault | No | `unavailable` | `59CEE;5A66A` |
| `0x4F` | 1→P0171; 2→P0172; 4→P0170; 8→P0170 | OEM fixed additive LTFT channel ABE6/B2C2 reaches its lean/rich limit or reports an adaptation fault | No | `unavailable` | `59CFC;5A678` |
| `0x50` | 1→P0606; 2→P0606; 4→P0606; 8→P0606 | OEM processor self-test asserts | Both modes | `implemented` | `631DE` |
| `0x51` | 1→P0350; 2→P0350; 4→P0350; 8→P0350 | Generic ignition-coil primary/output feedback monitor asserts | Both; feedback pending | `hardware_pending` | `37700;37B20;37C0A` |
| `0x53` | 1→P0410; 2→P0410; 4→P0410; 8→P0410 | Secondary-air functional monitor asserts insufficient/invalid air injection | No | `unavailable` | `2F582` |
| `0x56` | 1→P1525; 2→P1525; 4→P1524; 8→P1523 | Idle-stepper/L9935 driver supervisor selects one of its manufacturer-specific actuator fault subtypes | Both modes | `implemented` | `65784;65C86` |
| `0x57` | 1→P0351; 2→P0351; 4→P0351; 8→P0351 | Ignition coil A/output feedback monitor asserts | Both; feedback pending | `hardware_pending` | `37720;37988;37A58;37B8A` |
| `0x58` | 1→P0352; 2→P0352; 4→P0352; 8→P0352 | Ignition coil B/output feedback monitor asserts | Both; feedback pending | `hardware_pending` | `37740;379BC;37A8C;37BAA` |
| `0x5B` | 1→P0113; 2→P0112; 4→P0110; 8→P0111 | IAT ADC monitor selects signal high, low, circuit fault or range/performance | Both modes | `implemented` | `69966` |
| `0x60` | 1→P0445; 2→P0444; 4→P0443; 8→P0443 | Purge-valve output feedback selects circuit high, low or generic circuit fault | No | `unavailable` | `656FC` |
| `0x61` | 1→P0118; 2→P0117; 4→P0115; 8→P0116 | Coolant ADC monitor selects signal high, low, circuit fault or range/performance | Both modes | `implemented` | `696C6` |
| `0x62` | 1→P1608; 2→P1608; 4→P1608; 8→P1608 | Manufacturer P1608 producer asserts; internal-state identity is unresolved | Pending identity | `unresolved` | `66398` |
| `0x65` | 1→P0563; 2→P0562; 4→P0560; 8→P0561 | Supply-voltage monitor selects high, low, circuit fault or range/performance | Both modes | `implemented` | `66704` |
| `0x66` | 1→P0605; 2→P0605; 4→P0605; 8→P0605 | Program-memory/ROM integrity self-test asserts | Both; manifest pending | `manifest_pending` | `63480` |
| `0x67` | 1→P1613; 2→P1613; 4→P1613; 8→P1613 | OEM immobilizer/configuration monitor asserts P1613 | No | `unavailable` | `64608;6466E` |
| `0x68` | 1→P0503; 2→P0502; 4→P0500; 8→P0501 | Vehicle-speed monitor selects implausibly high, low/no pulses, circuit fault or range/performance | Both modes | `implemented` | `2A28A` |
| `0x69` | 1→P1615; 2→P1615; 4→P1615; 8→P1615 | OEM immobilizer/configuration monitor asserts P1615 | No | `unavailable` | `31864` |

## Important trigger boundaries

- The table is exact about which descriptor subtype reports which code. It does not call an unrecovered voltage, time, load or counter threshold exact merely because the public code name suggests one.
- Events `0x03..0x0C` are two independent OEM misfire banks. Their public mapping and producers are exact; the standalone uses two calibrated crank-window severity levels without claiming the unrecovered OEM bank assignment.
- Events `0x1A/0x4B` are phase-monitor events, not knock DTCs. The standalone now arms CC8 and captures the coil-derived DEPHIA timing. Paired injection remains unchanged.
- Events `0x32..0x38` (P1303) are produced inside the knock subsystem from per-channel knock diagnostic bits/counters, and `0x39` is the P032x knock-sensor event. All eight are disabled by owner scope. P1327 events are phase-system events in this image context and remain in scope.
- Events `0x2D`, `0x3F`, `0x40` and `0x45` are implemented only in narrowband mode. Event `0x2E` P0135 is meaningful in both modes for the electrically supervised OEM heater circuit: in wideband mode that circuit powers the Spartan controller. It remains inactive until the P6 driver-feedback transport is ported.
- Event `0x27` can report P0170/P0171/P0172 in either mode using sustained correction-limit/error qualification. Events `0x25/0x26/0x4E/0x4F` are the four OEM LTFT-cell monitors and are unavailable because the standalone intentionally has no LTFT.
- Purge, secondary air, downstream oxygen/catalyst, A/C pressure, OEM network and immobilizer events remain unavailable for the stated installation.

## Implemented standalone trigger contract

These are standalone calibrated equivalents, not claims that unrecovered OEM thresholds are identical:

| Events | Standalone failure trigger | Qualification / limitation |
|---|---|---|
| `03/08` P0300 | Middle 180-degree interval exceeds the mean of its two neighbors by `0x956` percent; bank B requires twice that deviation | running, valid sync, 500..7000 rpm, no hard/soft cut or DFCO and steady TPS; at least `0x957` hits per 128 qualified observations; stale/overflow/epoch changes reset the window |
| `04..07/09..0C` P0301/3/4/2 | same crank-window trigger, labelled in firing order 1-3-4-2 from valid DEPHIA identity | implemented but release-gated until `0x962` polarity is established on the board |
| `0D/4A` P0335/P0336 | loss of an acquired 60-2 pattern / incremented rotation-loss count | capture cannot distinguish P0337 from P0338, so those subtypes are never fabricated |
| `1A/4B` P1327 | armed DEPHIA capture missing / captured delay outside 66..168 ticks or no qualified >=18-tick direction change | thresholds are tune fields `0x958/0x95C..0x960` |
| `1B/43/5B/61` TPS/MAP/IAT/CLT | conditioned quality invalid; raw ADC half-scale selects high versus low; valid signal slew above `0x964/0x966/0x968` selects range/performance | three monitor calls by default |
| `27` P0171/P0172 | STFT remains at the configured positive/negative correction limit for `0x950` ms while trim is enabled | adapted overall regulation monitor; no OEM LTFT-cell emulation |
| `2D/45` P0130/P013x | invalid upstream narrowband quality or no threshold crossing during the `0x952` activity window | narrowband mode only |
| `3F/40` P0133 | lean-to-rich / rich-to-lean crossing takes longer than `0x954` ms while closed-loop trim is active | narrowband mode only |
| `4D/56` P152x | standalone IAC homing/position/deadline fault / decoded L9935 electrical response fault | real SSC response, not commanded state |
| `50` P0606 | volatile RAM patterns, arithmetic identities and control-flow branches fail | processor subset; not full program ROM |
| `65` P056x | conditioned supply-voltage quality invalid; ADC half-scale selects high/low; running voltage slew above `0x96A` selects performance | three monitor calls by default |
| `68` P050x | speed above `0x95B`, loss after prior motion, or >80 km/h sample jump | stationary and missing pulses are deliberately `unknown` |

Every row is first gated by the 107-event bitmap at `0x940`. The 107-byte table at `0x980` then gates descriptor subtypes 1/2/4/8 individually. Disabled live selections are recovered through the OEM-derived lifecycle. Wideband mode suspends `2D/3F/40/45`; their enable bits do not make a linear signal emulate narrowband switching.

Events `20..23`, `29/2A`, `2E`, `31`, `47..49`, `51/57/58` remain hardware candidates but are not activated: their honest predicate is the P6.5/P6.6/P6.7 driver diagnostic protocol, which is not safely replaceable by the output command. This includes P0135 in wideband mode: the monitored load is the Spartan controller supply across the OEM heater 12-V/ground conductors, but the electrical feedback transport still has to be ported. Event `66` P0605 is also inactive until the linked image contains a post-link checksum manifest; checking a few constants would not be a ROM-integrity monitor.