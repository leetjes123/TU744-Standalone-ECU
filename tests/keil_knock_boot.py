"""Knock IC (CC195) boot sequence in the Keil C166 simulator: OEM ROM versus standalone.

Runs the unchanged OEM image (bins/M744_C167_FULL.bin, at 0x000000 and its
0x800000 alias) and the linked standalone HEX in uVision's simulator, headless.
Unlike the c167re emulator, Keil charges the programmed BUSCON0 wait states,
so this is the timing reference for the OEM hold. It does not charge BUSCON1
(external RAM) wait states, so bus-heavy code is slightly optimistic.

A script model of the 95080 serves the stock EEPROM contents over the SSC
(chip select P4.7), so the OEM takes its normal boot path. Every write to
P3/DP3/P8/DP8, and every bit write (Keil reports BSET/BCLR as a write to
address 0), records the levels at the seven CC195 control pins, with the IC's
pull-ups assumed on undriven lines.

Usage: python tests/keil_knock_boot.py [--stock-95080|--engine-experimental]
Needs Keil C166 (UV4) and the built HEX. No ECU is touched.
Evidence: engines/TU5JP/archive/36-knock-ic-cc195-init-and-calibration.md.
"""
import ctypes
import os
import re
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
PROFILE = next((p for p in ('stock-95080', 'engine-experimental') if f'--{p}' in sys.argv), 'stock-95080')
OUT = ROOT / 'build' / 'keil-knock'
OUT.mkdir(parents=True, exist_ok=True)
UV4 = Path(os.environ.get('KEIL_UV4', str(Path.home() / 'AppData/Local/Keil_v5/UV4/UV4.exe')))
CLOCK = 20_000_000
EE_BASE = 0xA00000
ROM = (REPO / 'bins/M744_C167_FULL.bin').read_bytes()
EEPROM = (REPO / 'bins/M7.4.4 EEPROM ORI_ImmoOff.BIN').read_bytes()
NAMES = ['G2', 'G1', 'G0', 'KTI', 'KSA3', 'MF', 'BF2']


def read_h86(path):
    image, base = bytearray(bytes([255]) * 0x80000), 0
    for line in path.read_text().splitlines():
        rec = bytes.fromhex(line[1:])
        n, at, kind = rec[0], int.from_bytes(rec[1:3], 'big'), rec[3]
        if kind == 0:
            image[base + at:base + at + n] = rec[4:4 + n]
        elif kind == 2:
            base = int.from_bytes(rec[4:6], 'big') << 4
        elif kind == 4:
            base = int.from_bytes(rec[4:6], 'big') << 16
    return bytes(image)


def ihex(regions):
    lines = []
    for start, data in regions:
        seg = None
        for off in range(0, len(data), 32):
            at = start + off
            if at >> 16 != seg:
                seg = at >> 16
                body = bytes([2, 0, 0, 4]) + seg.to_bytes(2, 'big')
                lines.append(':' + (body + bytes([(-sum(body)) & 255])).hex().upper())
            chunk = data[off:off + 32]
            body = bytes([len(chunk), (at >> 8) & 255, at & 255, 0]) + chunk
            lines.append(':' + (body + bytes([(-sum(body)) & 255])).hex().upper())
    lines.append(':00000001FF')
    return '\n'.join(lines) + '\n'


# 25-series EEPROM: WREN/WRDI/RDSR/WRSR/READ/WRITE, 32-byte pages, 5 ms write.
EEPROM_MODEL = r'''
DEFINE INT ee_sel
DEFINE INT ee_cmd
DEFINE INT ee_phase
DEFINE INT ee_addr
DEFINE INT ee_status
DEFINE INT ee_n
DEFINE INT ee_base
DEFINE INT ee_off
DEFINE INT ee_b
DEFINE INT ee_out
DEFINE LONG ee_wip_until
DEFINE LONG ee_bytes
ee_sel = 0
ee_cmd = 0x100
ee_status = 0
ee_wip_until = 0
ee_bytes = 0
FUNC void ee_cs (void) {
  unsigned int now;
  now = ((DP4 & 0x80) != 0) && ((P4 & 0x80) == 0);
  if (now == ee_sel) return;
  ee_sel = now;
  if (!now && ee_cmd == 2 && ee_n > 0 && (ee_status & 2)) { ee_wip_until = states + 100000; ee_status &= 0xFD; }
  ee_cmd = 0x100; ee_phase = 0; ee_n = 0;
}
FUNC int ee_st (void) {
  if (ee_wip_until != 0 && states < ee_wip_until) return (ee_status | 1);
  ee_wip_until = 0;
  return (ee_status & 0xFE);
}
FUNC void ee_tx (void) {
  ee_bytes++;
  ee_b = SSCTB & 0xFF;
  ee_out = 0xFF;
  if (!ee_sel) { SSCIN = 0xFF; return; }
  if (ee_cmd == 0x100) {
    ee_cmd = ee_b; ee_phase = 0;
    if (ee_b == 6) ee_status |= 2;
    if (ee_b == 4) ee_status &= 0xFD;
  } else if (ee_cmd == 5) {
    ee_out = ee_st ();
  } else if (ee_cmd == 1) {
    if (ee_phase == 0) { ee_status = (ee_b & 0x8C) | (ee_status & 2); ee_phase = 1; }
  } else if (ee_cmd == 3 || ee_cmd == 2) {
    if (ee_phase == 0) { ee_addr = (ee_b << 8) & 0xFF00; ee_phase = 1; }
    else if (ee_phase == 1) { ee_addr |= ee_b; ee_phase = 2; }
    else if (ee_cmd == 3) {
      if (ee_wip_until == 0 || states >= ee_wip_until) ee_out = _RBYTE (0xA00000 + (ee_addr % 1024));
      ee_addr = (ee_addr + 1) % 1024;
    } else {
      ee_base = ee_addr & 0xFFE0;
      ee_off = ee_base + ((ee_addr - ee_base + ee_n) % 32);
      if (ee_off < 1024 && (ee_status & 2)) _WBYTE (0xA00000 + ee_off, ee_b);
      ee_n++;
    }
  }
  SSCIN = ee_out;
}
BS WRITE SSCTB, 1, "ee_tx ()"
'''

