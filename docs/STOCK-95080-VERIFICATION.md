# Stock-95080 / TunerPro checkpoint, 21 September 2026

These are software results, not a physical engine release. This checkpoint adds
the stock EEPROM profile, flash persistence and the SDK-based TunerPro plugin.

| Check | Result |
|---|---|
| Keil stock-profile build | Zero warnings/errors, output-enabled experimental image |
| Native stock storage | 19,625 assertions, including 3,107 interrupted-program prefixes |
| Stock SSC/EEPROM | 9,788 assertions, including 1 KiB bounds |
| Native engine outputs | 15,427 assertions |
| Existing native core | 19,507 assertions and 901 sensor/fault-journal assertions |
| Linked stock C167 arithmetic/calibration | 1,095 checks |
| Linked stock RAM flash worker | 30 cases: both slots, failure, timeout reset, bounds |
| Linked stock engine outputs | 32 cases using bytes identical to the stock artifact |
| Linked stock fuel/AE | 26 cases |
| Linked stock sensor faults | 6 cases, measured rail/stale values and non-inhibited plans |
| TunerPro SDK DLL / actual firmware parser | 453 assertions, no serial I/O |
| XDF/ADX + offline tune packing | Bounds, units, framing, actual C167 boot and CRC rejection |
| Python client / actual firmware parser | Transactions, storage capabilities and 1,040 reset observations |
| Default Keil/uVision regression | 76,429 programmed bytes identical; original external sources unchanged |

Reproduction from the repository root:

```powershell
python firmware/tu5jp_standalone/tools/build.py stock-native
python firmware/tu5jp_standalone/tools/build.py stock-95080
python firmware/tu5jp_standalone/tools/build_tunerpro.py
python firmware/tu5jp_standalone/tests/test_flash_target.py
python firmware/tu5jp_standalone/tests/test_engine_target.py --stock-95080
python firmware/tu5jp_standalone/tests/test_fuel_target.py --stock-95080
python firmware/tu5jp_standalone/tests/test_faults_target.py --stock-95080
python firmware/tu5jp_standalone/tests/test_tunerpro_definitions.py
python firmware/tu5jp_standalone/tools/package_tunerpro.py
```

The ZIP contains a hash manifest, DLL, two XDFs, ADX, stock-profile firmware,
offline tune-packing tool and documentation. It excludes the downloaded SDK and
any base map. Neither ECU flashing nor plugin installation was performed.

Unverified boundaries: real TunerPro GUI loading/interaction, COM adapter/echo
timing, physical flash erase/program and reset behavior, power interruption,
crank/coil/injector timing on the board and engine acceptance. The flash tests
use a command model; native journal tests simulate interrupted byte prefixes.
They cannot establish electrical behavior. Existing diagnostic/MIL integration
and physical power-latch limitations remain as documented in RELEASE.md.
