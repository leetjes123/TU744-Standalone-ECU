# Port validation — 28 September 2026

Software checks completed:

- MSVC Release build of `TuningWizard.exe` and its integration tests.
- CTest: 4/4 passed: calibration/protocol safety, generated calibration and DTC
  definition freshness, and neutral-interface branding/absence of identity telemetry.
- Desktop integration: 340 validator comparisons against the compiled C parser;
  definition bounds/overlaps; fixed/dynamic axes; compact frame version/length,
  signed units and sync losses; map-boundary splitting; map and transaction writes;
  lost commit replies; rejected writes and aborts; readback; structural running
  rejection; restoring a tune after an empty calibration; save-result polling;
  autotune gates/bounds; file roundtrip; minimum flash erase scope.
- Live-edit regression verifies that three changed bytes across a map boundary
  read back exactly those three bytes, without a full calibration read.
- Hidden desktop startup, neutral window title and clean shutdown.
- DTC tests: mixed-endian descriptors, first/latest subtype codes, freeze-frame
  units, unavailable trim, unknown subtype labels, controller record counts/ticks,
  empty/full scans, malformed/version/length errors, unready history, changed
  records and retained previous results. Clear tests cover running-engine
  refusal, rejection, acceptance, pending persistence, lost replies without
  retransmission and timeout. The compiled firmware parser rejects unavailable
  history and inadmissible clears as expected.
- Isolated DX11/ImGui visual fixture (`TW_BUILD_UI_PREVIEW=ON`), with synthetic
  records only: a simulated pointer click on the actual DTC card opens its
  freeze-frame modal. Inspected fault cards, the nine-value popup, empty scan,
  resized layout and high-contrast theme. The fixture executable and data are
  excluded from the release archive.
- Firmware `test_protocol.py`: compact v2, live writes, firmware-update entry,
  sync-loss values above 255 and above 65535, and unchanged command-10 layout.
- Full `build.py oem` regression suite passed, including OEM routine, sensor,
  rotation, ignition/dwell, diagnostics/history, cadence and protocol comparisons.
- `build.py stock-native`: 19,684 storage/lifecycle assertions and 3,107 interrupted
  program prefixes; 9,802 SSC assertions; 3,146,683 output/decoder assertions passed.
- `build.py stock-95080`: Keil compilation and link completed with zero warnings
  and errors. This is the output-enabled experimental stock-storage profile.
- `test_fwupdate_target.py`: 1,095 linked arithmetic/calibration checks and all
  13 firmware-update handler emulator cases passed, including failure status and
  page sums.

The package includes the firmware build manifest and SHA-256 hashes. Packaging
checks the image hash and source/build-input hashes against that manifest.

No physical ECU was flashed and no engine was run. Serial line timing, physical
flash/recovery behavior, autotune exhaust delay and engine acceptance remain
unvalidated. The bare firmware image contains **no calibration**; restore your
saved schema-4 tune using the sequence in the operator manual. No test fixture
is supplied as an engine tune.

The diagnostics follow-up changes the desktop only; commands 27/29/30 and
2B/2C already exist in the packaged firmware. Prior firmware regression/build
results above are retained; the binary/source manifest is rechecked when
packaging. Desktop output is now `build/bin/TuningWizard.exe`, allowing a
previous build to keep running without overwriting its executable.
