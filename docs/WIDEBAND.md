# Spartan on the upstream analog input

Owner-supplied hardware evidence, 18 and 22 September 2026: a 14point7 Spartan
with the controller in its cable supplies 0–5 V to the former narrowband ADC
input, and is powered across both OEM upstream-heater conductors: the
ECU-provided 12 V output and heater ground. This binds wideband measurement to
AN6 and controller power to the upstream-heater circuit. It does not establish
an additional ready wire, the exact Spartan generation, the board's exact
high-side/low-side topology, or the input board's actual voltage transfer.

The manufacturer's legacy [Spartan Lambda Sensor manual, p.1](https://cdn.shopify.com/s/files/1/0189/1312/files/Spartan_Lambda_Sensor_User_Manual.pdf?1955=)
specifies a linear output from 0.68 lambda at 0 V to 1.36 lambda at 5 V,
approximately 10–20 gasoline AFR. It separately describes a simulated
narrowband output. The owner's 0–5 V wiring is the linear output. This manual
does not specify a ready/fault encoding on that output.

The manufacturer lists separate legacy and later-generation manuals on its
[documentation page](https://www.14point7.com/pages/software-and-documentation).
Do not apply a Spartan 2/3 startup sequencer to the unidentified legacy unit.

Implementation:

- Choose wideband type at calibration byte 0x600 and use endpoints 100/200 at
  0x601/0x602 for the stated gasoline-AFR curve, subject to bench measurement.
  Scaling assumes the board presents the complete 0–5 V range to the ADC.
- ADC freshness, filtering and rail rejection are independent of conversion.
  Rail rejection is a standalone input-quality decision; it is not represented
  as an OEM P-code or a proven Spartan fault voltage.
- Internal sensor-heater regulation belongs to Spartan. In wideband mode the ECU
  energizes the OEM upstream-heater circuit from key-on to power the controller;
  this supply does not depend on enabling closed-loop qualification. Analog-only
  wideband calibration is rejected unless that equipment bit is enabled.
  P0135 can therefore diagnose the external ECU output, wiring and controller
  supply/load circuit after the OEM feedback interface is ported. It cannot
  diagnose the controller's internal heater regulation or sensor heater itself.
- STFT uses bounded incremental correction with no LTFT. Owner clarification
  on 21 September confirms only the analog signal is available. The firmware
  now implements an explicit analog-only qualification policy, disabled by
  default. It requires a calibrated warm-up interval and continuously fresh,
  in-range raw samples. Key-off, service, stale/rail samples and calibration
  changes revoke or restart qualification. AFR monitoring remains independent.
  A dropout restarts the full warm-up interval because internal controller state
  is unobservable; it may indicate a controller restart.

Elapsed warm-up time and a plausible voltage cannot distinguish a healthy sensor
from every controller/sensor fault. `WB_ANALOG_QUALIFIED` explicitly states this
limitation and is not a controller-health claim. The installed model, startup
and current/load behavior, transfer and calibration still need qualification. There is no
hidden override that silently reports controller health. OEM narrowband monitor
thresholds are not applied to the Spartan analog signal while claiming identical
OEM diagnostic triggers.

The policy uses extension0x926=1, warm-up ms at0x928 (10000..60000), continuous
good ms at0x92A (500..10000), and accepted raw voltage endpoints at0x92C/0x92E
(inside10..4990mV). Words are big-endian. These are selectable standalone limits,
not manufacturer readiness guarantees. Zero policy preserves inhibited STFT.
The old `wideband_input` byte no longer grants permission.
