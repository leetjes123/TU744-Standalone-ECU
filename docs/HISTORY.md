# Diagnostic history journal

The layout below describes the legacy 8 KiB EEPROM profile. The
[stock-95080 profile](STOCK-95080.md) retains one full history snapshot at byte0
and two standalone DTC slots at800/896 within 1 KiB.

The [standalone sensor/calibration DTC journal](FAULTS.md) is now a third owner
of the shared EEPROM lease. Its two slots occupy `3104..3199` and `7200..7295`
without changing this native-history codec or its slots. Shutdown saves those
private records first; recording sensor faults never enters service mode.

Update21 September: the running firmware now instantiates this owner at boot,
polls it in foreground and requests history saving during shutdown. Complete
native producer/input binding and physical latch release remain unfinished.
See [LIFECYCLE.md](LIFECYCLE.md); references below to a future owner describe
the earlier checkpoint.

`oem_history_codec.c` encodes the retained fields of the currently ported native
diagnostic state. `oem_history.c` stores an immutable snapshot in two EEPROM
journal slots. This is a standalone persistence transport, not a port of the
OEM standby-RAM supply or reset-cause policy. `oem_history_owner.c` now supplies
foreground dirty tracking and durable-clear bookkeeping. Admission and service
for the currently ported clear callbacks are now bound to that bookkeeping.
The running firmware instantiates it at boot and binds shutdown history saving.
Complete native input/task delivery, remaining mutation sites and physical
power-latch release remain pending.

## Ported clear integration

`oem_history_owner_clear_request` admits all-event, emissions-class or single-event
requests through the native request routines and marks the retained descriptors
dirty immediately. It requires a ready history owner; single-event requests are
restricted to IDs1..105. Rejected admission changes neither diagnostic nor owner
state. Admission does not report a completed clear. The caller supplies native
cycle initialization and foreground scheduling; this API does not enable either.

`oem_history_owner_clear_service` runs `oem_diagnostics_clear_ported`, then marks
the completed clear pending persistence. It validates the request/complement,
mode, manager gate and record indices before refreshing aliases or resetting any
producer. Missing, duplicate, timed-out or malformed requests cause no mutation.
The gate can defer service without consuming the request. The composition follows
the relative order of nine calls from native entry `29620`: drive phase, warm-up,
coolant, MIL, IAT, vehicle speed, supply voltage, the record worker, then readiness. Each producer reset
sees the still-pending request and live clear bits. Shared aliases are refreshed
after the worker; lamp output and demand are not artificially recomputed.

This is explicitly a projection of nine of the native entry's42 calls. The other
33 callbacks remain unimplemented; final `6C18E` is now ported. No complete
OEM clear, after-sales signature, tool response or runtime diagnostic integration
is claimed. See `q.diagnostic_clear_task_integration` in the TU5JP knowledge base.

An older in-flight snapshot cannot acknowledge a later completed clear. The
next explicit save must capture it and commit successfully. `clear_durable`
continues to describe the latest **completed** clear; merely admitting a new
request does not revoke that earlier result. Neither it nor `settled` reports
whether a deferred request still needs service. Ordinary diagnostic mutations
and maintenance-generated requests still require their owner notifications.

## Foreground owner contract

Zero one `OemHistoryOwner` at reset and call `oem_history_owner_load` at the
appropriate native boot restore point, with outputs and interrupts disabled.
Successful restore starts clean; absent/invalid history starts ready and dirty.
An unreadable device with no usable snapshot leaves the owner unready and blocks
saves until boot load is retried successfully. Native retained/lost-history
initialization remains the caller's responsibility. Load cannot be reused to
discard changes after the owner becomes ready.

Every diagnostic task must call `oem_history_owner_changed` after every
retained-state mutation (including initialization). Call
`oem_history_owner_cleared` only after the accepted native clear worker finishes,
not when a request is merely queued. All calls and diagnostic mutations belong
to the same foreground owner; no ISR may modify its state or snapshot source.

An explicit save snapshots the state and inherits stopped-engine service
admission. Polling success clears dirty only if no later mutation was reported.
A clear completed during an older save remains pending until a subsequent
snapshot commits. A later ordinary event can leave history dirty while the
earlier clear is durably recorded. Write failure or timeout never acknowledges
a clear, and retry is explicit. Boolean in-flight markers avoid generation-wrap
ambiguity. `clear_durable` is a session-local completion status, not a K-line
response or proof that the live event list is empty. It is not restored at boot.

`oem_history_owner_settled` describes only this owner's history. It does not
authorize power removal: calibration writes, EEPROM busy state, actuator and
native shutdown requirements still need their own completion checks. The owner
does not schedule diagnostic producers or invent a shutdown input.

