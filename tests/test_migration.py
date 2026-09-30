from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from migrate_schema5 import migrate

source = (ROOT/'release/TU744-v0.0.1/basemaps/TU5JP_1.6_8v_speed_density.bin').read_bytes()
out = migrate(source)
assert len(out) == len(source) == 3072
for i, (before, after) in enumerate(zip(source, out)):
    if i not in (0x903, 0x93E, 0x93F) and i < 0xA00:
        assert before == after, hex(i)
assert out[0xA00] == 0 and out[0xA06] == 4 and out[0xA10:0xA15] == bytes([0,4,16,0,100])
assert out[0x603] == source[0x603]
for bad in [source[:-1], out, bytes(3072)]:
    try: migrate(bad)
    except ValueError: pass
    else: raise AssertionError('Invalid source accepted')
print('PASS schema-4 migration preserves every existing calibration byte; knock disabled')
