#!/usr/bin/env python3
"""Bridge the Visor's USB data port to a MeshCore radio's serial port.

For testing the Palm client before the SB-LoRa boards exist: the app opens the
Visor's 'HsUd' Serial Manager driver (USB) and this forwards
bytes between it and the radio, logging each companion frame.

    visor_bridge.py /dev/cu.usbserial-XXXX     # radio on a USB-TTL adapter (D6/D7)
    visor_bridge.py /dev/cu.usbmodemXXXX       # radio's own USB companion link

Then put the Visor in its USB cradle and tap Connect with Link = USB.
The app's 'HsUd' driver turns out to send on the Visor's HOTSYNC port (2), so
the bridge also grabs the Visor during a HotSync: stop it before `make install`
(the Makefile does this for you).
Needs libusb-1.0 (brew install libusb); everything else is the standard library.
"""
import argparse
import ctypes
import ctypes.util
import os
import select
import sys
import termios
import threading
import time

VISOR_VID, VISOR_PID = 0x082D, 0x0100

# Visor vendor requests (as used by the Linux visor driver and pilot-link)
VISOR_REQ_TYPE_IN = 0xC2                 # device-to-host, vendor, endpoint
VISOR_GET_CONNECTION_INFORMATION = 0x03
VISOR_REQUEST_BYTES_AVAILABLE = 0x01
FUNCTIONS = {0: 'GENERIC', 1: 'DEBUGGER', 2: 'HOTSYNC', 3: 'CONSOLE', 4: 'REMOTE_FILE_SYS'}

LIBUSB_ERROR_TIMEOUT = -7
LIBUSB_ERROR_NO_DEVICE = -4
LIBUSB_ERROR_PIPE = -9

CODES_TO_RADIO = {1: 'APP_START', 2: 'SEND_TXT_MSG', 3: 'SEND_CHANNEL_TXT_MSG', 4: 'GET_CONTACTS',
                  10: 'SYNC_NEXT_MESSAGE', 22: 'DEVICE_QUERY'}
CODES_FROM_RADIO = {0: 'OK', 1: 'ERR', 2: 'CONTACTS_START', 3: 'CONTACT', 4: 'END_OF_CONTACTS',
                    5: 'SELF_INFO', 6: 'SENT', 10: 'NO_MORE_MESSAGES', 13: 'DEVICE_INFO',
                    16: 'CONTACT_MSG_V3', 17: 'CHANNEL_MSG_V3', 0x80: 'PUSH_ADVERT',
                    0x82: 'PUSH_SEND_CONFIRMED', 0x83: 'PUSH_MSG_WAITING'}


def load_libusb():
    path = ctypes.util.find_library('usb-1.0') or '/opt/homebrew/lib/libusb-1.0.dylib'
    lib = ctypes.CDLL(path)
    lib.libusb_init.argtypes = [ctypes.c_void_p]
    lib.libusb_open_device_with_vid_pid.restype = ctypes.c_void_p
    lib.libusb_open_device_with_vid_pid.argtypes = [ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint16]
    lib.libusb_close.argtypes = [ctypes.c_void_p]
    lib.libusb_claim_interface.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.libusb_release_interface.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.libusb_control_transfer.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint16,
                                            ctypes.c_uint16, ctypes.c_char_p, ctypes.c_uint16, ctypes.c_uint]
    lib.libusb_bulk_transfer.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_char_p, ctypes.c_int,
                                         ctypes.POINTER(ctypes.c_int), ctypes.c_uint]
    lib.libusb_clear_halt.argtypes = [ctypes.c_void_p, ctypes.c_uint8]
    lib.libusb_error_name.restype = ctypes.c_char_p
    lib.libusb_error_name.argtypes = [ctypes.c_int]
    if lib.libusb_init(None) != 0:
        sys.exit('libusb_init failed')
    return lib


