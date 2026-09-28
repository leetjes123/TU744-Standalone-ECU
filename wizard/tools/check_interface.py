"""Prevent old branding and unsupported identity telemetry from returning."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
patterns = [r'LRE-B4', r'Lemon\s*Racing', r'\bLRW\b', r'LRW_', r'lrw[_.]',
            r'GetLrw', r'securityIdentity', r'ECU identity', r'ECU Identity']
paths = [p for p in (root / 'src').iterdir()
         if p.suffix in ('.cpp', '.h', '.inc', '.rc', '.manifest')]
paths += [root / 'CMakeLists.txt', root / 'generate_icon.py']
errors = []
for p in paths:
    text = p.read_text(encoding='utf-8-sig')
    for pattern in patterns:
        if re.search(pattern, text):
            errors.append(f'{p.relative_to(root)}: {pattern}')
if errors:
    raise SystemExit('\n'.join(errors))
print('PASS neutral interface branding and absence of identity telemetry')
