"""Read-only standalone identification probe with raw serial diagnostics.

Close TunerPro before running. Uses the plugin's default DTR/RTS settings.
Sends only protocol capability command 0x20; never writes or saves a tune.
"""
import argparse
import time
from tune_client import frame


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port')
    parser.add_argument('--dtr', choices=('on', 'off'), default='off')
    parser.add_argument('--rts', choices=('on', 'off'), default='off')
    args = parser.parse_args()
    import serial
    connection = serial.Serial(port=None, baudrate=19200, timeout=0.02,
                               write_timeout=1, xonxoff=False, rtscts=False, dsrdtr=False)
    connection.port = args.port
    connection.dtr = args.dtr == 'on'
    connection.rts = args.rts == 'on'
    request = frame([0x20])
    try:
        connection.open()
        print(f'{args.port}: 19200 baud, 8N1, DTR={args.dtr}, RTS={args.rts}', flush=True)
        connection.reset_input_buffer()
        sent = connection.write(request)
        print(f'TX ({sent}): {request.hex(" ")}', flush=True)
        received = bytearray()
        started = time.monotonic()
        while time.monotonic() - started < 2:
            part = connection.read(256)
            if part:
                received.extend(part)
                print(f'+{1000*(time.monotonic()-started):.1f} ms RX: {part.hex(" ")}', flush=True)
            if len(received) > 256:
                print('Receive bound exceeded.')
                return 2
        if not received:
            print('No bytes received, including no adapter echo.')
            return 2
        if received == request:
            print('Adapter echo only; no ECU reply. Check ECU power and normal application boot.')
            return 2
        response = bytes(received[len(request):] if received.startswith(request) else received)
        if len(response) == 13 and response[:4] == b'\x55\x0a\x03\x04' and sum(response[1:-1]) & 255 == response[-1]:
            print('PASS: ECU replied with protocol 3 / schema 4 capabilities.')
            return 0
        print(f'Unexpected or incomplete ECU response: {response.hex(" ")}')
        return 2
    except (serial.SerialException, OSError) as error:
        print(f'Serial error: {error}')
        return 1
    finally:
        connection.close()


if __name__ == '__main__':
    raise SystemExit(main())
