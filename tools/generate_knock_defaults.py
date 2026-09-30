"""Extract schema-5 knock defaults from the hash-verified TU5JP image."""
from pathlib import Path
import sys
from oem_repo import oem_repo
ROOT = Path(__file__).resolve().parents[1]


def defaults(rom):
    data = bytearray(0xB7)
    data[0:8] = bytes([0, rom[0x18BF0], 2, 88, rom[0x18D99], rom[0x11058],
                       rom[0x1105C], rom[0x1105D]])
    data[8:12] = bytes.fromhex('01f4 0096')  # 500 ms latch, 150 ms stale
    data[12:16] = bytes([rom[0x11055], rom[0x11095], rom[0x11097], rom[0x11094]])
    data[2:4] = (rom[0x18D97] * 40).to_bytes(2, 'big')
    data[0x10:0x15] = bytes([0, rom[0x18D34], rom[0x18D44], 0, 100])
    for i in range(16):
        data[0x20+2*i:0x22+2*i] = (rom[0x181A0+i] * 40).to_bytes(2, 'big')
        columns = rom[0x18C01+4*i:0x18C05+4*i]
        if len(set(columns)) != 1: raise ValueError('OEM window columns differ')
        data[0x40+i] = columns[0]
        data[0x60+i] = min(rom[0x18C4C+16*slot+i] for slot in range(4))
    for dest, source in [(0x50,0x18BF1),(0x70,0x18D85),(0x80,0x18D34),
                         (0x90,0x18D44),(0xA0,0x18D55)]:
        data[dest:dest+16] = rom[source:source+16]
    data[0xB0:0xB7] = rom[0x1105E:0x11065]
    return data


if __name__ == '__main__':
    data = defaults((oem_repo()/'bins/M744_C167_FULL.bin').read_bytes())
    text = ('/* Generated from hash-verified TU5JP ROM by tools/generate_knock_defaults.py. */\n'
            'static const u8 knock_default_bytes[] = {\n')
    text += ''.join('    '+', '.join(f'0x{x:02X}' for x in data[i:i+16])+',\n'
                    for i in range(0,len(data),16)) + '};\n'
    path = ROOT/'src/knock_defaults.inc'
    if '--check' in sys.argv:
        if path.read_text() != text: raise SystemExit('Knock defaults differ from hashed ROM')
    else: path.write_text(text)
    print('PASS copied OEM defaults and shared-threshold derivation')