Both calibration and history now obtain a foreground EEPROM lease covering the
entire journal operation. Competing saves and loads are rejected before they
touch the device or change the snapshot. Each terminal success, failure or
timeout releases only its own lease. This also applies to the underlying journal
API; do not mix direct journal calls with owner calls. The per-transfer SSC lease
remains separate. After a timeout the EEPROM may still be internally busy; the
next writer checks readiness before issuing another write.

Supplied functional evidence: Citroen training document 1.3.277, PDF pp.50 and
53 (technical pp.44 and 47), describes fault memory, tool erasure and a separate
after-sales operation history. This standalone journal and its durable-clear
bookkeeping are implementation choices, not evidence of OEM EEPROM behavior or
an implementation of the document's 50 after-sales zones. The native retained
field and clear-table evidence below remains specific to the TU5JP reference ROM.

## Retained payload and journal

The ROM's normal clear table at `148C8` clears physical ranges
`380000..38293F` and `384000..384FFF`. The lost-history table at `148B8` clears
`380000..384FFF`. The codec retains these diagnostic spans, in native address
order, with native little-endian words and unchanged record bytes:

| Logical RAM span | Bytes | Ported history |
|---|---:|---|
| AA5C..AA5F | 4 | Captured temperatures and retained phase bytes |
| AA60..AA67 | 8 | Scan positions, last event, count, overflow, context and warm-up count |
| AA6E | 1 | Retained MIL state |
| ABB6..ABB7 | 2 | Retained native context coordinate |
| B03E..B043 | 6 | Coolant raw history, drive count and demand bytes |
| B044..B223 | 480 | Twenty unchanged 24-byte event records |
| B224..B2FB | 216 | All 107 live descriptors and native timestamp |

Schema2 retains the original717 bytes plus the six readiness bytes AA68..AA6D,
after the32-byte reference-ROM SHA-256. A schema marker at755 and12 zero bytes
complete the768-byte payload. Schema1 snapshots are rejected. It does not persist volatile
operating flags, debounce filters, clear requests or live inputs. Decode
validates before mutation and refreshes shared aliases after restoring the
canonical fields. The diagnostic owner must still run native initialization
and select the appropriate retained/lost-history branch. Future producer ports
must review their retained fields and version this format if they add any.

| EEPROM region | Use |
|---|---|
| 0..3103 | Calibration slot0 header/payload |
| 3200..3999 | History slot0 header/payload |
| 4096..7199 | Calibration slot1 header/payload |
| 7296..8095 | History slot1 header/payload |

Compile-time checks reject overlap if calibration or history size changes.
Each 32-byte history header contains `LRH1`, schema2, payload length, sequence,
payload CRC16, header CRC16 and a final commit byte. The writer invalidates only
the inactive slot, verifies that invalidation, writes and reads back every
32-byte payload page, writes/verifies the header, then writes/verifies commit.
Sequence comparison handles 32-bit rollover. Load selects the newest complete
valid snapshot, falls back to the other slot on corruption, and leaves live
state unchanged if neither can be restored.

Save enters the existing stopped-engine service latch and obtains an immutable
snapshot before polling. It performs no writes during normal engine operation.
The caller must serialize diagnostic mutation while taking that snapshot,
initialize `OemHistory` to zero before its first use, and keep power available
until completion. A successful journal operation means that snapshot is stored;
it does not certify that later event changes or a later clear are persisted.
Load is a boot-only operation while outputs and interrupts are disabled; its
watchdog service follows the existing calibration boot-loader convention.

Native tests interrupt all 803 byte boundaries in the journal write stream,
check calibration/history isolation in both directions, immutable snapshots,
CRC fallback, write-busy and IO failure, timeout across clock rollover, service
admission and sequence rollover. They model sequential torn writes, not arbitrary
electrical corruption or EEPROM endurance. The independent codec test compares
against native retained memory and executes the unchanged ROM clear walker;
its target mode checks actual Keil pointer/byte-order behavior. No physical
EEPROM programming or power-loss experiment was performed.

Owner tests additionally inject retained mutations and repeated clears at all
eight journal phases, exercise both calibration/history admission orders,
failed admission, unreadable boot history, explicit retry and timeout across
clock wrap. `tests/test_history_owner.py` executes the actual Keil-linked owner,
journal, codec and EEPROM-lease code with modeled EEPROM transfers and service
admission. It verifies C166 far pointers and the stack-passed high word of the
save timestamp; it does not model the physical SSC or EEPROM write latency.
The expanded target tests execute actual request admission and composed clear
service during each of the eight writer phases, for all three request kinds.
They reload both the old and new snapshots, check the producer resets, reject
duplicates and corrupt requests without mutation, and exercise the stack-passed
single-event argument. There are58 target owner scenarios in total.
