"""Put a schema-5 tune into the stock-95080 build's reserved NOR slot.

Creates a new image; performs no ECU I/O. Firmware still validates every tune
field on boot. The input must be the unmodified stock profile build artifact.
"""
from pathlib import Path
import argparse
import binascii
import hashlib
import json
ROOT=Path(__file__).resolve().parents[1]


def pack(image, tune, manifest):
    if manifest.get('profile')!='stock-95080' or len(image)!=524288:
        raise ValueError('stock-95080 build and 512 KiB image required')
    if hashlib.sha256(image).hexdigest()!=manifest['binary_sha256']:
        raise ValueError('input firmware does not match the build manifest')
    if len(tune)!=3072 or tune[0x900:0x904]!=b'LR\0\5':
        raise ValueError('3072-byte schema-5 calibration required')
    if image[0x50000:0x70000]!=b'\xff'*0x20000:
        raise ValueError('calibration sectors must be blank in the build artifact')
    header=bytearray(b'\xff'*32)
    header[:4]=b'LRC3'
    header[4:6]=(5).to_bytes(2,'big')
    header[6:8]=(3072).to_bytes(2,'big')
    header[8:12]=(1).to_bytes(4,'big')
    header[12:14]=binascii.crc_hqx(tune,0xffff).to_bytes(2,'big')
    header[14:16]=(1).to_bytes(2,'big')
    header[20:22]=binascii.crc_hqx(header[:20],0xffff).to_bytes(2,'big')
    header[31]=0xa5
    out=bytearray(image)
    out[0x50000:0x50c20]=header+tune
    return bytes(out)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('tune',type=Path)
    p.add_argument('output',type=Path)
    p.add_argument('--build',type=Path,default=ROOT/'build/stock-95080')
    a=p.parse_args()
    manifest=json.loads((a.build/'manifest.json').read_text())
    tune=a.tune.read_bytes()
    image=pack((a.build/'TU5JP_STOCK95080_EXPERIMENTAL.bin').read_bytes(),tune,manifest)
    with a.output.open('xb') as f:f.write(image)
    print(json.dumps(dict(output=str(a.output),sha256=hashlib.sha256(image).hexdigest(),
                          tune_sha256=hashlib.sha256(tune).hexdigest(),
                          calibration_schema=5,full_calibration_validation='performed by ECU on boot'),indent=2))
