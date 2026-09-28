"""Report the real C167 bootstrap reply through a KKL/serial adapter.

Sends exactly one 0x00 autobaud byte after a manual reset prompt. No stage
loader, erase command or firmware data is sent. Reset the ECU again before
another probe or an upload. Requires pyserial; importing/help opens no port.
"""
import argparse
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--baud', type=int, default=19200)
    parser.add_argument('--dtr', choices=('on', 'off'), default='on')
    parser.add_argument('--rts', choices=('on', 'off'), default='on')
    args = parser.parse_args()
    import serial
    connection = serial.Serial(port=None, baudrate=args.baud, bytesize=8,
                               parity='N', stopbits=1, timeout=0.05,
                               write_timeout=1, xonxoff=False, rtscts=False,
                               dsrdtr=False)
    connection.port = args.port
    connection.dtr = args.dtr == 'on'
    connection.rts = args.rts == 'on'
    try:
        connection.open()
        print(f'{args.port}: {args.baud} baud, 8N1, no flow control; '
              f'DTR={args.dtr}, RTS={args.rts}')
        input('Perform a fresh hardware reset into boot mode, then press Enter: ')
        stale = connection.read(connection.in_waiting)
        if stale:
            print(f'Before probe: {len(stale)} byte(s): {stale.hex(" ")}')
        sent = connection.write(b'\x00')
        print(f'Transmitted: {sent} byte(s): 00', flush=True)
        received = bytearray()
        start = time.monotonic()
        while time.monotonic() - start < 1.5:
            part = connection.read(64)
            if part:
                received.extend(part)
                print(f'+{(time.monotonic()-start)*1000:.1f} ms: '
                      f'{len(part)} byte(s): {part.hex(" ")}', flush=True)
        print(f'Total received: {len(received)} byte(s): '
              f'{received.hex(" ") if received else "<none>"}')
        if received == b'\x00\xc5':
            print('Expected echo plus C167 boot ID. The legacy uploader handshake should match.')
            result = 0
        elif received == b'\xc5':
            print('C167 boot ID without echo. This legacy uploader rejects this framing.')
            result = 0
        elif bytes(received) in (b'\xd5', b'\x00\xd5', b'\xa5', b'\x00\xa5'):
            print('Alternate bootstrap ID. The legacy uploader only accepts 00 C5; '
                  'loader compatibility needs checking before changing that check.')
            result = 0
        elif received == b'\x00':
            print('One zero, consistent with K-line echo only; no C167 boot ID received.')
            result = 2
        elif not received:
            print('No received bytes, including no echo. Check adapter power, routing and serial configuration.')
            result = 2
        else:
            print('Unexpected response; retain the exact bytes for diagnosis.')
            result = 2
        print('No loader or firmware was sent. Reset again before probing or uploading.')
        return result
    except (serial.SerialException, OSError) as error:
        print(f'Serial error: {error}')
        return 1
    finally:
        connection.close()


if __name__ == '__main__':
    raise SystemExit(main())
