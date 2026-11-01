#!/usr/bin/env python3
"""A fake MeshCore companion radio on a pseudo-terminal, for testing clients
without transmitting anything.

    fake_radio.py                       # prints the pty path, e.g. /dev/ttys012
    visor_bridge.py /dev/ttys012        # then bridge the Visor to it

It answers the companion protocol (layouts per MeshCore's MyMesh.cpp):
device/self info, clock, channels (Public, #test; unused slots carry stale
bytes like the real firmware), contacts of every type, queued messages behind
PUSH_MSG_WAITING, and sends: a DM gets SENT then PUSH_SEND_CONFIRMED after
~2 s (never, if its text contains "noack"), plus an echo reply. A channel
message from "Bob" arrives every --chat seconds.

Type at its prompt to inject messages:
    c <text>     channel message on Public from "Alice"
    d <text>     DM from the first contact
"""
import argparse
import os
import random
import select
import struct
import sys
import threading
import time

NAME = 'FakeRadio'
CHANNELS = {0: 'Public', 1: '#test'}
MAX_CHANNELS = 8
CONTACTS = [   # (key prefix byte, type, name, path_len)
    (0xA1, 1, 'Alice', 0x00),
    (0xA2, 1, 'Bob ☃', 0x41),          # snowman: a UTF-8 name
    (0xA3, 2, 'Hilltop Repeater', 0x42),
    (0xA4, 3, 'Rittenhouse Room', 0xFF),
    (0xA5, 4, 'Weather Sensor', 0x01),
]


def key_of(b):
    return bytes([b] * 32)


