# Output-enabled experimental ECU build

For a stock 1 KiB M95080, use the separate [stock-95080 profile](STOCK-95080.md).
The `engine-experimental` profile below retains the 8 KiB EEPROM layout.

The `engine-experimental` profile compiles the standalone with
`BOARD_RELEASED=1` in every C translation unit. It permits engine outputs when
the existing calibration, run-permission, synchronization, deadline and
service-state checks also permit them. It does not bypass those checks or
automatically load an example tune. This profile is an ECU-test artifact;
hardware and engine acceptance have not been completed.

From the repository root:

```powershell
python firmware/tu5jp_standalone/tools/build.py engine-experimental
python firmware/tu5jp_standalone/tests/test_target.py --engine-experimental
python firmware/tu5jp_standalone/tests/test_engine_target.py --engine-experimental
python firmware/tu5jp_standalone/tests/test_reset_target.py --engine-experimental
```

Artifacts are in `build/engine-experimental/`:

- `TU5JP_ENGINE_EXPERIMENTAL.bin`: 512 KiB standalone flash image, unused bytes
  filled with `FF`. This is not a patch to an OEM image.
- `TU5JP.H86`: the same linked program in Intel HEX format.
- `TU5JP.m66`, `build.log`: placement and toolchain diagnostics.
- `manifest.json`: output permission, binary/source hashes and uncompleted
  hardware/engine validation, with calibration explicitly excluded.

The ordinary `keil` and uVision profiles still inhibit the engine outputs.
The legacy `BOARD_RELEASED` name represents output permission in this profile;
it is not evidence of physical release approval or complete OEM parity.
No build or test command programs the ECU or EEPROM.

For output admission the ECU must have a valid schema-4 calibration in its
calibration journal, filtered run permission on the ROM-supported P4.4 input,
synchronized 60-2 crank input and a current foreground plan. Failed/stale TPS,
coolant, IAT, battery and speed-density MAP inputs now record standalone DTCs
without an engine inhibit or sensor substitutes; see [FAULTS.md](FAULTS.md).
The reference tuning client's status/diagnostic commands
expose the inhibit state. The owner's existing base map was not changed or
converted by this work; its fuel, angle and sensor units must match the schema
already described in PROTOCOL.md. A legacy map is not automatically schema-4
merely because it has the same byte length.

The board clock remains an inherited 20 MHz assumption. Crank-to-TDC reference,
coil pairing, input transfer/polarity, output currents, startup/bus wiring and
worst-case interrupt timing have not been physically established here.
Ignition compares now preserve the captured-tooth fire deadline through delayed
dwell admission, but actual coil-off is an ISR GPIO write and includes its
latency. Native diagnostics/MIL integration and physical power-latch release
remain incomplete; key-off holds the ECU powered because `hal_power_release()`
still reports unavailable. A firmware recovery/update mechanism is not supplied.
These limitations are detailed in RELEASE.md and ARCHITECTURE.md.

The emulator output test verifies that its enabled-owner test link is
byte-identical to this experimental artifact before executing its output
cases. Scripted SFRs and linked instruction execution do not constitute an
engine test. Commissioning evidence must be recorded separately.
