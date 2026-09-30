"""Schema-5 reference client for bench use and the Wizard transport integration.

No flash/update command, raw EEPROM write, automatic legacy-tune conversion, or
implicit save. pyserial is needed only for the command-line serial transport.
"""
from pathlib import Path
import argparse
import json
import time


def frame(payload):
    if not 1 <= len(payload) <= 36:
        raise ValueError("request payload must contain 1..36 bytes")
    body = bytes([len(payload)]) + bytes(payload)
    return b"\xaa" + body + bytes([sum(body) & 255])


class Client:
    def __init__(self, exchange):
        self.exchange = exchange

    def command(self, payload):
        return self.exchange(bytes(payload))

    def identity(self):
        return self.command([0]).decode('ascii')

    def accepted(self, payload):
        response = self.command(payload)
        if response != b"\0":
            raise RuntimeError(f"command 0x{payload[0]:02x} rejected: {response.hex()}")

    def capabilities(self):
        b = self.command([0x20])
        if len(b) != 10 or b[0:2] != b"\x03\x05":
            raise RuntimeError("firmware does not expose protocol 3 / schema 5")
        return dict(protocol=b[0], schema=b[1], size=int.from_bytes(b[2:4], "big"),
                    generation=int.from_bytes(b[4:6], "big"), write_limit=b[6],
                    read_limit=b[7], exact_dtc_parity=bool(b[8]), board_released=bool(b[9]))

    def read_tune(self):
        caps = self.capabilities()
        data = bytearray()
        for at in range(0, caps["size"], 128):
            count = min(128, caps["size"] - at)
            b = self.command(bytes([4]) + at.to_bytes(2, "big") + bytes([count]))
            if len(b) != count:
                raise RuntimeError("short calibration read")
            data.extend(b)
        if self.capabilities()["generation"] != caps["generation"]:
            raise RuntimeError("calibration changed while reading; repeat the dump")
        return bytes(data)

    def activate(self, data):
        caps = self.capabilities()
        if len(data) != caps["size"] or data[0x900:0x904] != b"LR\0\5":
            raise ValueError("expected a complete schema-5 calibration image")
        self.accepted([0x21])
        try:
            for at in range(0, len(data), 32):
                part = data[at:at+32]
                self.accepted(bytes([5]) + at.to_bytes(2, "big") + bytes([len(part)]) + part)
            self.accepted([0x22])
        except Exception:
            # On an ambiguous commit response, abort cannot undo activation.
            # Report failure; do not repeat the commit or assume the old tune.
            try:
                self.accepted([0x23])
            except Exception:
                pass
            raise
        current = self.capabilities()
        if current["generation"] != (caps["generation"] + 1) & 65535:
            raise RuntimeError("unexpected calibration generation after commit")
        if self.read_tune() != data:
            raise RuntimeError("activated calibration readback differs")
        return current

    def status(self):
        b = self.command([0x25])
        if len(b) != 34:
            raise RuntimeError("unexpected status schema")
        return dict(inhibits=int.from_bytes(b[0:2], "big"),
                    generation=int.from_bytes(b[4:6], "big"),
                    error_offset=int.from_bytes(b[6:8], "big"), storage_result=b[8],
                    service_latched=bool(b[9]), iac_state=b[10], iac_fault=b[11],
                    stft_enabled=bool(b[26]), mil_steady=bool(b[32]))

    def knock_ic(self):
        """Knock IC (CC195) idle check through command 11 (raw ADC).

        With MF (P8.0) held low and no knock window running, the integrator
        output KI on AN15 sits at its start voltage VDD/7 +- 0.2 V: 8-bit
        26..46, OEM clamp 0x19..0x30, nominal 0x25 (archive/36, section 1).
        A reading far outside that window means the IC is not driven, not
        powered, or held in another mode."""
        b = self.command([0x11])
        if len(b) != 32:
            raise RuntimeError("unexpected raw ADC reply length")
        raw = int.from_bytes(b[30:32], "big")
        return dict(an15_raw=raw, an15_volts=round(raw * 5 / 1024, 3), an15_8bit=raw >> 2,
                    integrator_start_ok=0x19 <= raw >> 2 <= 0x30,
                    expected_8bit="0x19..0x30, nominal 0x25 (0.72 V)")

    def monitor(self):
        b = self.command([0x10])
        if len(b) != 98 or b[80] not in (3, 4):
            raise RuntimeError("TU744 logging requires monitor extension 3 or 4")
        return dict(coolant_c=int.from_bytes(b[82:84], 'big', signed=True),
                    intake_c=int.from_bytes(b[84:86], 'big', signed=True),
                    planned_advance_deg=int.from_bytes(b[86:88], 'big', signed=True) / 10,
                    planned_pulse_us=int.from_bytes(b[6:8], 'big'),
                    legacy_temperature_clipped=b[81], mil_output=bool(b[88]),
                    inhibits=int.from_bytes(b[89:91], 'big'),
                    dtcs_stored=b[91], dtcs_active=b[92],
                    narrowband={0: 'unavailable', 1: 'lean', 2: 'stoich', 4: 'rich'}.get(b[93], 'invalid'),
                    knock=self.decode_knock(b[94:98]) if b[80] == 4 else None)

    @staticmethod
    def decode_knock(b):
        mv = int.from_bytes(b[:2], 'big')
        return dict(an15_mv=None if mv == 65535 else mv, detected=bool(b[2]&1),
                    voltage_fresh=bool(b[2]&2), control_active=bool(b[2]&4),
                    monitor_only=bool(b[2]&8), sensing_fault=bool(b[2]&16),
                    bench=bool(b[2]&32), last_detected=bool(b[2]&64),
                    running_window_valid=bool(b[2]&128), requested_retard_deg=b[3]*0.75)

    def knock_details(self, job=False):
        b = self.command([0x35, 3] if job else [0x34])
        if len(b) != (86 if job else 64) or b[0] != 1:
            raise RuntimeError('unsupported knock detail packet')
        result = self.decode_knock(b[1:5])
        word = lambda at: int.from_bytes(b[at:at+2], 'big')
        long = lambda at: int.from_bytes(b[at:at+4], 'big')
        result.update(raw=word(5), sequence=word(7), stamp_ms=long(9), sample_age_ms=long(13),
                      count=long(17), last_event_age_ms=long(21), source=b[25], type=b[26],
                      offset=b[27], amplitude=b[28], reference=b[29], gain=b[30], gain_code=b[31],
                      ratio=b[32], threshold=b[33], decision=bool(b[34]), scheduled_retard_deg=b[35]*.75,
                      fault=b[36], hold=word(37), epoch=word(39), generation=word(41),
                      adc_timeouts=word(43), missed_windows=word(45), stale_samples=word(47),
                      ceiling_events=word(49), normal_count=long(51), null_count=long(55),
                      test_count=long(59), null_start=b[63])
        if job:
            if b[64] != 1: raise RuntimeError('unsupported knock job packet')
            result.update(job_state=b[65], completed_windows=b[66], job_fault=b[67],
                          null_raw=word(68), test_raw=word(70),
                          gain_raw=[word(72+2*i) for i in range(7)])
        return result

    def knock_job(self, action):
        self.accepted([0x35, action])
        return self.knock_details(job=True)

    def dtcs(self):
        """Stored native DTC records with their freeze frames (command 29).

        A record is created when a DTC is first confirmed. The freeze frame is
        captured then; later occurrences update only the count and timestamp.
        """
        life = self.command([0x27])
        if len(life) != 24 or life[0] != 1:
            raise RuntimeError("unexpected lifecycle status schema")
        if not life[7]:
            raise RuntimeError("diagnostic history has not booted yet")
        count = life[20]
        if count > 20:
            raise RuntimeError("corrupt native DTC record count")
        records = []
        for slot in range(count):
            r = self.command([0x29, slot])
            if len(r) != 24:
                raise RuntimeError("unexpected native DTC record")
            records.append(decode_dtc(r))
        return dict(stored=count, active=sum(r["active"] for r in records), records=records)

    def clear_dtcs(self):
        """Clear stored DTCs (command 30): engine stopped, key on. Faults still
        present are stored again. Firmware before 2026-09-24 rejects it."""
        self.accepted([0x30])
        return dict(cleared=True)

    def output_health(self):
        b = self.command([0x2E])
        if len(b) != 18 or b[0] != 1 or b[1] > 1 or b[2] & ~3 or b[3]:
            raise RuntimeError('unexpected output-health schema')
        return dict(dwell_feedback_enabled=bool(b[1]), draining_coils=b[2],
                    missing_feedback=[int.from_bytes(b[n:n+2], 'big') for n in (4, 6)],
                    invalid_feedback=[int.from_bytes(b[n:n+2], 'big') for n in (8, 10)],
                    correction_ticks=[int.from_bytes(b[n:n+2], 'big', signed=True) for n in (12, 14)],
                    late_events=int.from_bytes(b[16:18], 'big'))

    def timing(self, clear=False):
        b = self.command([0x2F, 1] if clear else [0x2F])
        if len(b) != 14 or b[0] != 1 or b[1]:
            raise RuntimeError('unexpected timing-health schema')
        w = [int.from_bytes(b[n:n+2], 'big') for n in range(2, 14, 2)]
        return dict(foreground_pass_max_ms=w[0], foreground_interval_max_ms=w[1],
                    tick_gap_max_ms=w[2], plan_age_max_ms=w[3],
                    capture_overruns=w[4], late_events=w[5], cleared=clear)

    def timing_detail(self):
        b = self.command([0x2F, 2])
        if len(b) not in (34, 96) or b[0] != 2 or b[1]:
            raise RuntimeError('timing-detail requires capture-detail firmware')
        health = Client(lambda payload: bytes([1]) + b[1:14]).timing()
        w = [int.from_bytes(b[n:n+2], 'big') for n in range(14, 30, 2)]
        health['capture_fault'] = dict(
            reason={0:'none', 1:'queue full', 2:'counter/capture mismatch',
                    4:'unstable first capture'}.get(w[0], 'unknown'),
            reason_code=w[0], counter=w[1], expected_counter=w[2],
            counter_delta=(w[1]-w[2]) & 65535, captured_timer=w[3],
            latest_capture_timer=w[4], capture_interrupt_register=w[5],
            pec_register=w[6], destination=w[7], head=b[30], tail=b[31],
            next_slot=b[32], block_count=b[33])
        if len(b) == 96:
            words = [int.from_bytes(b[n:n+2], 'big') for n in range(36, 96, 2)]
            health['capture_resyncs'] = int.from_bytes(b[34:36], 'big')
            if w[0]:
                health['capture_fault']['block_words'] = words[:b[33]]
                health['capture_fault']['block_intervals'] = [
                    (y - x) & 65535 for x, y in zip(words[:b[33]], words[1:b[33]])]
        return health

    def reset_status(self):
        b = self.command([0x2A])
        if (len(b) != 8 or b[0] != 1 or b[1] > 1 or b[6] > 1 or b[7] != 0
                or b[4:6] != b"\x00\x02"):
            raise RuntimeError("unexpected reset observation schema")
        valid = bool(b[1])
        raw = int.from_bytes(b[2:4], "big")
        if (not valid and raw) or bool(b[6]) != (valid and bool(raw & 2)):
            raise RuntimeError("inconsistent reset observation")
        return dict(available=valid, raw_wdtcon=raw if valid else None,
                    decoded_mask=2, watchdog_indicated=bool(b[6]) if valid else None)

    def faults(self):
        b = self.command([0x2B])
        if len(b) != 12 or b[:2] != b"\x01\x06" or b[10:12] != b"\0\0":
            raise RuntimeError("unexpected standalone fault schema")
        result = dict(active=int.from_bytes(b[2:4], "big"), stored=int.from_bytes(b[4:6], "big"),
                      storage_ready=bool(b[6]), dirty=bool(b[7]), write_phase=b[8],
                      storage_result=b[9], records=[])
        names = ["calibration", "TPS", "coolant", "intake temperature", "battery", "MAP"]
        for index, name in enumerate(names, 1):
            row = self.command([0x2C, index])
            if len(row) != 16 or row[:2] != bytes([1, index]) or row[5]:
                raise RuntimeError("unexpected standalone fault record")
            if row[3]:
                result["records"].append(dict(code=f"LRE-{index:04d}", name=name,
                    active=bool(row[2]), reason=row[4], occurrences=int.from_bytes(row[6:8], "big"),
                    first_uptime_ms=int.from_bytes(row[8:12], "big"),
                    last_uptime_ms=int.from_bytes(row[12:16], "big")))
        return result

    def storage(self):
        b = self.command([0x2D])
        if len(b) != 8 or b[0] != 1 or b[1] > 1 or b[6:8] != b'\0\0':
            raise RuntimeError('unexpected storage capability response')
        return dict(calibration_storage='nor-flash' if b[1] else 'eeprom',
                    eeprom_bytes=int.from_bytes(b[2:4], 'big'), calibration_copies=b[4],
                    history_copies=b[5])

    def save(self):
        self.capabilities()
        self.accepted([0x24])
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            try:
                state = self.status()
            except (TimeoutError, RuntimeError):
                # Read-only retries tolerate the stock NOR erase interval.
                # Never repeat the save command after an ambiguous response.
                time.sleep(0.05)
                continue
            if state["storage_result"] == 1:
                return state
            if state["storage_result"] in (2, 3):
                raise RuntimeError(f"persistent save failed: {state}")
            time.sleep(0.05)
        raise TimeoutError("save status unknown; read status before taking further action")


