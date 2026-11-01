#!/usr/bin/env python3
"""Bench tool: send companion-protocol frames to a MeshCore radio and dump the replies.

Uses only the standard library (termios), so it runs anywhere with Python 3.

    companion_query.py /dev/cu.usbmodemXXXX                 # CMD_DEVICE_QUERY
    companion_query.py /dev/cu.usbmodemXXXX 16 03           # same, payload given as hex bytes
    companion_query.py /dev/cu.usbserial-XXXX --baud 115200 # via a USB-TTL adapter on D6/D7

Frames: app -> radio '<' len_lo len_hi payload; radio -> app '>' len_lo len_hi payload.
"""
import argparse
import os
import select
import sys
import termios
import time

BAUDS = {9600: termios.B9600, 19200: termios.B19200, 38400: termios.B38400,
         57600: termios.B57600, 115200: termios.B115200}


def open_port(path, baud):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0                                   # iflag: raw
    attrs[1] = 0                                   # oflag: raw
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0                                   # lflag: no echo, non-canonical
    attrs[4] = attrs[5] = BAUDS[baud]
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def read_frames(fd, timeout):
    """Yields (payload, raw_bytes) for each '>' frame until timeout passes with no data."""
    buf = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            buf += os.read(fd, 512)
            deadline = time.monotonic() + timeout
        while True:
            start = buf.find(b'>')
            if start < 0:
                buf.clear()
                break
            del buf[:start]
            if len(buf) < 3:
                break
            n = buf[1] | (buf[2] << 8)
            if len(buf) < 3 + n:
                break
            yield bytes(buf[3:3 + n]), bytes(buf[:3 + n])
            del buf[:3 + n]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('port')
    ap.add_argument('payload', nargs='*', default=['16', '03'], help='payload bytes in hex (default: 16 03)')
    ap.add_argument('--baud', type=int, default=115200, choices=sorted(BAUDS))
    ap.add_argument('--timeout', type=float, default=2.0, help='seconds of silence before stopping')
    args = ap.parse_args()

    payload = bytes(int(b, 16) for b in args.payload)
    frame = b'<' + len(payload).to_bytes(2, 'little') + payload
    fd = open_port(args.port, args.baud)
    try:
        os.write(fd, frame)
        print(f'TX {frame.hex(" ")}')
        count = 0
        for body, raw in read_frames(fd, args.timeout):
            count += 1
            code = body[0] if body else None
            print(f'RX code=0x{code:02x} len={len(body)}  {raw.hex(" ")}')
            text = bytes(c if 32 <= c < 127 else 46 for c in body[1:]).decode()
            print(f'   ascii: {text}')
        if not count:
            print('no reply')
            sys.exit(1)
    finally:
        os.close(fd)


if __name__ == '__main__':
    main()
