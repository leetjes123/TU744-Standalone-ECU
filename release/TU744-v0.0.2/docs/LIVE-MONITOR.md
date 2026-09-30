# Live monitor, live tuning and firmware update

Firmware 0.0.2 extends compact command 13 to version 3 / 44 bytes, preserving
bytes 1..39 and appending AN15 millivolts (40..41), knock flags (42), and global
retard in 0.75-degree counts (43). Wizard accepts (v2,40) with knock unavailable
and (v3,44), rejecting other pairs. The framed reply is now 47 bytes, about
24.5 ms at 19200 baud. Legacy command 10 remains 98 bytes and uses its former
reserved tail for knock. See [KNOCK.md](KNOCK.md). The historical v2 layout
below remains valid for its original fields.

Plan and contract for faster live data, single-cell live tuning (the basis
for autotune) and the LRE-B4-compatible firmware-update path. Command numbers
are protocol commands (`AA len payload sum`, see [PROTOCOL.md](PROTOCOL.md)).

## Why command 10 is slow

Command 10 returns a 98-byte legacy envelope: 101 bytes on the wire. At
19200 baud (10 bits per byte, 0.52 ms) that is 53 ms per reply, plus the
request and up to one 10 ms foreground pass before the reply starts: at best
about 15 frames per second. About a third of it is dead weight for a tuner:
reserved or always-zero legacy bytes (21, 23, 24, 27, 28, 31, 39, 48, 68..74,
94..97), duplicates (CLT/IAT/advance appear twice, VE and AE twice), raw ADC
words (54..65, available on command 11), configuration bytes that never
change while driving (51, 37) and DTC counts (command 27/29 own those).

## Command 13: compact live frame (implemented)

40 bytes, big endian, version at byte 0. With framing 43 bytes: 22 ms on the
wire, so 30+ frames per second with foreground latency. Command 10 is kept
unchanged for TunerPro (TU744.dll / ADX).

| Byte | Type | Field |
|---|---|---|
| 0 | u8 | Frame version, 2 |
| 1 | u16 | RPM |
| 3 | u16 | MAP kPa |
| 5 | u16 | TPS in 0.1 % |
| 7 | u8 | Coolant C + 40 |
| 8 | u8 | Intake air C + 40 |
| 9 | u8 | Battery in 0.1 V |
| 10 | u8 | Wideband AFR x10; 0 = narrowband mode or invalid sample |
| 11 | u8 | Oxygen input mV / 5 (narrowband voltage) |
| 12 | u8 | Target AFR x10 |
| 13 | u16 | Applied fuel trim, 1024 = 100 % |
| 15 | u16 | Selected VE % (running map, cranking VE or anti-lag VE) |
| 17 | u16 | Planned injector pulse, us (admission and cuts still apply) |
| 19 | s16 | Planned advance, 0.1 deg BTDC |
| 21 | u8 | Warm-up multiplier % |
| 22 | u8 | After-start multiplier % |
| 23 | u8 | Acceleration multiplier %, saturated at 255 |
| 24 | u8 | IAC position, steps |
| 25 | u8 | Idle target RPM / 10 |
| 26 | u8 | Vehicle speed km/h |
| 27 | u8 | Map RPM axis index of the last fuel plan (0..14) |
| 28 | u8 | RPM fraction towards index+1, 0..255 (256 saturates) |
| 29 | u8 | Map load axis index (MAP, or TPS in alpha-N) |
| 30 | u8 | Load fraction, 0..255 |
| 31 | u8 | b0 sync, b1 cranking, b2 running, b3 overrun cut, b4 closed loop active, b5 fan, b6 pump, b7 launch |
| 32 | u8 | b0 rev limit, b1 fuel cut, b2 spark cut, b3 anti-lag, b4 unsaved tune, b5 save busy, b6 MIL lamp, b7 transaction open |
| 33 | u8 | Low nibble narrowband band (1 lean, 2 stoich, 4 rich, 0 n/a, as command 10 byte 93); high nibble gear |
| 34 | u16 | Output inhibit mask (command 25 byte 0 bits) |
| 36 | u16 | Calibration generation |
| 38 | u16 | Sync-loss count since boot (u32 firmware counter saturated to 65535) |

Bytes 27..30 are the bilinear weights the fuel plan used, taken from the same
`AxisAt` values as `table2_at`. An autotuner distributes each AFR error over
the four cells in proportion to (1-fr)(1-fl), fr(1-fl), (1-fr)fl, fr*fl,
without re-deriving the axis lookup on the PC.

