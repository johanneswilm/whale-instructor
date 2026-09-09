#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Whale Instructor native CLI for the WhalesBot MC101s controller
(sold as E7 Pro / AI S1).

USB HID, VID:PID 2018:5750, no report IDs. 64-byte reports; hidraw writes
carry a leading 0x00 report-ID marker byte, reads return the raw 64 bytes.

Protocol (reverse engineered from USB captures; see the repo docs):

  Detection probe (7 bytes on the wire):
    01 <seq> 00 02 0b 00 <cs>          ->  02 <seq> 00 08 0b 00 "MC1102" <cs>

  Live motor command (10 bytes, executed immediately, echoed as ACK):
    01 <ctr:u16le> 05 03 00 10 <port> <speed:i8> <cs>
    port: A=1 B=2 C=3 D=4; speed -100..+100; 0 = stop.
    Counter and seq are free-running and not validated by the device;
    the checksum is.

  Program upload to slot 1..3:
    0b <seq> 00 0f 02 <tok> 01 00 <slot> "APP_<slot>" <cs>
    0e <seq> <blk> 30 <48-byte payload> <cs>      (many; ~84 ms/packet)
    0e <seq> <lastblk> 08 <8 zero bytes> <cs>     (end-of-data marker)
    11 <seq+2> <lastblk> 0a 02 "APP_<slot>.bin" <cs>   (finalizes + runs)
    ACKs: 02 <seq> <blk> 00 <cs>

  Checksum everywhere: cs = ~sum(all preceding frame bytes) & 0xff.