RECORDER = r'''
DEFINE INT k3
DEFINE INT k8
k3 = 0x7FFF
k8 = 0x7FFF
FUNC void rec (void) {
  unsigned int v3, v8;
  ee_cs ();
  v3 = (P3 & 0x6E) | ((~DP3 & 0x6E) << 8);
  v8 = (P8 & 0x21) | ((~DP8 & 0x21) << 8);
  if (v3 != k3 || v8 != k8) printf ("KN %lu %04X %04X\n", states, v3, v8);
  k3 = v3; k8 = v8;
}
BS WRITE P3, 1, "rec ()"
BS WRITE DP3, 1, "rec ()"
BS WRITE P8, 1, "rec ()"
BS WRITE DP8, 1, "rec ()"
BS WRITE P4, 1, "rec ()"
BS WRITE DP4, 1, "rec ()"
BS WRITE 0x000000, 1, "rec ()"
FUNC void at_release (void) { printf ("AT %lu ee=%lu\n", states, ee_bytes); }
'''


def project(ini, name):
    """Scratch copy of TU5JP.uvproj: absolute paths, no run-to-main, this init file."""
    t = (ROOT / 'TU5JP.uvproj').read_text(encoding='utf-8').replace('.\\', str(ROOT) + '\\')
    t = t.replace('<RunToMain>1</RunToMain>', '<RunToMain>0</RunToMain>', 1)
    t, n = re.subn(r'(<SimDlls>.*?)<InitializationFile\s*(?:/>|>[^<]*</InitializationFile>)',
                   lambda m: m.group(1) + f'<InitializationFile>{ini}</InitializationFile>', t, count=1, flags=re.S)
    assert n == 1, 'could not set the simulator initialization file'
    t, n = re.subn(r'<OutputDirectory>[^<]*</OutputDirectory>',
                   lambda m: f'<OutputDirectory>{ROOT / "build" / PROFILE}\\</OutputDirectory>', t, count=1)
    assert n == 1
    path = OUT / f'{name}.uvproj'
    path.write_text(t, encoding='utf-8')
    return path


_user32 = ctypes.windll.user32
_ENUM = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)


def _dialogs(pid):
    found = []

    def text(h):
        buf = ctypes.create_unicode_buffer(1024)
        _user32.GetWindowTextW(h, buf, 1024)
        return buf.value

    def top(hwnd, _):
        owner = wintypes.DWORD()
        _user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        cls = ctypes.create_unicode_buffer(64)
        _user32.GetClassNameW(hwnd, cls, 64)
        if owner.value == pid and _user32.IsWindowVisible(hwnd) and cls.value == '#32770':
            texts, buttons = [], []

            def child(h, _):
                c = ctypes.create_unicode_buffer(64)
                _user32.GetClassNameW(h, c, 64)
                texts.append(text(h))
                if c.value == 'Button' and text(h).replace('&', '') == 'OK':
                    buttons.append(h)
                return True
            _user32.EnumChildWindows(hwnd, _ENUM(child), 0)
            found.append((hwnd, [t for t in texts if t], buttons))
        return True
    _user32.EnumWindows(_ENUM(top), 0)
    return found


