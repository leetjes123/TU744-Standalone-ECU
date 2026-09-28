"""Exercise the Python/Wizard reference contract against the actual C parser."""
from pathlib import Path
import ctypes as C
import os
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from tune_client import Client,frame
lib=C.CDLL(str(ROOT/'build/oem'/('oem.dll' if os.name=='nt' else 'oem.so')))
lib.bridge_exchange.argtypes=[C.c_char_p,C.c_uint16,C.c_void_p]
lib.bridge_exchange.restype=C.c_uint16

def raw(request):
    response=C.create_string_buffer(256)
    n=lib.bridge_exchange(request,len(request),response)
    return response.raw[:n]

def exchange(payload):
    response=raw(frame(payload))
    assert response[0]==0x55 and len(response)==response[1]+3,response
    assert sum(response[1:-1])&255==response[-1]
    return response[2:-1]

lib.bridge_reset()
client=Client(exchange)
assert client.identity() == 'TU744 0.0.1'
assert client.capabilities()['exact_dtc_parity'] is False
assert client.storage() == dict(calibration_storage='eeprom', eeprom_bytes=8192,
                               calibration_copies=2, history_copies=2)
assert exchange(bytes([0x2d, 0])) == b'\x01'
assert client.output_health() == dict(dwell_feedback_enabled=False, draining_coils=0,
    missing_feedback=[0, 0], invalid_feedback=[0, 0], correction_ticks=[0, 0], late_events=0)
assert exchange(bytes([0x2e, 0])) == b'\x01'
for malformed in [b'', bytes(18), b'\x01\x02'+bytes(16), b'\x01\0\x04'+bytes(15),
                  b'\x01\0\0\x01'+bytes(14)]:
    try: Client(lambda payload: malformed).output_health()
    except RuntimeError: pass
    else: raise AssertionError(('bad output health accepted', malformed))
health = Client(lambda payload: bytes.fromhex('010102000001ffff0002000380007fff0004')).output_health()
assert health == dict(dwell_feedback_enabled=True, draining_coils=2,
    missing_feedback=[1, 65535], invalid_feedback=[2, 3], correction_ticks=[-32768, 32767], late_events=4)
assert client.timing() == dict(foreground_pass_max_ms=0, foreground_interval_max_ms=0, tick_gap_max_ms=0,
    plan_age_max_ms=0, capture_overruns=0, late_events=0, cleared=False)
assert client.timing(clear=True)['cleared'] is True
detail = client.timing_detail()
assert detail['capture_fault']['reason'] == 'none' and detail['capture_resyncs'] == 0, detail
assert 'block_words' not in detail['capture_fault'], detail
assert exchange(bytes([0x2f, 3])) == b'\x01' and exchange(bytes([0x2f, 1, 0])) == b'\x01'
words = [1000 + 385 * i for i in range(30)]
reply = bytearray(96); reply[0] = 2
reply[14:30] = b''.join(v.to_bytes(2, 'big') for v in [2, 1392, 1391, words[-1], words[-1], 0x7E, 0x200, 0xF64A])
reply[30:36] = bytes([0, 0, 1, 30, 0, 3])
reply[36:96] = b''.join(v.to_bytes(2, 'big') for v in words)
detail = Client(lambda payload: bytes(reply)).timing_detail()
assert detail['capture_resyncs'] == 3 and detail['capture_fault']['counter_delta'] == 1, detail
assert detail['capture_fault']['block_words'] == words, detail
assert detail['capture_fault']['block_intervals'] == [385] * 29, detail
assert Client(lambda payload: bytes(reply[:34])).timing_detail()['capture_fault']['reason_code'] == 2
for malformed in [b'', bytes(14), b'\x01\x01'+bytes(12), b'\x01'+bytes(14)]:
    try: Client(lambda payload: malformed).timing()
    except RuntimeError: pass
    else: raise AssertionError(('bad timing health accepted', malformed))
timing = Client(lambda payload: bytes.fromhex('0100000c0012000200180000ffff')).timing()
assert timing == dict(foreground_pass_max_ms=12, foreground_interval_max_ms=18, tick_gap_max_ms=2,
    plan_age_max_ms=24, capture_overruns=0, late_events=65535, cleared=False)
lifecycle = exchange(bytes([0x27]))
assert len(lifecycle) == 24 and lifecycle[0] == 1 and lifecycle[23] == 0
assert lifecycle[7:14] == bytes(7)  # No booted/native-input bindings fabricated.
assert exchange(bytes([0x28, 2])) == b'\x01'  # No tune/permission to arm in this fixture.
assert exchange(bytes([0x28, 0])) == b'\0'
assert exchange(bytes([0x28, 3])) == b'\x01'
assert exchange(bytes([0x29, 0])) == b'\x01'  # Cannot read uninitialized history.
assert exchange(bytes([0x29, 20])) == b'\x01'
assert client.reset_status() == dict(available=False, raw_wdtcon=None,
                                   decoded_mask=2, watchdog_indicated=None)