def open_serial(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = attrs[1] = attrs[3] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


class FrameLogger:
    """Splits a byte stream into companion frames and prints one line per frame."""

    def __init__(self, label, start, names, quiet):
        self.label, self.start, self.names, self.quiet = label, start, names, quiet
        self.buf = bytearray()

    def feed(self, data):
        if self.quiet:
            return
        self.buf += data
        while True:
            i = self.buf.find(self.start)
            if i < 0:
                self.buf.clear()
                return
            del self.buf[:i]
            if len(self.buf) < 3:
                return
            n = self.buf[1] | (self.buf[2] << 8)
            if len(self.buf) < 3 + n:
                return
            body = bytes(self.buf[3:3 + n])
            del self.buf[:3 + n]
            code = body[0] if body else None
            name = self.names.get(code, f'0x{code:02x}' if code is not None else 'empty')
            shown = body[:24].hex(' ') + (' ...' if n > 24 else '')
            print(f'{time.strftime("%H:%M:%S")} {self.label} {name:<18} len={n:<3} {shown}', flush=True)


def wait_for_visor(lib):
    print('Waiting for the Visor (open VisorMesh, Link = USB, tap Connect)...', flush=True)
    while True:
        h = lib.libusb_open_device_with_vid_pid(None, VISOR_VID, VISOR_PID)
        if h:
            # macOS can hold a freshly enumerated device for a moment; retry the claim.
            for _ in range(20):
                rc = lib.libusb_claim_interface(h, 0)
                if rc == 0:
                    return h
                time.sleep(0.1)
            print(f'claim_interface failed: {lib.libusb_error_name(rc).decode()}', flush=True)
            lib.libusb_close(h)
        time.sleep(0.1)


def connection_ports(lib, h):
    """Asks the Visor which USB port number carries each function. Returns {port: function name}."""
    buf = ctypes.create_string_buffer(0x12)
    n = lib.libusb_control_transfer(h, VISOR_REQ_TYPE_IN, VISOR_GET_CONNECTION_INFORMATION, 0, 0, buf, 0x12, 2000)
    if n < 2:
        raise RuntimeError(f'GET_CONNECTION_INFORMATION failed: {n}')
    raw = buf.raw[:n]
    count = raw[0] | (raw[1] << 8)
    ports = {}
    for k in range(count):
        if 2 + 2 * k + 1 >= n:
            break
        func, port = raw[2 + 2 * k], raw[3 + 2 * k]
        ports[port] = FUNCTIONS.get(func, hex(func))
        print(f'  port {port}: {ports[port]}', flush=True)
    avail = ctypes.create_string_buffer(2)
    lib.libusb_control_transfer(h, VISOR_REQ_TYPE_IN, VISOR_REQUEST_BYTES_AVAILABLE, 0, 0, avail, 2, 2000)
    if not ports:
        raise RuntimeError('Visor reported no ports')
    return ports


def bridge(lib, h, ports, serial_fd, quiet):
    """
    Listens on every port's IN endpoint, since which one the app's driver uses
    isn't documented. Radio replies go out on the port the Visor last sent on.
    Stalled endpoints are cleared and retried rather than ending the session.
    """
    to_radio = FrameLogger('visor->radio', b'<', CODES_TO_RADIO, quiet)
    to_visor = FrameLogger('radio->visor', b'>', CODES_FROM_RADIO, quiet)
    gone = threading.Event()
    lock = threading.Lock()
    active = {'port': None}

    for port in ports:
        lib.libusb_clear_halt(h, 0x80 | port)
        lib.libusb_clear_halt(h, port)

    def stop(where, rc):
        with lock:
            if not gone.is_set() and rc != LIBUSB_ERROR_NO_DEVICE:
                print(f'{where}: {lib.libusb_error_name(rc).decode()} ({rc})', flush=True)
            gone.set()

    def usb_to_serial(port):
        ep_in = 0x80 | port
        buf = ctypes.create_string_buffer(512)
        got = ctypes.c_int(0)
        stalls = 0
        while not gone.is_set():
            rc = lib.libusb_bulk_transfer(h, ep_in, buf, len(buf), ctypes.byref(got), 200)
            if got.value:
                if active['port'] != port:
                    active['port'] = port
                    print(f'Visor is sending on port {port} ({ports[port]})', flush=True)
                data = buf.raw[:got.value]
                os.write(serial_fd, data)
                to_radio.feed(data)
            if rc == LIBUSB_ERROR_PIPE:
                stalls += 1
                if stalls in (10, 100):
                    print(f'bulk read 0x{ep_in:02x} stalled ({stalls}x), clearing', flush=True)
                lib.libusb_clear_halt(h, ep_in)
                time.sleep(0.05)
            elif rc not in (0, LIBUSB_ERROR_TIMEOUT):
                stop(f'bulk read 0x{ep_in:02x}', rc)

    readers = [threading.Thread(target=usb_to_serial, args=(p,), daemon=True) for p in ports]
    for t in readers:
        t.start()
    got = ctypes.c_int(0)
    while not gone.is_set():
        r, _, _ = select.select([serial_fd], [], [], 0.2)
        if not r:
            continue
        data = os.read(serial_fd, 512)
        to_visor.feed(data)
        port = active['port']
        if port is None:
            print('radio sent data before the Visor did; dropped', flush=True)
            continue
        rc = lib.libusb_bulk_transfer(h, port, data, len(data), ctypes.byref(got), 2000)
        if rc == LIBUSB_ERROR_PIPE:
            lib.libusb_clear_halt(h, port)
            rc = lib.libusb_bulk_transfer(h, port, data, len(data), ctypes.byref(got), 2000)
        if rc != 0:
            stop(f'bulk write 0x{port:02x}', rc)
    for t in readers:
        t.join(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('radio', help='serial port of the MeshCore companion radio')
    ap.add_argument('--quiet', action='store_true', help="don't log frames")
    args = ap.parse_args()

    lib = load_libusb()
    serial_fd = open_serial(args.radio)
    try:
        while True:
            h = wait_for_visor(lib)
            print('Visor connected', flush=True)
            try:
                ports = connection_ports(lib, h)
                print(f'Bridging Visor USB ports {sorted(ports)} <-> {args.radio}', flush=True)
                bridge(lib, h, ports, serial_fd, args.quiet)
            except RuntimeError as e:
                print(e, flush=True)
            finally:
                lib.libusb_release_interface(h, 0)
                lib.libusb_close(h)
            print('Visor disconnected', flush=True)
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        os.close(serial_fd)


if __name__ == '__main__':
    main()
