"""Generate the complete TU5JP reportable-event applicability matrix."""
from __future__ import annotations

import csv
from pathlib import Path

from dtc_policy import policy_for

ROOT = Path(__file__).resolve().parents[1]
from oem_repo import oem_repo
REPO = oem_repo()


TRIGGERS: dict[int, str] = {}


def group(events, text):
    for event in events:
        if event in TRIGGERS:
            raise ValueError(f"duplicate trigger 0x{event:02X}")
        TRIGGERS[event] = text


group(range(0x03, 0x08), "OEM misfire bank A asserts after missing expected crank acceleration; aggregate event 03, cylinders 1/3/4/2 in 04/05/06/07")
group(range(0x08, 0x0D), "OEM misfire bank B asserts after missing expected crank acceleration; aggregate event 08, cylinders 1/3/4/2 in 09/0A/0B/0C")
group((0x0D, 0x4A), "Crank-input electrical/range monitor selects subtype high, low, no-signal or range/performance")
group((0x0E, 0x10, 0x11, 0x12, 0x13, 0x16), "OEM communications monitor asserts for its particular missing/invalid peer-message channel; peer identity remains unresolved")
group((0x1A, 0x4B), "DEPHIA/CC8 phase monitor asserts when coil-derived cylinder-1 phase acquisition is invalid or unavailable")
group((0x1B,), "Throttle ADC monitor selects signal high, low, circuit fault or range/performance")
group((0x1D,), "A/C pressure monitor selects signal high, low, circuit fault or range/performance")
group((0x20,), "OEM output-driver feedback reports injector cylinder 1 circuit fault")
group((0x21,), "OEM output-driver feedback reports injector cylinder 3 circuit fault")
group((0x22,), "OEM output-driver feedback reports injector cylinder 4 circuit fault")
group((0x23,), "OEM output-driver feedback reports injector cylinder 2 circuit fault")
group((0x25,), "OEM high-load multiplicative LTFT channel ABDA/B26E reaches its lean/rich limit or reports an adaptation fault")
group((0x26,), "OEM low-load multiplicative LTFT channel ABDE/B270 reaches its lean/rich limit or reports an adaptation fault")
group((0x27,), "Overall lambda-regulation descriptor B272 selects lean, rich or regulation-malfunction subtype; its complete OEM upstream predicate remains to be reduced")
group((0x4E,), "OEM speed-scaled additive LTFT channel ABE2/B2C0 reaches its lean/rich limit or reports an adaptation fault")
group((0x4F,), "OEM fixed additive LTFT channel ABE6/B2C2 reaches its lean/rich limit or reports an adaptation fault")
group((0x28,), "Downstream-heater/P6.4 state machine asserts its manufacturer-specific control fault")
group((0x29,), "Low-speed fan output/feedback monitor asserts")
group((0x2A,), "High-speed fan output/feedback monitor asserts")
group((0x2B, 0x2C), "Downstream oxygen-heater monitor asserts (two OEM monitor paths, same public code)")
group((0x2D,), "Upstream oxygen regulation/circuit monitor asserts; public report is P0130 for every subtype")
group((0x2E,), "Upstream heater output/feedback monitor asserts; in wideband mode this is the Spartan controller supply/ground circuit, not its internal heater")
group((0x2F,), "Catalyst comparison monitor asserts efficiency below threshold")
group((0x30,), "Manufacturer P1110 producer asserts; physical monitor identity/threshold is unresolved")
group((0x31,), "Fuel-pump relay feedback selects circuit high, circuit low or generic circuit fault")
group(range(0x32, 0x39), "One of seven knock-subsystem diagnostic channels asserts from its per-channel status/counter (exact subchannel meaning remains unresolved)")
group((0x39,), "Knock-sensor monitor selects signal high, low, intermittent or range/performance")
group((0x3F, 0x40), "One of two upstream narrowband switching-rate monitors asserts slow response")
group((0x43,), "MAP ADC monitor selects signal high, low, intermittent or range/performance")
group((0x44,), "Downstream oxygen ADC monitor selects signal high, low, no activity or circuit/range fault")
group((0x45,), "Upstream narrowband ADC monitor selects signal high, low, no activity or circuit/range fault")
group((0x47,), "MIL output-driver feedback disagrees with the commanded lamp state")
group((0x48, 0x49), "One of two fan power/ground feedback paths asserts")
group((0x4D,), "Idle-stepper position/supervisor monitor selects circuit high, circuit low, range/performance or generic circuit subtype")
group((0x50,), "OEM processor self-test asserts")
group((0x51,), "Generic ignition-coil primary/output feedback monitor asserts")
group((0x53,), "Secondary-air functional monitor asserts insufficient/invalid air injection")
group((0x56,), "Idle-stepper/L9935 driver supervisor selects one of its manufacturer-specific actuator fault subtypes")
group((0x57,), "Ignition coil A/output feedback monitor asserts")
group((0x58,), "Ignition coil B/output feedback monitor asserts")
group((0x5B,), "IAT ADC monitor selects signal high, low, circuit fault or range/performance")
group((0x60,), "Purge-valve output feedback selects circuit high, low or generic circuit fault")
group((0x61,), "Coolant ADC monitor selects signal high, low, circuit fault or range/performance")
group((0x62,), "Manufacturer P1608 producer asserts; internal-state identity is unresolved")
group((0x65,), "Supply-voltage monitor selects high, low, circuit fault or range/performance")
group((0x66,), "Program-memory/ROM integrity self-test asserts")
group((0x67,), "OEM immobilizer/configuration monitor asserts P1613")
group((0x68,), "Vehicle-speed monitor selects implausibly high, low/no pulses, circuit fault or range/performance")
group((0x69,), "OEM immobilizer/configuration monitor asserts P1615")