class SerialExchange:
    def __init__(self, port):
        import serial
        self.port = serial.Serial(port, 19200, timeout=0.02, write_timeout=1)

    def __call__(self, payload):
        request = frame(payload)
        self.port.reset_input_buffer()
        self.port.write(request)
        received = bytearray()
        deadline = time.monotonic() + 1.5
        while time.monotonic() < deadline:
            received.extend(self.port.read(256))
            # The K-line adapter may echo the whole request. Wait for that echo
            # before looking for 0x55 within it; calibration data can contain it.
            start = 0
            if received[:1] == b"\xaa":
                if len(received) < len(request):
                    continue
                if received[:len(request)] != request:
                    raise RuntimeError("corrupt request echo")
                start = len(request)
            for at in range(start, len(received) - 2):
                if received[at] != 0x55:
                    continue
                size = received[at + 1]
                end = at + size + 3
                if size <= 128 and end <= len(received):
                    body = received[at+1:end-1]
                    if sum(body) & 255 == received[end-1]:
                        return bytes(received[at+2:end-1])
            if len(received) > 512:
                raise RuntimeError("unframed response exceeds receive bound")
        raise TimeoutError("ECU response timed out")


# Report codes per descriptor subtype 1/2/4/8 for events with a standalone
# monitor (ROM report table; engines/TU5JP/defs/dtc_events.csv).
DTC_CODES = {
    0x03: ('P0300',) * 4, 0x04: ('P0301',) * 4, 0x05: ('P0303',) * 4,
    0x06: ('P0304',) * 4, 0x07: ('P0302',) * 4, 0x08: ('P0300',) * 4,
    0x09: ('P0301',) * 4, 0x0A: ('P0303',) * 4, 0x0B: ('P0304',) * 4,
    0x0C: ('P0302',) * 4, 0x0D: ('P0338', 'P0337', 'P0335', 'P0336'),
    0x1A: ('P1327',) * 4, 0x1B: ('P0123', 'P0122', 'P0120', 'P0121'),
    0x27: ('P0171', 'P0172', 'P0170', 'P0170'), 0x2D: ('P0130',) * 4,
    0x3F: ('P0133',) * 4, 0x40: ('P0133',) * 4,
    0x43: ('P0108', 'P0107', 'P0109', 'P0106'), 0x45: ('P0132', 'P0131', 'P0134', 'P0130'),
    0x4A: ('P0338', 'P0337', 'P0335', 'P0336'), 0x4B: ('P1327',) * 4,
    0x4D: ('P1528', 'P1527', 'P1529', 'P1526'), 0x50: ('P0606',) * 4,
    0x56: ('P1525', 'P1525', 'P1524', 'P1523'), 0x5B: ('P0113', 'P0112', 'P0110', 'P0111'),
    0x61: ('P0118', 'P0117', 'P0115', 'P0116'), 0x65: ('P0563', 'P0562', 'P0560', 'P0561'),
    0x68: ('P0503', 'P0502', 'P0500', 'P0501'),
}
ENGINE_MODES = {0: "stopped", 1: "cranking", 2: "running"}