def keil(name, image, release_at, run_ms):
    hexfile = OUT / f'{name}.hex'
    hexfile.write_text(ihex([(0, image), (0x800000, image), (EE_BASE, EEPROM)]))
    log = OUT / f'{name}.log'
    log.unlink(missing_ok=True)
    s = [f'LOG > {log}', 'MAP 0x800000, 0x87FFFF EXEC READ', 'MAP 0x380000, 0x387FFF READ WRITE',
         f'MAP 0x{EE_BASE:06X}, 0x{EE_BASE + 1023:06X} READ WRITE', f'LOAD {hexfile}', 'RESET',
         'AIN5 = 2.4', 'AIN10 = 1.5', 'AIN11 = 2.0']
    s += EEPROM_MODEL.strip().splitlines() + RECORDER.strip().splitlines()
    s += [f'BS 0x{release_at:06X}, 1, "at_release ()"']
    # Bounded run: a signal stops the simulation after run_ms, in 20 ms steps so the log flushes.
    for n in range(run_ms // 20):
        s += ['LOG OFF', f'LOG >> {log}', f'SIGNAL void stop{n} (void) {{',
              f'  twatch ({CLOCK // 50});', '  _break_ = 1;', '}', f'stop{n} ()', 'g', '_break_ = 0']
    s += ['LOG OFF', 'EXIT']
    ini = OUT / f'{name}.ini'
    ini.write_text('\n'.join(s) + '\n', encoding='ascii')
    proc = subprocess.Popen([str(UV4), '-d', str(project(ini, name))])
    start, status = time.time(), 'TIMEOUT'
    while time.time() - start < 600:
        try:
            proc.wait(1)
            status = 'exit'
            break
        except subprocess.TimeoutExpired:
            pass
        for hwnd, texts, buttons in _dialogs(proc.pid):
            if any('Registered ARM Compiler ignored' in t for t in texts):
                for b in buttons:
                    _user32.PostMessageW(b, 0x00F5, 0, 0)   # BM_CLICK the unrelated MDK warning
            else:
                # e.g. a simulated trap/watchdog notice after the window of interest:
                # end the run; the recorded events up to here are still valid.
                status = 'dialog: ' + ' | '.join(texts)
        if status.startswith('dialog'):
            break
    if proc.poll() is None:
        proc.kill()
    text = log.read_text(encoding='latin1') if log.exists() else ''
    assert status != 'TIMEOUT' and '*** error' not in text.lower(), (name, status)
    if status != 'exit':
        print(f'{name}: run ended by {status!r}')
    return text


def events(text):
    """Observable CC195 pin events from reset: (cpu_states, driven, levels) at
    G2 G1 G0 KTI KSA3 MF BF2 whenever a line becomes driven or changes level.
    Undriven lines sit at the IC's pull-up level (high)."""
    out = [(0, (0,) * 7, (1,) * 7)]
    for at, v3, v8 in re.findall(r'^KN (\d+) ([0-9A-F]{4}) ([0-9A-F]{4})', text, re.M):
        v3, v8 = int(v3, 16), int(v8, 16)
        lines = [(v3, b) for b in (0x08, 0x04, 0x02, 0x20, 0x40)] + [(v8, 0x01), (v8, 0x20)]
        driven = tuple(0 if (v >> 8) & bit else 1 for v, bit in lines)
        level = tuple(1 if (v >> 8) & bit else int(bool(v & bit)) for v, bit in lines)
        if (driven, level) != out[-1][1:]:
            out.append((int(at), driven, level))
    return out


listing = (ROOT / 'build' / PROFILE / 'TU5JP.m66').read_text()
symbols = {n: int(a, 16) for a, n in re.findall(r'^\s+([0-9A-F]{6})H\s+(\w+)\s+LABEL', listing, re.M)}
oem_text = keil('oem', ROM, 0x8493BC, 80)
std_text = keil(f'standalone-{PROFILE}', read_h86(ROOT / 'build' / PROFILE / 'TU5JP.H86'),
                symbols['board_knock_ic_release'], 100)
oem, std = events(oem_text), events(std_text)
for label, seq in (('OEM', oem), (f'standalone ({PROFILE})', std)):
    print(label)
    for at, driven, level in seq:
        print(f'  {at:>9} states {at / CLOCK * 1000:9.4f} ms  ' + ' '.join(
            f'{n}={v}{"" if d else "(pull-up)"}' for n, d, v in zip(NAMES, driven, level)))

assert [e[1:] for e in std] == [e[1:] for e in oem], 'CC195 pin sequence differs from the OEM'
ee = int(re.search(r'^AT \d+ ee=(\d+)', oem_text, re.M)[1])
assert ee == 1244, ('OEM took a different boot path', ee)
delta = [s_[0] - o[0] for s_, o in zip(std, oem)]
hold = lambda seq: seq[3][0] - seq[2][0]
steps = lambda seq: [seq[i + 1][0] - seq[i][0] for i in (3, 4)]
print('difference per event (states):', delta[1:])
assert steps(std) == steps(oem), (steps(std), steps(oem))
# T1 ticks are 16 states and the wait loops take 17 states per pass; code layout
# differs per profile. Allow two loop passes (34 states = 1.7 us) per event.
assert all(abs(d) <= 34 for d in delta), delta
print(f'PASS Keil knock IC boot: identical {len(std)} pin events from reset, each within '
      f'{max(abs(d) for d in delta)} CPU states of the OEM; hold OEM {hold(oem)} / standalone {hold(std)} '
      f'states; release steps {steps(std)} both; OEM read {ee} EEPROM bytes first')
