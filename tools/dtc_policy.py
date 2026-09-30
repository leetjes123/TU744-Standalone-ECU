"""Standalone applicability policy for TU5JP OEM diagnostic events.

The states separate running producers, hardware/post-link gates, unresolved
monitors and equipment known to be absent. Implementation is not OEM parity.
"""
from __future__ import annotations

from dataclasses import dataclass

SUBTYPES = (1, 2, 4, 8)


@dataclass(frozen=True)
class StandaloneDtcPolicy:
    status: str
    allowed_subtypes: tuple[int, ...]
    reason: str
    evidence: str


def _policy(status: str, reason: str, evidence: str,
            subtypes: tuple[int, ...] = SUBTYPES) -> StandaloneDtcPolicy:
    return StandaloneDtcPolicy(status, subtypes, reason, evidence)


AVAILABLE = {
    # Running standalone producers; this is not a claim of OEM threshold parity.
    0x1B: _policy("implemented", "standalone monitors the throttle potentiometer on AN8",
                  "src/sensors.c sample(8); OEM event/report table"),
    0x43: _policy("implemented", "standalone monitors manifold pressure on AN0",
                  "src/sensors.c sample(0); OEM event/report table"),
    0x5B: _policy("implemented", "standalone and native frontend monitor intake-air temperature",
                  "src/sensors.c sample(11); ported OEM IAT producer"),
    0x61: _policy("implemented", "standalone and native frontend monitor coolant temperature",
                  "src/sensors.c sample(10); ported OEM coolant producer"),
    0x65: _policy("implemented", "standalone and native frontend monitor supply voltage",
                  "src/sensors.c sample(5); ported OEM voltage producer"),
    0x68: _policy("implemented", "standalone monitors its timestamped vehicle-speed capture input",
                  "target/c167/board.c vss_isr; ported OEM VSS input/producer"),
}