class Radio:
    def __init__(self, fd, chat_every):
        self.fd = fd
        self.lock = threading.Lock()
        self.queue = []              # pending SYNC_NEXT_MESSAGE frames (payloads)
        self.clock_offset = 0
        self.out_buf = bytearray()   # the firmware reuses one reply buffer: unused channel replies show its leftovers
        self.chat_every = chat_every

    def now(self):
        return int(time.time()) + self.clock_offset

    def send(self, payload):
        self.out_buf[:len(payload)] = payload
        with self.lock:
            os.write(self.fd, b'>' + struct.pack('<H', len(payload)) + payload)

    def ok(self):
        self.send(b'\x00')

    def push_waiting(self):
        self.send(b'\x83')

    def queue_channel(self, idx, sender, text, snr=24, path=0x02):
        body = f'{sender}: {text}'.encode()
        self.queue.append(bytes([17, snr & 0xFF, 0, 0, idx, path, 0]) + struct.pack('<I', self.now()) + body)
        self.push_waiting()

    def queue_dm(self, prefix_byte, text, snr=12, path=0xFF):
        self.queue.append(bytes([16, snr & 0xFF, 0, 0]) + bytes([prefix_byte] * 6) + bytes([path, 0])
                          + struct.pack('<I', self.now()) + text.encode())
        self.push_waiting()

    def handle(self, p):
        code = p[0]
        if code == 22:                                   # DEVICE_QUERY
            info = bytes([13, 13, 175, MAX_CHANNELS, 0, 0, 0, 0]) + b'1 Nov 2026'.ljust(12, b'\0')
            info += b'Fake Radio'.ljust(40, b'\0') + b'v1.17.1-fake'.ljust(20, b'\0') + b'\x00\x00'
            self.send(info)
        elif code == 1:                                  # APP_START
            info = bytes([5, 1, 22, 22]) + bytes([0x9B] * 32) + struct.pack('<ii', 39952600, -75165200)
            info += bytes([0, 0, 0, 0]) + struct.pack('<II', 919500, 500000) + bytes([10, 5]) + NAME.encode()
            self.send(info)
        elif code == 5:                                  # GET_DEVICE_TIME
            self.send(b'\x09' + struct.pack('<I', self.now()))
        elif code == 6:                                  # SET_DEVICE_TIME (forward only, like the firmware)
            t = struct.unpack('<I', p[1:5])[0]
            if t >= self.now():
                self.clock_offset += t - self.now()
                self.ok()
            else:
                self.send(b'\x01\x06')
        elif code == 31:                                 # GET_CHANNEL
            idx = p[1]
            if idx >= MAX_CHANNELS:
                self.send(b'\x01\x02')
                return
            name = CHANNELS.get(idx, '')
            reply = bytearray(self.out_buf[:50].ljust(50, b'\0'))   # stale bytes, as the firmware does
            reply[0], reply[1] = 18, idx
            enc = name.encode()
            reply[2:2 + len(enc) + 1] = enc + b'\0'
            if name:
                reply[2 + 32:50] = bytes(range(16))
            self.send(bytes(reply))
        elif code == 4:                                  # GET_CONTACTS
            self.send(b'\x02' + struct.pack('<I', len(CONTACTS)))
            for b, typ, name, path in CONTACTS:
                rec = bytes([3]) + key_of(b) + bytes([typ, 0, path]) + bytes(64)
                rec += name.encode()[:31].ljust(32, b'\0')
                rec += struct.pack('<IiiI', self.now() - 60 * b % 7200, 0, 0, self.now())
                self.send(rec)
            self.send(b'\x04' + struct.pack('<I', self.now()))
        elif code == 10:                                 # SYNC_NEXT_MESSAGE
            self.send(self.queue.pop(0) if self.queue else b'\x0a')
        elif code == 2:                                  # SEND_TXT_MSG
            prefix = p[7:13]
            text = p[13:].decode('utf-8', 'replace')
            ack = random.getrandbits(32) or 1
            self.send(bytes([6, 1]) + struct.pack('<II', ack, 3000))
            print(f'\n[DM to {prefix[:4].hex()}] {text}', flush=True)
            if 'noack' not in text:
                threading.Timer(2.0, lambda: self.send(b'\x82' + struct.pack('<II', ack, 1800))).start()
            threading.Timer(3.0, lambda: self.queue_dm(prefix[0], f'echo: {text}')).start()
        elif code == 3:                                  # SEND_CHANNEL_TXT_MSG
            print(f'\n[channel {p[2]}] {NAME}: {p[7:].decode("utf-8", "replace")}', flush=True)
            self.ok()
        elif code in (8, 11, 12, 61, 7, 38):             # settings, advert
            self.ok()
        elif code == 19:                                 # REBOOT: no reply
            print('\n[reboot requested]', flush=True)
        else:
            self.send(b'\x01\x01')                       # ERR unsupported

    def reader(self):
        buf = bytearray()
        while True:
            r, _, _ = select.select([self.fd], [], [], 0.2)
            if r:
                try:
                    buf += os.read(self.fd, 512)
                except OSError:
                    time.sleep(0.2)
                    continue
            while True:
                i = buf.find(b'<')
                if i < 0:
                    buf.clear()
                    break
                del buf[:i]
                if len(buf) < 3:
                    break
                n = buf[1] | (buf[2] << 8)
                if len(buf) < 3 + n:
                    break
                payload = bytes(buf[3:3 + n])
                del buf[:3 + n]
                if payload:
                    self.handle(payload)

    def chatter(self):
        lines = ['anyone on tonight?', 'testing from Fishtown', 'hello @[' + NAME + '] how is the Visor?',
                 'repeater on the hill is back up']
        while True:
            time.sleep(self.chat_every)
            self.queue_channel(0, 'Bob', random.choice(lines), snr=random.randint(-20, 40))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--chat', type=float, default=45, help='seconds between channel messages (0 = none)')
    args = ap.parse_args()

    master, slave = os.openpty()
    import termios
    attrs = termios.tcgetattr(slave)
    attrs[0] = attrs[1] = attrs[3] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    print(f'Fake radio on {os.ttyname(slave)}  (point visor_bridge.py or a client at it)', flush=True)

    radio = Radio(master, args.chat)
    radio.queue_channel(0, 'Alice', 'welcome to the fake mesh', snr=30)
    threading.Thread(target=radio.reader, daemon=True).start()
    if args.chat > 0:
        threading.Thread(target=radio.chatter, daemon=True).start()
    try:
        for line in sys.stdin:
            line = line.strip()
            if line.startswith('c '):
                radio.queue_channel(0, 'Alice', line[2:])
            elif line.startswith('d '):
                radio.queue_dm(CONTACTS[0][0], line[2:])
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