MODE = {
    "available": "Both modes",
    "implemented": "Both modes",
    "implemented_bench_gate": "Both; bench gate",
    "implemented_adapted": "Both, adapted",
    "implemented_narrowband": "Narrowband only",
    "hardware_pending": "Both; feedback pending",
    "manifest_pending": "Both; manifest pending",
    "port_candidate": "Both modes",
    "adaptation_candidate": "Both, adapted",
    "narrowband_candidate": "Narrowband only",
    "unresolved": "Pending identity",
    "unavailable": "No",
}


def main():
    rows = list(csv.DictReader((REPO / "engines/TU5JP/defs/dtc_events.csv").open()))
    reportable = []
    for row in rows:
        event = int(row["event"], 16)
        mappings = []
        for subtype in (1, 2, 4, 8):
            code = row[f"subtype_{subtype}_code"]
            if code:
                mappings.append(f"{subtype}→{code}")
        if mappings:
            reportable.append((event, row, mappings))
    missing = {event for event, _, _ in reportable} - TRIGGERS.keys()
    extra = TRIGGERS.keys() - {event for event, _, _ in reportable}
    if missing or extra:
        raise ValueError(f"trigger coverage missing={missing}, extra={extra}")

    out = [
        "# TU5JP standalone DTC event decision matrix", "",
        "Generated from the exact TU5JP ROM report table and producer inventory. The ROM proves the event/subtype/report-code mapping and the listed callsites. Trigger text identifies the strongest presently supported upstream condition; where the physical monitor or threshold has not been reduced, it says so rather than inferring from a P-code name.", "",
        "At the generic event manager, descriptor bit 0 asserts/clears the event and descriptor bits 8..11 select subtype `1/2/4/8`. Therefore the subtype-to-code column below is the exact public-code selection. Debounce, completion, aging and MIL policy are subsequently applied by the shared OEM lifecycle.", "",
        "Mode decisions: `Both modes` means independent of oxygen-sensor selection; `Both, adapted` means the public fuel-trim condition is reproduced without copying the OEM LTFT algorithm; `Narrowband only` requires an actual narrowband sensor; pending rows are not activated; `No` is deliberately unavailable for this installation/scope.", "",
        "| Event | Exact subtype → code | OEM assertion source | Standalone mode | Decision | Producer |", "|---:|---|---|---|---|---|",
    ]
    for event, row, mappings in reportable:
        decision = policy_for(event, True)
        calls = row["direct_callsites"] or row["dynamic_callsites"] or "unresolved"
        trigger = TRIGGERS[event].replace("|", "\\|")
        out.append(f"| `0x{event:02X}` | {'; '.join(mappings)} | {trigger} | {MODE[decision.status]} | `{decision.status}` | `{calls}` |")
    out += ["", "## Important trigger boundaries", "",
            "- The table is exact about which descriptor subtype reports which code. It does not call an unrecovered voltage, time, load or counter threshold exact merely because the public code name suggests one.",
            "- Events `0x03..0x0C` are two independent OEM misfire banks. Their public mapping and producers are exact; the standalone uses two calibrated crank-window severity levels without claiming the unrecovered OEM bank assignment.",
            "- Events `0x1A/0x4B` are phase-monitor events, not knock DTCs. The standalone now arms CC8 and captures the coil-derived DEPHIA timing. Paired injection remains unchanged.",
            "- Events `0x32..0x38` (P1303) are produced inside the knock subsystem from per-channel knock diagnostic bits/counters, and `0x39` is the P032x knock-sensor event. All eight are disabled by owner scope. P1327 events are phase-system events in this image context and remain in scope.",
            "- Events `0x2D`, `0x3F`, `0x40` and `0x45` are implemented only in narrowband mode. Event `0x2E` P0135 is meaningful in both modes for the electrically supervised OEM heater circuit: in wideband mode that circuit powers the Spartan controller. It remains inactive until the P6 driver-feedback transport is ported.",
            "- Event `0x27` can report P0170/P0171/P0172 in either mode using sustained correction-limit/error qualification. Events `0x25/0x26/0x4E/0x4F` are the four OEM LTFT-cell monitors and are unavailable because the standalone intentionally has no LTFT.",
            "- Purge, secondary air, downstream oxygen/catalyst, A/C pressure, OEM network and immobilizer events remain unavailable for the stated installation.", ""]
    out += """## Implemented standalone trigger contract

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
""".strip().splitlines()
    (ROOT / "docs/OEM-DTC-EVENT-MATRIX.md").write_text("\n".join(out), encoding="utf-8")
    print(f"Generated {len(reportable)} reportable event decisions")


if __name__ == "__main__":
    main()