reset_raw = C.c_uint16.in_dll(lib, 'reset_capture_raw')
reset_marker = C.c_uint16.in_dll(lib, 'reset_capture_marker')
for marker in [0, 0xffff, 0x5252, 0x5253]:
    reset_marker.value = marker
    for raw_word in [*range(256), 0x8000, 0x8002, 0xfffd, 0xffff]:
        reset_raw.value = raw_word
        valid = marker == 0x5253
        assert client.reset_status() == dict(available=valid,
            raw_wdtcon=raw_word if valid else None, decoded_mask=2,
            watchdog_indicated=bool(raw_word & 2) if valid else None)
        assert reset_raw.value == raw_word and reset_marker.value == marker
assert exchange(bytes([0x2a, 0])) == b'\x01'
for malformed in [b'\x01', bytes(8), b'\x02\x01\x00\x02\x00\x02\x01\x00',
                  b'\x01\x00\x00\x02\x00\x02\x00\x00',
                  b'\x01\x01\x00\x02\x00\x02\x00\x00']:
    try: Client(lambda payload: malformed).reset_status()
    except RuntimeError: pass
    else: raise AssertionError(('bad reset status accepted', malformed))
reset_raw.value = reset_marker.value = 0
original=client.read_tune()
candidate=bytearray(original);candidate[0]=65
assert client.activate(candidate)['generation']==2
assert client.read_tune()==candidate
invalid=bytearray(candidate);invalid[0x550:0x552]=b'\0\0'
try:client.activate(invalid)
except RuntimeError:pass
else:raise AssertionError('invalid thermistor configuration accepted')
assert client.read_tune()==candidate and client.capabilities()['generation']==2
fault_status = client.faults()
assert fault_status['stored'] & 1
assert fault_status['records'][0]['code'] == 'LRE-0001'
assert fault_status['records'][0]['reason'] == 2
assert fault_status['records'][0]['occurrences'] == 1
assert not client.status()['inhibits'] & 4  # No sensor inhibit was added by logging.
for payload in [bytes([0x2b, 0]), bytes([0x2c]), bytes([0x2c, 0]), bytes([0x2c, 7])]:
    assert exchange(payload) == b'\x01'
# The audit's real malformed write must not alter active memory.
assert raw(bytes.fromhex('AA040500202049'))==bytes.fromhex('55010102')
assert client.read_tune()==candidate
# A valid single-cell legacy write still works through independent validation.
assert exchange(bytes.fromhex('0500000146'))==b'\0'
assert client.read_tune()[0]==70
assert client.capabilities()['generation']==3
assert exchange(bytes([3,0,0,1,99]))==b'\x01'  # direct EEPROM write rejected
assert client.capabilities()['schema'] == 4
old = bytearray(client.read_tune()); old[0x902:0x904] = b'\0\3'
try: client.activate(old)
except ValueError: pass
else: raise AssertionError('client accepted schema-3 fuel units')
# Bypassing the client does not bypass the ECU's schema validation.
client.accepted([0x21])
client.accepted(bytes([5, 9, 2, 2, 0, 3]))
assert exchange(bytes([0x22])) == b'\x01'
client.accepted([0x23])
assert client.capabilities()['generation'] == 3
assert client.read_tune()[0x900:0x904] == b'LR\0\4'
monitor = exchange(bytes([0x10]))
assert len(monitor) == 98
assert monitor[80] == 3
assert monitor[89:91] == exchange(bytes([0x25]))[0:2]  # inhibits, extension 2
assert monitor[91:93] == bytes(2)  # history not booted: no native DTC counts
assert client.monitor()['coolant_c'] == int.from_bytes(monitor[82:84], 'big', signed=True)
assert exchange(bytes([0x25]))[20:26] == bytes(6)
for state, name in [(0,'unavailable'),(1,'lean'),(2,'stoich'),(4,'rich')]:
    sample=bytearray(monitor);sample[93]=state
    assert Client(lambda payload: bytes(sample)).monitor()['narrowband'] == name
for revision in (1,2):
    sample=bytearray(monitor);sample[80]=revision
    try: Client(lambda payload: bytes(sample)).monitor()
    except RuntimeError: pass
    else: raise AssertionError('obsolete monitor accepted')
