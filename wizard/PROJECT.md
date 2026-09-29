# Tuning Wizard

Windows desktop editor, monitor and logger for the standalone schema-4 firmware.
The executable is `TuningWizard.exe`. The original desktop application is not modified.

## Build

Use CMake 3.20+ and an MSVC developer shell with Windows SDK tools:

```powershell
python tools/wizard_definition.py
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

CMake fetches the pinned ImGui and FreeType versions. For an offline build, set
`FETCHCONTENT_SOURCE_DIR_IMGUI` and `FETCHCONTENT_SOURCE_DIR_FREETYPE` to existing
source checkouts. Generated C++ definitions are checked in; regenerating them
requires the adjacent standalone firmware tree and its ROM-derived DTC table.
The firmware source and XDF generator remain the authorities for calibration.

Before running desktop integration tests, build the firmware parser library
(`build/oem/oem.dll`):

```powershell
python tools/wizard_definition.py --check
ctest --test-dir build -C Release --output-on-failure
```

`tests/port_tests.cpp` exercises the actual C parser through its bridge, including
340 validator comparisons, live writes, transaction commits, dropped replies,
write rejection, readback, save results, schema files and compact telemetry.
It also checks definition overlap/bounds, axes, autotune gates and flash erase scope.

## Contracts

- Calibration: 3072 bytes; schema marker `4C 52 00 04` at 0x900.
- Protocol: capability version 3, schema 4, 32-byte writes and 128-byte reads.
- Live data: command 13, exactly 40 bytes, version 2. Bytes 38..39 report sync
  losses, saturated at 65535; byte 31 bit 0 reports current sync.
- Map edits: command 32, at most 16 bytes, never across a map boundary.
- Other edits: commands 21/05/22; abort with 23 on failure. Verify generation
  and read back the active tune. Structural changes require a stopped engine.
- Persistence: command 24, poll command 25 byte 8 for completion. Saving latches
  service mode; the ECU requires a key cycle afterwards.
- Firmware update: 512 KiB image with `FA 00` reset vector, erasing both tune slots,
  probing erase completion and checking handler status where supported. Firmware
  page checksums are not requested or compared.

No ECU identity/security indication is displayed. Firmware version remains an
internal protocol operation. Unsupported legacy fields are absent from logs.

See [OPERATOR_MANUAL.md](OPERATOR_MANUAL.md) and [PORTING-PLAN.md](PORTING-PLAN.md).
Physical ECU flashing, line timing and engine tuning acceptance remain outstanding.
