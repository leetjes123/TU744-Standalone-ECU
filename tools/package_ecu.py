"""Package the ordinary stock-M95080 image with an explicitly supplied tune.

Verifies build identity, runs the actual native calibration validator, packs NOR
slot 0 and round-trips the complete HEX/BIN. Local files only; no ECU interface.
"""
from pathlib import Path
import argparse
import hashlib
import json
from artifacts import require, verify_firmware, programmed, binary_image
from convert_lre_b4 import native_client
from pack_stock_tune import pack
import re

ROOT=Path(__file__).resolve().parents[1]


def record(kind, address, payload=b''):
    data=bytes([len(payload)])+address.to_bytes(2,'big')+bytes([kind])+payload
    return ':'+(data+bytes([-sum(data)&255])).hex().upper()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tune',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    build=ROOT/'build/stock-95080'
    verify_firmware(ROOT,build,'stock-95080')
    manifest=json.loads((build/'manifest.json').read_text())
    tune=args.tune.read_bytes()
    client=native_client();client.activate(tune)
    require(client.read_tune()==tune, 'native calibration readback mismatch')
    firmware=(build/'TU5JP_STOCK95080_EXPERIMENTAL.bin').read_bytes()
    packed=pack(firmware,tune,manifest)
    args.output.mkdir(parents=True,exist_ok=True)
    identity=(ROOT/'include/identity.h').read_text()
    name=re.search(r'#define TU744_ECU_NAME "([^"]+)"',identity)[1]
    version=re.search(r'#define TU744_FIRMWARE_VERSION "([^"]+)"',identity)[1]
    stem=f'{name}_{version}_M95080'
    binary=args.output/(stem+'.bin')
    binary.write_bytes(packed)
    lines=(build/'TU5JP.H86').read_text().splitlines()
    require(lines[-1]==record(1,0), 'unexpected firmware EOF record')
    lines=lines[:-1]+[record(4,0,b'\0\5')]
    for at in range(0,0xc20,16):
        lines.append(record(0,at,packed[0x50000+at:0x50000+at+16]))
    lines.append(record(1,0))
    hex_file=args.output/(stem+'.hex')
    hex_file.write_text('\n'.join(lines)+'\n')
    require(binary_image(programmed(hex_file,0x70000))==packed, 'HEX/BIN roundtrip mismatch')
    (args.output/'LRE-B4-1.6Basemap-schema4.bin').write_bytes(tune)
    (args.output/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    result=dict(ecu_name=name,firmware_version=version,profile='stock-95080',target='TU5JP Bosch M7.4.4, stock M95080',
        engine_outputs_enabled=True,special_test_behavior=False,flash_bytes=len(packed),
        tune_slot=0,tune_offset='0x50020',calibration_schema=4,
        base_firmware_sha256=manifest['binary_sha256'],
        binary_sha256=hashlib.sha256(packed).hexdigest(),
        hex_sha256=hashlib.sha256(hex_file.read_bytes()).hexdigest(),
        tune_sha256=hashlib.sha256(tune).hexdigest(),
        native_calibration_validation=True,hex_bin_roundtrip=True,
        physical_hardware_validation=False,ecu_programming_performed=False)
    (args.output/'image-manifest.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