assert monitor[18] == 100 and monitor[29:31] == bytes([0, 100])
assert monitor[78:80] == b'\0\x64' and not monitor[49] & 4
# Native DTC record decode: TPS event 0x1B, first subtype 4 (P0120), latest
# subtype 1 (P0123) and failing; steady MIL; freeze frame per oem_runtime.c.
from tune_client import decode_dtc
record = bytes([0x1B, 0, 0x01, 0x41, 0x48, 0, 0, 0, 0,
                130, 65, 50, 98, 138, 94, 65, 65, 72, 2, 136, 0x01, 0x2C, 3, 0])
dtc = decode_dtc(record)
assert dtc['code'] == 'P0123' and dtc['first_code'] == 'P0120' and dtc['active']
assert dtc['mil'] == 'on' and dtc['occurrences'] == 3 and dtc['on_time_counter'] == 300
assert dtc['freeze_frame'] == dict(rpm=3008, map_kpa=98, tps_percent=20.0, coolant_c=90,
    intake_c=25, battery_v=13.8, speed_kph=72, engine='running', fuel_trim_percent=106.2)
assert decode_dtc(bytes([0x7F]) + bytes(18) + b'\xff' + bytes(4))['code'] == 'event 0x7F/0'
assert decode_dtc(bytes(19) + b'\xff' + bytes(4))['freeze_frame']['fuel_trim_percent'] is None
# Draft example is generated by the same C validator and is never a car tune.
(ROOT/'build/oem/example-schema4.bin').write_bytes(original)
print('PASS C-parser/client transactions, 1040 reset observations, malformed-status rejection and legacy packet regression')

# Command 13: compact live frame v2 (docs/LIVE-MONITOR.md).
lib.bridge_reset()
live = exchange(b'\x13')
assert len(live) == 40 and live[0] == 2, live.hex()
generation = int.from_bytes(live[36:38], 'big')


def live_write(at, data):
    """Command 32: live map-cell write; returns (accepted, generation, live edits)."""
    reply = exchange(bytes([0x32, at >> 8, at & 255, len(data)]) + bytes(data))
    assert len(reply) == 5, reply
    return reply[0] == 0, int.from_bytes(reply[1:3], 'big'), int.from_bytes(reply[3:5], 'big')


assert live_write(0x0012, [77, 78]) == (True, generation, 1)
assert exchange(bytes([0x04, 0x00, 0x12, 2])) == bytes([77, 78])
assert live_write(0x0100, [140] * 16) == (True, generation, 2)  # ignition 50 deg
assert live_write(0x0100, [141])[0] is False                     # above 50 deg
assert live_write(0x0200, [69])[0] is False                      # AFR below 7.0
assert live_write(0x02FF, [220])[0]                              # AFR 22.0
assert live_write(0x0300, [101])[0] is False                     # boost duty above 100 %
assert live_write(0x00FF, [1, 2])[0] is False                    # crosses into ignition
assert live_write(0x0400, [1])[0] is False                       # axes are not live
assert exchange(bytes([0x32, 0, 0, 2, 1])) == b'\x01'            # length mismatch
assert exchange(b'\x21') == b'\x00'                              # open transaction
assert live_write(0x0000, [5])[0] is False
assert exchange(b'\x23') == b'\x00'
live = exchange(b'\x13')
assert int.from_bytes(live[36:38], 'big') == generation
assert int.from_bytes(live[38:40], 'big') == 0  # edit count is not sync losses
assert live[32] & 16, 'unsaved live edits must show calibration dirty'
# Command 01: accepted with the engine stopped; the handler is entered once
# the status reply has gone out (the LRE-B4 firmware-update path).
updates = C.c_uint16.in_dll(lib, 'fake_firmware_updates')
before = updates.value
assert exchange(b'\x01') == b'\x00'
assert updates.value == before + 1
lib.bridge_reset()
assert exchange(b'\x21') == b'\x00'
assert exchange(b'\x01') == b'\x01'                              # not with staging open
assert updates.value == before + 1
print('PASS live frame, live map-cell writes and firmware-update entry')


# Regression: the compact counter must retain values above 255 and not alias
# calibration edits. The legacy frame keeps its saturating byte unchanged.
lib.bridge_reset()
lib.bridge_sync_losses.argtypes = [C.c_uint32]
for losses in (0, 1, 255, 256, 0x1234, 65535, 65536, 0x12345678):
    lib.bridge_sync_losses(losses)
    compact = exchange(b'\x13')
    assert len(compact) == 40 and compact[0] == 2
    assert compact[38:40] == min(losses, 65535).to_bytes(2, 'big')
    legacy = exchange(b'\x10')
    assert len(legacy) == 98 and legacy[46] == min(losses, 255)
print('PASS compact full-width sync losses and unchanged legacy layout')
