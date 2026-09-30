"""Explicit discovery of the private, unchanged TU5JP reference workspace."""
from pathlib import Path
import hashlib
import os
import sys

ROM_SHA256 = '5710015f7c5c066c860a1757fd893f305701608c25af8ff23bfcb4fd1e4837b3'


def oem_repo():
    value = os.environ.get('TU744_OEM_REPO')
    for i, arg in enumerate(sys.argv):
        if arg == '--oem-repo':
            if i + 1 == len(sys.argv):
                raise SystemExit('--oem-repo needs a directory')
            value = sys.argv[i + 1]
            del sys.argv[i:i + 2]
            break
        if arg.startswith('--oem-repo='):
            value = arg.split('=', 1)[1]
            del sys.argv[i]
            break
    if not value:
        raise SystemExit('Set TU744_OEM_REPO or pass --oem-repo pointing to the reverse-engineering workspace')
    root = Path(value).resolve()
    rom = root / 'bins/M744_C167_FULL.bin'
    if not rom.is_file() or not (root / 'src/c167re').is_dir():
        raise SystemExit(f'OEM workspace missing TU5JP ROM or emulator: {root}')
    if hashlib.sha256(rom.read_bytes()).hexdigest() != ROM_SHA256:
        raise SystemExit('OEM TU5JP reference image hash mismatch')
    os.environ['TU744_OEM_REPO'] = str(root)
    return root
