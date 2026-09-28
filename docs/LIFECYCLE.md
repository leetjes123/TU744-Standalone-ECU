# Lifecycle checkpoint, 21 September 2026

Implemented: boot/history/shutdown ownership, analog-only wideband qualification,
explicit launch arming, soft launch, bounded anti-lag and gear estimation.
**The full requested OEM diagnostic rewrite is not complete.** The Wizard and
physical commissioning remain unchanged/deferred. Engine outputs remain inhibited.

Later scheduler follow-up: that inhibit statement describes the default build.
The explicitly selected [experimental ECU profile](EXPERIMENTAL.md) now permits
outputs; hardware/engine acceptance and the lifecycle limitations below remain open.

## Diagnostic owner

`oem_runtime.c` instantiates native state and the history owner. Before interrupts,
boot loads history, selects retained/lost-history cycle initialization and marks
initialization changes dirty. Unreadable EEPROM blocks replacement with empty
history. Foreground polls storage; shutdown stops native releases before saving
an immutable snapshot. Calibration and history retain exclusive journal access.
Failed saves remain visible and block release; retries are not automatic.

Six native binding obligations remain: configuration, rotation, context,
producer gates, MIL inputs and speed acquisition. `bindings` starts at zero,
has no tuning command, and must not be set until those paths are implemented.
Raw ADC publication does not satisfy them. The guarded task projection preserves
the relative order of currently ported calls and rejects a missed whole adaptive
release, but is not the full native scheduler/producer set. All event-support
and exact-parity flags remain false. There is no OEM clear-tool success response.

Command27 exposes lifecycle status; command29 reads a native24-byte stored row.
A **standalone fallback** preserves retained steady warning demand while run
permission is asserted. It is not OEM lamp prove-out/healing/trigger parity.
Flashing requests remain data only; no LTFT or fabricated equipment inputs exist.

## Run permission and shutdown

The board reads P4.4, the ROM-supported run-permission input, and explicitly
configures it as input. Permission starts false and requires three equal samples
at least10ms apart. This10ms standalone filter is not native adaptive-task timing.
Unavailable acquisition revokes permission. Deassertion cancels injection and
future coil charges, drains an existing charge to its retained fire deadline,
stops pump/heaters/boost, disarms launch, requests IAC bridge-off and stops native
diagnostic releases. Saving waits for stopped rotation, bridge-off completion and
no competing/staged calibration transaction. Release waits for durable history,
at least15seconds, valid coolant and the end of fan demand. RAM tune edits still
require an explicit save; shutdown never commits them implicitly.

Reassertion during shutdown enters `POWER_RESTART_REQUIRED`. After active outputs,
IAC bridge-off and already-started storage writes drain, the board executes `SRST`
even while rotation continues. It does not start another history save or wait for
the engine to stop. This matches the native reassertion instruction; ordinary
key-off still waits for durable history before any latch release. The fake HAL
and full-foreground linked test record a single reset request. This is
software evidence; actual hardware reset acceptance remains open.
Raw reset provenance is now captured as described below. It cannot resume half-shutdown state.
**Physical latch release remains blocked:** `hal_power_release()` returns false.
ROM68C40 raises P3.12, but its connected load/board variant is unresolved in
`q.mode_input_board_identity`. No new P3.12 write is made.

Supplied evidence: Citroen1.3.277 PDFp13 (technicalp7) describes power retention,
its15second minimum, cooling and saving fault memory. Separate TU5JP ROM evidence
in `alg.scheduler_mode_input` establishes P4.4 ->FD08.13, alternate-mode selection,
reassertion/reset and the terminal P3.12 write. Neither proves connector identity.

## Reset observation

`START167.A66` captures the entire WDTCON word before its first overwrite or
watchdog service. R15 holds it in the linked internal register bank while C
zero/copy initialization runs. A NOP separates the CP write from GPR use.
After initialization, startup publishes the word and a validity marker to
separate globals that `board_init`, `ecu_init` and `lifecycle_init` preserve.
Every reset-vector entry replaces the observation; this is not a retained log.
There is no added EEPROM write or change to diagnostic-history restore policy.

