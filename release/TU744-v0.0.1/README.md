# TU744 v0.0.1

Standalone firmware for the Bosch M7.4.4 (stock M95080) on PSA TU engines.
The ECU identifies itself as `TU744 0.0.1`.

| File | Use |
| --- | --- |
| `firmware/TU744_0.0.1_M95080.bin` | Complete 512 KiB image to flash. The TU5JP 1.6 8v speed-density basemap is built in. |
| `firmware/TU744_0.0.1_M95080.hex` | The same image as HEX (sparse: erase 0x50000-0x6FFFF before programming it) |
| `firmware/*manifest.json` | Build and image identities |
| `flashing/` | `UploadViaDebug.exe` + `STAGE1.BIN`/`STAGE2.BIN`/`stage3.bin` (first flash via debug mode), `FlashUpdate.exe` (flash over K-line), `Hex2Bin.exe` |
| `tunerpro/TU744.dll` | TunerPro RT plugin 0.5.0 |
| `tunerpro/TU744_schema4.adx` | Live data / logging definition |
| `tunerpro/TU744_schema4_speed_density.xdf` | Map definition for the speed-density basemap |
| `tunerpro/TU744_schema4_alpha_n.xdf` | Map definition for alpha-N (throttle-based) tunes |
| `basemaps/TU5JP_1.6_8v_speed_density.bin` | The built-in basemap as a 3072-byte calibration file |
| `wizard/TuningWizard.exe` | Tuning Wizard |
| `SHA256SUMS.txt` | File checksums |

Step-by-step flashing and setup instructions are in the
[main README](../../README.md).