GROUPS = {
    "misfire_aggregate_implemented": {
        "status": "implemented", "events": {0x03, 0x08},
        "reason": "standalone counts relative 180-degree crank-interval imbalance over 128 qualified observations; engine validation remains required",
        "evidence": "rotation.c edge history; OEM producers 0x700DC..0x7093A; Citroen training PDF pp.51-52",
    },
    "misfire_cylinder_bench_gate": {
        "status": "implemented_bench_gate",
        "events": {0x04, 0x05, 0x06, 0x07, 0x09, 0x0A, 0x0B, 0x0C},
        "reason": "phase-attributed crank-window monitor is implemented; DEPHIA polarity requires board/cylinder acceptance",
        "evidence": "diagnostic_monitors.c firing-order attribution; Citroen training PDF pp.30-31 and pp.51-52",
    },
    "rotation_phase_implemented": {
        "status": "implemented", "events": {0x0D, 0x1A, 0x4A, 0x4B},
        "reason": "standalone monitors 60-2 sync/loss and coil-derived DEPHIA capture timing",
        "evidence": "CC8 phase capture 0x67C5A..0x67E30; P1327 producers 0x67C44/0x67C52; Citroen PDF pp.30-32",
    },
    "output_diagnostic_bus_candidate": {
        "status": "hardware_pending",
        "events": {0x20, 0x21, 0x22, 0x23, 0x29, 0x2A, 0x31, 0x47,
                   0x48, 0x49, 0x51, 0x57, 0x58, 0x2E},
        "reason": "the OEM output-driver feedback interface is present but unused by the standalone",
        "evidence": "OEM sub_6489C clocks P6.5 and samples P6.7; P6.6 is companion control; standalone board_init only configures P6.2/P6.4",
    },
    "iac_diagnostic_candidate": {
        "status": "implemented", "events": {0x4D, 0x56},
        "reason": "these manufacturer P152x events belong to the idle stepper/driver supervisor, and the standalone already captures L9935 response status",
        "evidence": "OEM producers 0x6574C/0x65C76/0x69B68; standalone src/iac.c response/open/short/thermal accounting",
    },
    "fuel_trim_adaptation_candidate": {
        "status": "implemented_adapted", "events": {0x27},
        "reason": "the overall rich/lean regulation condition is meaningful in either oxygen mode, using a qualified sustained STFT-limit/error monitor",
        "evidence": "event 0x27 uses the separate B272 descriptor; src/lambda.c supplies mode-specific closed-loop correction",
    },
    "narrowband_only_candidate": {
        "status": "implemented_narrowband", "events": {0x2D, 0x3F, 0x40, 0x45},
        "reason": "the OEM upstream switching-signal monitor is valid only when narrowband mode is selected",
        "evidence": "AN6 acquisition; OEM producers at 0x42FBE/0x436F6/0x437F6/0x451A2",
    },
    "processor_self_test_implemented": {
        "status": "implemented", "events": {0x50},
        "reason": "standalone tests volatile RAM patterns, arithmetic identities and control flow",
        "evidence": "OEM P0606/P0605 producers at 0x631DE/0x63480; standalone lifecycle differs",
    },
    "program_memory_manifest_pending": {
        "status": "manifest_pending", "events": {0x66},
        "reason": "a truthful whole-program test requires an excluded-range checksum inserted after link",
        "evidence": "no post-link checksum manifest exists; calibration CRC or selected constants are not program-memory integrity",
    },
    "identity_unresolved": {
        "status": "unresolved",
        "events": {0x30, 0x62},
        "reason": "the P-code mapping is known but the vehicle function and required observable are not proven well enough to accept or reject",
        "evidence": "OEM report mapping and producer callsite only; monitor contract investigation pending",
    },
    "network_unavailable": {
        "status": "unavailable", "events": {0x0E, 0x10, 0x11, 0x12, 0x13, 0x16},
        "reason": "the standalone has no OEM BSI/gearbox/ESP communication state and disables both on-chip CAN modules",
        "evidence": "target/c167/START167.A66 CAN1DIS=CAN2DIS=1; standalone exposes K-line only",
    },
    "ac_pressure_unavailable": {
        "status": "unavailable", "events": {0x1D},
        "reason": "no air-conditioning pressure signal is connected to the standalone",
        "evidence": "no standalone sensor or board binding for OEM A/C pressure information",
    },
    "downstream_oxygen_unavailable": {
        "status": "unavailable",
        "events": {0x28, 0x2B, 0x2C, 0x2F, 0x44},
        "reason": "the downstream oxygen sensor/heater and catalyst-monitoring observable are not fitted",
        "evidence": "no bound downstream sensor; event 0x28 is proved downstream-heater related",
    },
    "ltft_channels_unavailable": {
        "status": "unavailable", "events": {0x25, 0x26, 0x4E, 0x4F},
        "reason": "these four events belong to OEM LTFT cells and the standalone intentionally has no LTFT",
        "evidence": "OEM B26E=ABDA high-load multiplicative, B270=ABDE low-load multiplicative, B2C0=ABE2 speed-additive, B2C2=ABE6 fixed-additive",
    },
    "knock_dtc_disabled_by_scope": {
        "status": "unavailable", "events": set(range(0x32, 0x3A)),
        "reason": "knock DTCs are explicitly outside the requested standalone diagnostic scope",
        "evidence": "owner direction 2026-09-22; ROM 0x4B374 onward derives events 0x32..0x38 from knock diagnostic bits/counters and event 0x39 from knock input state",
    },
    "secondary_air_unavailable": {
        "status": "unavailable", "events": {0x53},
        "reason": "secondary-air equipment is not installed",
        "evidence": "owner hardware scope; Citroen training PDF pp.51-52 marks equipment variants",
    },
    "purge_unavailable": {
        "status": "unavailable", "events": {0x60},
        "reason": "purge-canister equipment is not installed",
        "evidence": "owner hardware scope; Citroen training PDF pp.51-52 marks equipment variants",
    },
    "oem_identity_unavailable": {
        "status": "unavailable", "events": {0x67, 0x69},
        "reason": "the standalone does not reproduce the OEM immobilizer/configuration identity contract",
        "evidence": "standalone reconstruction scope replaces OEM immobilizer behavior",
    },
}


DECISIONS: dict[int, StandaloneDtcPolicy] = dict(AVAILABLE)
for group in GROUPS.values():
    for event in group["events"]:
        if event in DECISIONS:
            raise ValueError(f"duplicate policy event 0x{event:02X}")
        subtypes = () if group["status"] == "unavailable" else SUBTYPES
        DECISIONS[event] = _policy(group["status"], group["reason"],
                                   group["evidence"], subtypes)


def policy_for(event: int, reportable: bool) -> StandaloneDtcPolicy:
    if not reportable:
        return _policy("not_reportable", "event has no public report word",
                       "OEM report table", ())
    if event in DECISIONS:
        return DECISIONS[event]
    raise KeyError(f"reportable event 0x{event:02X} has no standalone applicability decision")