Version 2 replaces version 1 bytes 38..39 (live-edit count) with the 16-bit saturated
sync-loss counter. Hosts must require an exact supported version and length;
the two layouts are not interchangeable. Command 32 still returns the live-edit
count. Current sync is independently reported in byte 31 bit 0. The last two
bytes are a saturated atomic snapshot of `Rotation.losses`, not a desktop-derived count.

### Further steps (planned, not implemented)

1. **Faster line.** The wire, not the payload, is now the limit. A
   command that switches ASC0 to 38400 or 57600 baud after an acknowledged
   handshake (reverting on 2 s silence) halves or thirds every frame. Needs a
   check of the K-line transceiver's slew limit on the board.
2. **Streaming.** A "stream on" command that sends a frame every N ms until
   any request byte arrives removes the request and its echo (4 bytes) and
   the foreground wait. K-line is half duplex, so the host must stop the
   stream before any other command; plan-age safety is unaffected because
   replies are already sent byte-by-byte outside the control pass.
3. **Frame versions.** New fields go at the end with a new version byte;
   hosts check an explicitly supported version and its exact length.

## Command 32: live map-cell write (implemented)

`32 offset:u16 count:u8 bytes...`, 1..16 bytes inside one of the four 16x16
maps. The reply is 5 bytes: status (0 accepted, 1 rejected), calibration
generation:u16, live-edit counter:u16.

| Map | Offsets | Raw range per cell (as `cal_validate`) |
|---|---|---|
| VE | 0x000..0x0FF | 0..255 |
| Ignition | 0x100..0x1FF | 0..140 (-20..50 deg) |
| Target AFR | 0x200..0x2FF | 70..220 (7.0..22.0) |
| Boost duty | 0x300..0x3FF | 0..100 |

The bytes go straight into the active calibration: no staging copy, no full
validation, no bank switch, so the next control pass uses them (the 05
transaction costs 1..3 passes and a 3 KiB copy). Rejected while a
transaction (21) or save (24) is open, without a valid calibration, across a
map boundary or outside the range. Allowed while running: this is the
autotune path.

Live writes do **not** change the calibration generation. Generation
changes restart wideband warm-up, disarm launch and abort a save in
progress; the live counter and "unsaved tune" flag report the edit instead.
A live edit is volatile until command 24 saves the tune.

Wideband without qualification (calibration 0x926 = 0) is now used for closed
loop as soon as its sample is valid, with no warm-up. With qualification on,
the warm-up timer no longer restarts on calibration writes.

## Command 01: firmware update (implemented)

The LRE-B4 path, so the same host tools work on both firmwares. Accepted
(status 00) only with the engine stopped and no calibration transaction or
save open; it latches service mode (outputs inhibited). After the status
and its echo have gone out, `hal_firmware_update` stops all outputs, masks
all interrupts, raises P3.5 as LRE-B4 does, copies `FWUPDATE.A66` to
external RAM and jumps there. From then on the line carries the handler's
raw protocol:

| Bytes | Action | Reply |
|---|---|---|
| `01 pp hh ll` | read word at page pp (16 KiB), offset hhll | hi, lo |
| `02 pp hh ll` + 4 x (hi lo) | program four words | none |
| `03` | (LRE-B4 stub) | `57` |
| `05 pp hh ll` | erase the sector containing pp:hhll | none |
| `06` | software reset | none |
| `07` | failed erase/program count (added) | `5A`, count |
| `08 pp` | word sums of page pp (added): s1 += w, s2 += s1 | `5A`, s1, s2 |
| other (hosts probe `66`) | none | `FF` |

The LRE-B4 handler answers 07/08 with `FF`, so a host detects the additions.
Differences from LRE-B4: erase and program wait for completion (DQ7/DQ5
polling) and service the watchdog, which TU744 keeps running; a host that
probes with `66` after an erase gets `FF` once the erase has finished.

P3.5 has conflicting labels: LRE-B4 source says "disable security module";
OEM ROM evidence in this repository labels it the CC195 knock-IC KTI input.
The engine is stopped during an update, so the LRE-B4 sequence is kept.
Physical confirmation on the ECU is still open.

The stock-M95080 image embeds its tune in NOR slot 0 (0x50000). Hosts must
erase both calibration sectors (0x50000..0x6FFFF) when they program a full
image, or an older slot-1 tune with a higher sequence number wins at boot.

Verification: `tests/test_fwupdate_target.py` runs the linked handler on the
repository C167 emulator with a K-line echo model and an Am29F400B command
model (13 cases); `tests/test_protocol.py` covers entry conditions, command 13
and command 32 against the compiled C parser.

The desktop tool that will use these commands is being ported; see
[../wizard/PORTING-PLAN.md](../wizard/PORTING-PLAN.md).
