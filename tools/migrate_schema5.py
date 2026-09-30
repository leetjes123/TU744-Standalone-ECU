"""Explicit, offline schema-4 to schema-5 migration; never alters the input file."""
from pathlib import Path
import argparse
import re

ROOT = Path(__file__).resolve().parents[1]


def migrate(data):
    if len(data) != 3072 or data[0x900:0x904] != b'LR\0\4':
        raise ValueError('A complete 3072-byte schema-4 tune is required')
    result = bytearray(data)
    result[0x903] = 5
    # Formerly reserved bytes: initialize only the new fields. All existing
    # fuel, ignition, idle, DFCO delay and diagnostic settings are preserved.
    result[0x93E:0x940] = (200).to_bytes(2, 'big')
    defaults = bytes(int(x, 16) for x in re.findall(
        r'0x([0-9A-F]{2})', (ROOT/'src/knock_defaults.inc').read_text()))
    result[0xA00:0xC00] = bytes(512)
    result[0xA00:0xA00+len(defaults)] = defaults
    assert result[0xA00] == 0  # control must never be enabled by migration
    return bytes(result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    converted = migrate(args.source.read_bytes())
    with args.destination.open('xb') as output:
        output.write(converted)
    print(f'Created {args.destination}; knock disabled; existing tuning retained')