def dtc_code(event, subtype):
    if event in DTC_CODES and subtype in (1, 2, 4, 8):
        return DTC_CODES[event][(1, 2, 4, 8).index(subtype)]
    return f"event 0x{event:02X}/{subtype}"


def decode_dtc(r):
    """Decode a 24-byte native record. Words at 2 and 4 are little-endian."""
    descriptor = r[2] | r[3] << 8
    status = r[4] | r[5] << 8
    return dict(
        code=dtc_code(r[0], descriptor >> 8 & 15), first_code=dtc_code(r[0], descriptor >> 12 & 15),
        event=r[0], active=bool(descriptor & 1),
        mil="flashing" if status & 16 else ("on" if status & 8 else "off"),
        occurrences=r[22],
        # Native clock: one count per 361 x 100 ms of ECU on-time, retained.
        on_time_counter=r[20] << 8 | r[21], on_time_approx_min=round((r[20] << 8 | r[21]) * 36.1 / 60, 1),
        freeze_frame=dict(
            rpm=r[14] * 32, map_kpa=r[12], tps_percent=round(r[11] * 0.4, 1),
            coolant_c=r[9] - 40, intake_c=r[10] - 40, battery_v=r[13] / 10,
            speed_kph=r[17], engine=ENGINE_MODES.get(r[18], r[18]),
            # 255 = firmware before 2026-09-24 did not capture the trim.
            fuel_trim_percent=None if r[19] == 255 else round(r[19] * 100 / 128, 1)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("action", choices=["identity", "caps", "status", "monitor", "outputs", "timing", "timing-clear", "timing-detail", "reset", "faults", "dtcs", "clear-dtcs", "storage", "dump", "activate", "save", "knock-ic", "knock-details", "knock-bench-start", "knock-selftest", "knock-job", "knock-stop"])
    parser.add_argument("file", nargs="?", type=Path)
    args = parser.parse_args()
    if args.action in ("dump", "activate") and args.file is None:
        parser.error("dump/activate requires a file")
    transport = SerialExchange(args.port)
    client = Client(transport)
    try:
        if args.action == "identity": result = client.identity()
        elif args.action == "caps": result = client.capabilities()
        elif args.action == "status": result = client.status()
        elif args.action == "monitor": result = client.monitor()
        elif args.action == "outputs": result = client.output_health()
        elif args.action == "timing": result = client.timing()
        elif args.action == "timing-detail": result = client.timing_detail()
        elif args.action == "timing-clear": result = client.timing(clear=True)
        elif args.action == "reset": result = client.reset_status()
        elif args.action == "faults": result = client.faults()
        elif args.action == "dtcs": result = client.dtcs()
        elif args.action == "clear-dtcs": result = client.clear_dtcs()
        elif args.action == "storage": result = client.storage()
        elif args.action == "knock-ic": result = client.knock_ic()
        elif args.action == "knock-details": result = client.knock_details()
        elif args.action == "knock-job": result = client.knock_details(job=True)
        elif args.action == "knock-bench-start": result = client.knock_job(1)
        elif args.action == "knock-selftest": result = client.knock_job(2)
        elif args.action == "knock-stop": result = client.knock_job(0)
        elif args.action == "dump":
            data = client.read_tune()
            with args.file.open("xb") as file: file.write(data)
            result = {"written": str(args.file), "bytes": len(data)}
        elif args.action == "activate": result = client.activate(args.file.read_bytes())
        else: result = client.save()
        print(json.dumps(result, indent=2))
    finally:
        transport.port.close()


if __name__ == "__main__": main()