"""
import argparse
import os
import struct
import sys
import time

try:
    import hid as _hidapi
except ImportError:
    _hidapi = None

VID = 0x2018
PID = 0x5750
REPORT_LEN = 64
UPLOAD_TOKEN = b'\xd8\x43'
BLOCK0_PACKETS = 254
BLOCK_PACKETS = 256
PAYLOAD_LEN = 48
# Vendor app paces at ~2 ms/packet (measured from bridge logs, 2026-09-06):
# it writes, reads the ACK, and immediately continues — no inter-packet sleep.
# The old 42 ms "vendor pacing" note was a misread capture. We wait for each
# ACK anyway (~ms device turnaround), which is the natural pacemaker.
UPLOAD_PACKET_DT = 0


def checksum(frame):
    """~sum of all bytes preceding the checksum byte."""
    return (~sum(frame)) & 0xFF


def find_hidraw():
    """Return the /dev/hidrawN path for the Whalesbot controller."""
    base = '/sys/class/hidraw'
    for name in sorted(os.listdir(base)):
        try:
            with open(os.path.join(base, name, 'device', 'uevent')) as f:
                uevent = f.read()
        except OSError:
            continue
        for line in uevent.splitlines():
            if line.startswith('HID_ID='):
                parts = line.split(':')[1:]
                if len(parts) >= 2 and int(parts[0], 16) == VID and int(parts[1], 16) == PID:
                    return '/dev/' + name
    raise OSError('Whale Instructor controller (2018:5750) not found')


class _HidrawDevice:
    """Linux transport: direct /dev/hidraw access (the device-validated
    path; no third-party dependencies)."""

    def __init__(self, path):
        self.path = path
        self.fd = None

    def open(self):
        self.fd = os.open(self.path, os.O_RDWR | os.O_NONBLOCK)
        try:
            while os.read(self.fd, 4096):
                pass
        except BlockingIOError:
            pass

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def write(self, frame):
        report = frame + b'\x00' * (REPORT_LEN - len(frame))
        os.write(self.fd, b'\x00' + report)

    def read(self, timeout=0.5):
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                data = os.read(self.fd, REPORT_LEN)
            except BlockingIOError:
                time.sleep(0.002)
                continue
            if data:
                return data
        return None


class _HidapiDevice:
    """Windows/macOS transport: hidapi bindings (the `hid` package —
    prebuilt wheels on both platforms, no compiler needed)."""

    def __init__(self):
        if _hidapi is None:
            raise SystemExit(
                'USB support on Windows/macOS needs the `hid` package — '
                'install whale-instructor with pip (it pulls `hid` in '
                'automatically there) or run: pip install hid')
        entries = sorted(_hidapi.enumerate(VID, PID),
                         key=lambda e: e.get('interface_number', 0))
        if not entries:
            raise OSError('Whale Instructor controller (2018:5750) not found')
        self.path = entries[0]['path']
        self.dev = None

    def open(self):
        self.dev = _hidapi.device()
        self.dev.open_path(self.path)
        self.dev.set_nonblocking(0)

    def close(self):
        if self.dev is not None:
            self.dev.close()
            self.dev = None

    def write(self, frame):
        report = frame + b'\x00' * (REPORT_LEN - len(frame))
        if self.dev.write(b'\x00' + report) < 0:
            raise IOError('USB write failed')

    def read(self, timeout=0.5):
        data = self.dev.read(REPORT_LEN, timeout_ms=int(timeout * 1000))
        return bytes(data) or None


def _open_transport(path=None):
    """Pick the transport for this platform: hidraw on Linux (proven,
    zero-dep), hidapi elsewhere."""
    if path is not None:
        return _HidrawDevice(path)
    if sys.platform.startswith('linux'):
        return _HidrawDevice(find_hidraw())
    return _HidapiDevice()


class Controller:
    def __init__(self, path=None):
        self._dev = _open_transport(path)
        self.seq = 0x00
        self.ctr = 0x0000

    # -- transport ---------------------------------------------------------
    def open(self):
        self._dev.open()

    def close(self):
        self._dev.close()

    def _write(self, frame):
        self._dev.write(frame)

    def _read(self, timeout=0.5):
        return self._dev.read(timeout)

    def _next_seq(self):
        self.seq = (self.seq + 1) & 0xFF
        return self.seq

    # -- detection ---------------------------------------------------------
    def probe(self, timeout=1.0):
        """Send the detection probe, return the controller model string."""
        seq = self._next_seq()
        frame = bytearray(b'\x01\x00\x00\x02\x0b\x00')
        frame[1] = seq
        frame.append(checksum(bytes(frame)))
        self._write(bytes(frame))
        resp = self._read(timeout)
        if resp is None:
            raise IOError('no response to detection probe')
        if resp[0] != 0x02 or resp[1] != seq or resp[3] != 0x08 or resp[4] != 0x0b:
            raise IOError(f'unexpected probe response: {resp[:16].hex()}')
        n = resp[3] - 2
        name = resp[6:6 + n]
        if checksum(resp[:5 + len(name) + 1]) != resp[5 + len(name) + 1]:
            raise IOError('bad checksum in probe response')
        return name.decode('ascii', 'replace')

    # -- live motor control --------------------------------------------------
    def motor(self, port, speed, wait_ack=True):
        """Drive a motor. port 'A'..'D', speed -100..100 (0 = stop)."""
        if isinstance(port, str):
            port = ord(port.upper()) - ord('A') + 1
        if not 1 <= port <= 4:
            raise ValueError('port must be A..D')
        speed = max(-100, min(100, int(speed)))
        self.ctr = (self.ctr + 1) & 0xFFFF
        frame = bytearray(10)
        frame[0] = 0x01
        struct.pack_into('<H', frame, 1, self.ctr)
        frame[3] = 0x05
        frame[4] = 0x03
        frame[5] = 0x00
        frame[6] = 0x10
        frame[7] = port
        frame[8] = speed & 0xFF
        frame[9] = checksum(frame[:9])
        self._write(bytes(frame))
        if not wait_ack:
            return None
        echo = self._read(0.3)
        if echo is None:
            raise IOError(f'no echo for motor command (port {port}, speed {speed})')
        if echo[0] != 0x02 or echo[1:9] != frame[1:9]:
            raise IOError(f'motor command rejected: {echo[:16].hex()}')
        return True

    # -- live sensor reads ---------------------------------------------------
    # Verified op codes: 0x01 = IR (analog, 12-bit), 0x04 = touch switch.
    # The other u16 op codes come from the vendor web IDE's sensor table
    # (the same family the Bluetooth link uses) — whether a read returns
    # anything sensible depends on what is actually plugged in.
    SENSOR_OPS = {'ir': 0x01, 'gray': 0x02, 'touch': 0x04, 'light': 0x05,
                  'sound': 0x07, 'flame': 0x08, 'magnetic': 0x0c,
                  'ultrasonic': 0x0f, 'color': 0x15, 'encoder': 0x1e}
    SIGNED_OPS = {0x1e}          # encoder counts can be negative

    def read_sensor(self, kind, port):
        """Read a live sensor. kind in SENSOR_OPS, port 1..5 (motor port
        number for 'encoder')."""
        op = self.SENSOR_OPS[kind]
        self.ctr = (self.ctr + 1) & 0xFFFF
        frame = bytearray(9)
        frame[0] = 0x01
        struct.pack_into('<H', frame, 1, self.ctr)
        frame[3] = 0x04
        frame[4] = 0x03
        frame[5] = 0x00
        frame[6] = op
        frame[7] = port
        frame[8] = checksum(frame[:8])
        self._write(bytes(frame))
        t0 = time.time()
        while time.time() - t0 < 0.4:
            resp = self._read(0.1)
            if resp is None:
                continue
            if resp[0] == 0x02 and resp[6] == op and resp[7] == port:
                if len(resp) >= 11 and resp[10] == checksum(resp[:10]):
                    return int.from_bytes(resp[8:10], 'little',
                                          signed=op in self.SIGNED_OPS)
                raise IOError(f'bad checksum in sensor response: {resp[:12].hex()}')
        raise IOError(f'no response reading {kind} on port {port}')

    def stop_all(self, ports='ABCD'):
        for p in ports:
            self.motor(p, 0)

    # -- program upload ------------------------------------------------------
    def upload(self, image, slot=1, progress=None):
        """Upload a program image (ARM Thumb, APP_N.bin) to slot 1..3.

        Framing (verified against app captures): 48-byte full packets plus a
        FINAL PARTIAL packet carrying only the remaining bytes (len byte =
        remainder, no padding, no end marker). Blocks are seq-aligned: block 0
        runs from the seq after the 0b frame to the seq wrap (254 packets when
        the 0b frame uses seq 1), later blocks 256 packets, last block holds
        the remainder.
        """
        if slot not in (1, 2, 3):
            raise ValueError('slot must be 1..3 (P1..P3)')
        chunks = [image[i:i + PAYLOAD_LEN]
                  for i in range(0, len(image), PAYLOAD_LEN)]
        # seq-aligned block split: 0b frame uses seq 1 -> data starts at seq 2,
        # block 0 ends at seq 0xff (254 packets), then 256 per block.
        blocks = []
        i = 0
        first = True
        while i < len(chunks):
            n = BLOCK0_PACKETS if first else BLOCK_PACKETS
            blocks.append(chunks[i:i + n])
            i += n
            first = False
        last_blk = len(blocks) - 1

        # pre-upload command
        seq = self._next_seq()
        name = f'APP_{slot}'.encode()
        frame = bytearray(b'\x0b\x00\x00\x0f\x02' + UPLOAD_TOKEN + b'\x01\x00')
        frame[1] = seq
        frame.append(slot)
        frame += name
        frame.append(checksum(frame))
        self._write(bytes(frame))
        self._expect_ack(seq, 0x00)

        # data packets; seq is global and wraps
        total = len(chunks)
        done = 0
        for blk, pkts in enumerate(blocks):
            for j, payload in enumerate(pkts):
                seq = self._next_seq()
                partial = (blk == last_blk and j == len(pkts) - 1
                           and len(payload) < PAYLOAD_LEN)
                ln = len(payload)
                frame = bytearray(b'\x0e\x00\x00\x30')
                frame[1] = seq
                frame[2] = blk
                if partial:
                    frame[3] = ln
                frame += payload
                frame.append(checksum(frame))
                self._write(bytes(frame))
                self._expect_ack(seq, blk)
                if UPLOAD_PACKET_DT:
                    time.sleep(UPLOAD_PACKET_DT)
                done += 1
                if progress and done % 32 == 0:
                    progress(done, total)

        # finalize (golden trace shows seq skipping +2 here)
        seq = (self._next_seq() + 1) & 0xFF
        self.seq = seq
        fname = f'APP_{slot}.bin'.encode()
        frame = bytearray(b'\x11\x00\x00\x0a\x02')
        frame[1] = seq
        frame[2] = last_blk
        frame += fname
        frame.append(checksum(frame))
        self._write(bytes(frame))
        self._expect_ack(seq, last_blk)
        if progress:
            progress(total, total)

    def _expect_ack(self, seq, blk, timeout=5.0):
        t0 = time.time()
        while time.time() - t0 < timeout:
            resp = self._read(0.2)
            if resp is None:
                continue
            if resp[0] == 0x02 and resp[1] == seq and resp[3] == 0x00:
                if len(resp) >= 5 and resp[4] == checksum(resp[:4]):
                    return resp
                raise IOError(f'bad checksum in ACK: {resp[:8].hex()}')
            # stray echo/other frame: ignore and keep waiting
        raise IOError(f'timeout waiting for ACK seq={seq:#04x}')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('probe', help='detect controller, print model')
    m = sub.add_parser('motor', help='drive a motor (0 speed = stop)')
    m.add_argument('port', help='A, B, C or D')
    m.add_argument('speed', type=int, help='-100..100')
    sub.add_parser('motors-stop', help='stop all motors')
    s = sub.add_parser('sensor', help='read a live sensor')
    s.add_argument('kind', choices=['ir', 'gray', 'touch', 'light', 'sound', 'flame', 'magnetic', 'ultrasonic', 'color', 'encoder'])
    s.add_argument('port', type=int, help='sensor port 1..5')
    f = sub.add_parser('sensor-watch', help='read a sensor repeatedly')
    f.add_argument('kind', choices=['ir', 'gray', 'touch', 'light', 'sound', 'flame', 'magnetic', 'ultrasonic', 'color', 'encoder'])
    f.add_argument('port', type=int, help='sensor port 1..5')
    u = sub.add_parser('upload', help='upload a program image to a slot')
    u.add_argument('file', help='APP_N.bin image')
    u.add_argument('slot', nargs='?', type=int, default=1, help='1..3 (default 1)')
    u.add_argument('--quiet', action='store_true')
    args = ap.parse_args(argv)

    bot = Controller()
    bot.open()
    try:
        if args.cmd == 'probe':
            print(bot.probe())
        elif args.cmd == 'motor':
            bot.motor(args.port, args.speed)
        elif args.cmd == 'motors-stop':
            bot.stop_all()
        elif args.cmd == 'sensor':
            print(bot.read_sensor(args.kind, args.port))
        elif args.cmd == 'sensor-watch':
            while True:
                print(bot.read_sensor(args.kind, args.port), flush=True)
                time.sleep(0.2)
        elif args.cmd == 'upload':
            image = open(args.file, 'rb').read()
            if not args.quiet:
                def progress(done, total):
                    print(f'\r{done}/{total} packets', end='', flush=True)
            else:
                progress = None
            bot.upload(image, args.slot, progress)
            if not args.quiet:
                print()
    finally:
        bot.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