Command `2A` and the reference client's `reset` action expose availability,
raw WDTCON and the WDTR indication. Unavailable capture is distinct from a
valid zero. Extended flags are preserved without decoding them as power-on,
software reset, brownout or trap cause. A watchdog indication does not identify
which fault stopped service. Detailed decoding awaits verified silicon identity
and reset wiring in `q.standalone_reset_identity`.

Manufacturer evidence, separate from TU5JP ROM evidence: Infineon's
[C167CR/SR HA errata v1.1, p24](https://www.infineon.com/dgdl/C167xR_HA_11.pdf?fileId=db3a304412b407950112b41d86f9304c)
documents WDTR at bit1, its clearing by SRVWDT, stepping-dependent additional
flags, and effects of bidirectional reset. The
[C167 manual, section 4, p4-22](https://community.infineon.com/gfawx74859/attachments/gfawx74859/twlegacymcu/1131/1/2594108.pdf)
requires an intervening instruction after updating CP before using the new GPR
context. Supplied Citroen PDF p13 describes power retention and saving memory;
it does not specify these CPU reset flags or establish this board's silicon step.

`test_reset_target.py` executes the linked reset vector through all C startup
clear/copy loops to main, then the board/application initializers. Register
observations and RAM patterns are test inputs. It checks the CP barrier in the
linked bytes because the emulator does not model that pipeline hazard. These
checks do not establish physical watchdog timing, reset-source electrical
behavior, instruction prefetch timing or a complete running ECU boot.

## Analog-only wideband

The owner confirmed the only diagnostic signal is the 0–5 V analog output;
there is no ready/fault wire. The controller itself is powered across the OEM
upstream-heater 12 V and ground conductors.
Policy0 is disabled; policy1 qualifies fresh in-range raw samples after a calibrated
warm-up interval and continuous qualification time. Raw rails cannot be hidden
by filtering. Stale input, missed service, key-off, service mode or calibration
change revoke/restart qualification. Existing STFT gates and limits still apply.
The status is `WB_ANALOG_QUALIFIED`, not controller health: an in-range failed
output is indistinguishable using this wire alone. `wideband_input` cannot bypass
the policy. All new bytes default zero; no existing car tune is changed.
Model, transfer, startup/current behavior and intervals still need installation qualification.
No Spartan2/3 startup sequence is assumed. See WIDEBAND.md for manufacturer sources.

## Launch, anti-lag and gear

Arming requires closed throttle. Moving mode requires valid low speed. Stationary
mode is an explicit operator assertion, accepted only with zero measured speed
and no recent pulse; the next pulse consumes the arm. It does not prove a working
VSS. Arms expire within1–30seconds and are revoked by invalid input, inhibits,
service, key-off or calibration generation change.

Bit3 at0x7B4 selects soft launch with an independent start RPM/cut fraction and
the existing hard backstop. Anti-lag shares that launch decision for VE, timing,
lambda exclusion and cuts. A100–3000ms one-shot window per arm, coolant/IAT gates
and main-limit/overboost precedence bound it. Bit7 converts only launch cuts to
ignition-only; other protections remain.0x7B8 uses raw*0.5-20degrees;0x7B9 uses
this rewrite's VE convention. Bounds are implementation limits, not measured
exhaust-temperature protection or acceptance of the old tune.

Gear uses legacy x100 gearbox/final-drive ratios and tyre circumference in mm.
It requires valid speed, a match within500RPM and more than100RPM separation
from the next candidate; unknown/slip/ambiguous results are zero. Supplied PDFp44
(technicalp38) describes RPM plus speed for gear information. This estimator's
confidence limits are standalone choices, not an OEM algorithm port.

## Still required

- Remaining applicable OEM producers, complete native inputs/PEC/cadence/phase
  ownership and the remaining33 clear callbacks (nine currently ported).
- Full native MIL lifecycle, tool clears/after-sales history and complete runtime
  equivalence; the current retained-warning fallback is separate.
- Verified physical latch binding, silicon-specific reset decoding and a
  recoverable firmware-update boot/recovery design.
- Installation calibration and physical/engine acceptance, separately from the
  software checks recorded in VERIFICATION.md.
