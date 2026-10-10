# TU744 – standalone firmware for the Bosch M7.4.4 on PSA TU engines

TU744 turns the Bosch **M7.4.4** engine computer used on Peugeot/Citroën **TU**
petrol engines into a fully tunable standalone ECU. You keep the original ECU
box and the original wiring loom, and you tune it live from your laptop with
the included **Tuning Wizard**.

It is a port of [WillItBoost/M744-Stand-Alone-ECU](https://github.com/WillItBoost/M744-Stand-Alone-ECU),
reworked specifically for the PSA TU engine family (crank pattern, sensors,
injectors, coils and idle stepper as fitted to the TU).

> **Current release: TU744 v0.0.4** – see [`release/TU744-v0.0.4`](release/TU744-v0.0.4)
> and its [release notes](docs/RELEASE-0.0.4.md).
> When you connect, the ECU introduces itself as `TU744 0.0.4`.

> ⚠️ **Use at your own risk.** This is hobby software for off-road/motorsport use.
> It has been tested in simulation, not proven on every car. Keep a backup of
> your original ECU software (or a spare ECU), and check fuelling and timing with
> a wideband and a timing light before driving hard.

---

## What's in this repository

| Folder | What it is |
| --- | --- |
| [`release/TU744-v0.0.4`](release/TU744-v0.0.4) | **Ready-to-use files. Start here if you just want to flash your ECU.** |
| [`basemaps/`](basemaps) | Starting tune for a stock **TU5JP 1.6 8v** (speed-density) |
| [`flashing/`](flashing) | Flashing tools: `UploadViaDebug.exe` (+ `STAGE1/2/3` files), `FlashUpdate.exe`, `Hex2Bin.exe` |
| [`wizard/`](wizard) | Tuning Wizard – a standalone tuning/logging program (source) |
| `src/`, `include/`, `target/` | ECU firmware source (C167, Keil C166) |

### The release folder

```
release/TU744-v0.0.4/
├── firmware/      TU744_0.0.4_M95080.bin   ← the file you flash (basemap already inside)
│                  TU744_0.0.4_M95080.hex   ← same firmware as HEX
├── basemaps/      TU5JP_1.6_8v_speed_density_v0.0.4.bin
├── wizard/        TuningWizard.exe, OPERATOR_MANUAL.md
├── docs/          protocol, live-monitor and knock references
├── tools/         migrate_schema5.py (schema-4 tunes from 0.0.1)
└── verification/  build manifests and test logs
```

The flashing tools are in the repository's [`flashing/`](flashing) folder.

## The basemap

The firmware comes with a **basemap for the TU5JP 1.6 8v** already built in, so a
stock TU5JP can start straight after flashing. The same map is in
[`basemaps/TU5JP_1.6_8v_speed_density_v0.0.4.bin`](basemaps/TU5JP_1.6_8v_speed_density_v0.0.4.bin),
so you can always go back to it.

It is a **speed-density** tune: fuel is worked out from engine speed and intake
manifold pressure (MAP sensor). Open it in the Tuning Wizard with **Open file**.

A basemap is a *starting point*, not a finished tune. Every engine is a bit
different, so check your air/fuel ratio and fine-tune from there.

---

## What you need

- A Bosch **M7.4.4** ECU from a TU5JP-engined car (with its standard M95080 EEPROM).
- A **USB KKL / K-line cable** (the common "VAG-COM" style). The ECU talks at 19200 baud.
- A **12 V supply** (car battery or bench supply) and a few jumper wires for
  flashing on the bench.
- A Windows PC.
- The **Tuning Wizard** from the release folder.
- Recommended: a **wideband lambda controller** (e.g. 14point7 Spartan). Its
  0–5 V output goes to the ECU's former upstream (narrowband) O2 sensor input.

---

## Setup: from closed ECU to running engine

### 1. Back up and open the ECU

1. **Disconnect the battery and remove the ECU** from the car.
2. If you ever want to go back to stock, **read and save the original software**
   with your usual tool first. TU744 replaces it completely.
3. **Open the case** by carefully removing the lid to reach the circuit board.
   The video below shows the opened ECU and exactly where to connect.

### 2. Flash the firmware (via debug mode)

A factory ECU won't accept new firmware over the cable. The first time, you start
it in its **debug (boot) mode** and load a small helper program into it.

**Watch this video first. It shows the whole procedure:**
▶️ **https://www.youtube.com/watch?v=HnpJFZFuZvM**

1. **Connect the ECU on the bench**: 12 V, ground, and the K-line of your KKL
   cable, as shown in the video.
2. **Put the processor in debug mode** by making the connection on the board
   shown in the video, then switch on the 12 V.
3. **Run `UploadViaDebug.exe`** from the `flashing` folder. Keep `STAGE1.BIN`,
   `STAGE2.BIN` and `stage3.bin` in the **same folder** as the exe. It loads
   `stage3.bin` into the ECU. That file is a small basic program with a flash
   loader, so the ECU can then receive firmware over the cable.
4. **Switch off, remove the debug-mode connection, and switch on again.**
5. **Run `FlashUpdate.exe`**, pick your cable's COM port and the file
   **`firmware/TU744_0.0.4_M95080.bin`**, and let it finish. Don't disconnect
   anything while it's writing.
   (Tuning Wizard can do the same: **ECU → Update firmware**.)
6. Switch off and on again, then connect with the Wizard. The ECU
   should report **`TU744 0.0.4`**.

`Hex2Bin.exe` converts a `.hex` file into a `.bin`. You only need it if you
build the firmware yourself.

**Future updates are easier.** Once TU744 is on the ECU, you can install new
versions over the K-line cable with FlashUpdate or the Wizard. There's no need to
open the ECU or use debug mode again.

### 3. Put it back in the car

1. Close the ECU and plug it back into the **original loom**.
2. If you use a wideband, wire its 0–5 V signal to the former upstream O2 sensor
   input.
3. Connect the KKL cable to the car's diagnostic socket.

### 4. Set up the Tuning Wizard

1. Run **`release/TU744-v0.0.4/wizard/TuningWizard.exe`** and connect to your
   cable's COM port (19200 baud, 8N1).
2. Select **ECU > Read calibration** to load the tune that is in the ECU.
3. **Save file** to keep a copy on your PC before changing anything.
4. The dashboard shows live data (rpm, temperatures, AFR, faults and so on).

More detail: the [operator manual](wizard/OPERATOR_MANUAL.md).

### 5. Calibrate the throttle (TPS)

Do this once, with the **ignition on and the engine off**:

1. Wait a few seconds for the idle valve to finish homing.
2. In the Wizard, open the TPS calibration settings (**TPS closed ADC** and
   **TPS open ADC**).
3. Foot **off** the pedal, then click **Set Closed**.
4. Pedal **flat to the floor**, then click **Set WOT**. Release the pedal.
5. Click **Write active tune**, then **Save file** to keep a copy on your PC.
6. Select **Save tune to ECU flash**, wait for it to report success, then switch
   the ignition off and on again.

Check in the live data that the throttle reads about 0% released and 100% fully pressed.

### 6. First start

1. Ignition on. Listen for the fuel pump priming, and check the live data makes
   sense: coolant and air temperature, MAP near atmospheric, throttle 0%.
2. Start the engine. It should fire and settle into idle on the basemap.
3. Watch the **AFR** and **coolant temperature**. Check ignition timing with a
   timing light.
4. Make changes in the Wizard. With **Live tuning** on, map edits take effect
   immediately; use **Write active tune** for settings and axes.
5. **To keep your changes**, stop the engine, select **Save tune to ECU flash**,
   then switch the ignition off and on. (Saving a file on your PC does *not*
   store it in the ECU.)

---

## Building from source

Only needed if you want to change the software. You need Keil C166 (firmware),
Visual Studio (Wizard) and Python 3.

```powershell
cmake -S wizard -B wizard/build; cmake --build wizard/build --config Release
```

The firmware name and version are set in [`include/identity.h`](include/identity.h)
and returned by protocol command `00`.

## Credits

- [WillItBoost/M744-Stand-Alone-ECU](https://github.com/WillItBoost/M744-Stand-Alone-ECU):
  the original M7.4.4 standalone project this port is based on, including the
  flashing tools.
