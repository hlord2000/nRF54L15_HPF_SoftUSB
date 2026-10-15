#!/usr/bin/env python3
"""Deterministic native/reference differential tests (no hardware access)."""
import argparse
import ctypes as ct
import importlib.util
from pathlib import Path
import random
import sys

# Keep generated products inside tests/.build, including no reference pycache.
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('reference_usb', ROOT / 'tools/reference_usb.py')
ref = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ref)
U8 = ct.c_uint8
SIZE = ct.c_size_t
PTR = ct.POINTER(U8)
ERRORS = ['ok', 'argument', 'capacity', 'pid', 'length', 'crc', 'sync', 'stuff',
          'eop', 'line', 'unsupported', 'state']


class Packet(ct.Structure):
    _fields_ = [('kind', ct.c_int), ('pid', U8), ('address', U8), ('endpoint', U8),
                ('frame', ct.c_uint16), ('payload', PTR), ('payload_len', SIZE)]


class Native:
    def __init__(self, library):
        self.lib = ct.CDLL(str(library))
        for name, signature, result in [
            ('crc5', [ct.c_uint16], U8),
            ('crc16', [PTR, SIZE], ct.c_uint16),
            ('pid_byte', [U8], U8),
            ('pid_valid', [U8], ct.c_bool),
            ('build_token', [U8, U8, U8, PTR, SIZE, ct.POINTER(SIZE)], ct.c_int),
            ('build_sof', [ct.c_uint16, PTR, SIZE, ct.POINTER(SIZE)], ct.c_int),
            ('build_data', [ct.c_bool, U8, PTR, SIZE, PTR, SIZE, ct.POINTER(SIZE)], ct.c_int),
            ('build_handshake', [U8, PTR, SIZE, ct.POINTER(SIZE)], ct.c_int),
            ('parse_packet', [ct.c_bool, PTR, SIZE, ct.POINTER(Packet)], ct.c_int),
            ('encode', [ct.c_bool, PTR, SIZE, PTR, SIZE, ct.POINTER(SIZE)], ct.c_int),
            ('decode', [ct.c_bool, PTR, SIZE, PTR, SIZE, ct.POINTER(SIZE)], ct.c_int),
            ('encode_packet', [ct.c_bool, U8, PTR, SIZE, PTR, SIZE, ct.POINTER(SIZE)], ct.c_int),
        ]:
            fn = getattr(self.lib, 'softusb_' + name)
            fn.argtypes, fn.restype = signature, result

    @staticmethod
    def buf(data):
        return (U8 * len(data))(*data)

    def build(self, ls, pid, payload):
        out, n = (U8 * 1026)(), SIZE(123)
        p = self.buf(payload)
        if pid in ref.TOKEN:
            status = self.lib.softusb_build_token(pid, *payload, out, 1026, ct.byref(n))
        elif pid == ref.SOF:
            status = self.lib.softusb_build_sof(int.from_bytes(payload, 'little'), out, 1026, ct.byref(n))
        elif pid in ref.DATA:
            status = self.lib.softusb_build_data(ls, pid, p, len(payload), out, 1026, ct.byref(n))
        else:
            status = self.lib.softusb_build_handshake(pid, out, 1026, ct.byref(n))
        assert status == 0, ERRORS[status]
        return bytes(out[:n.value])

    def parse(self, ls, packet):
        p, raw = Packet(), self.buf(packet)
        status = self.lib.softusb_parse_packet(ls, raw, len(packet), ct.byref(p))
        if status:
            return ERRORS[status], None
        result = {'kind': p.kind, 'pid': p.pid}
        if p.kind == 0:
            result.update(address=p.address, endpoint=p.endpoint)
        elif p.kind == 1:
            result['payload'] = ct.string_at(p.payload, p.payload_len)
        elif p.kind == 3:
            result['frame'] = p.frame
        return 'ok', result

    def lines(self, op, ls, value, pid=None, payload=b'', capacity=None):
        output = (U8 * 9600)()
        n = SIZE(123)
        raw = self.buf(payload if pid is not None else value)
        cap = capacity if capacity is not None else (1026 if op == 'decode' else 9600)
        if pid is None:
            status = getattr(self.lib, 'softusb_' + op)(ls, raw, len(value), output, cap, ct.byref(n))
        else:
            status = self.lib.softusb_encode_packet(ls, pid, raw, len(payload), output, cap, ct.byref(n))
        assert status == 0 or n.value == 0, 'error must clear output count'
        data = bytes(output[:n.value]) if op == 'decode' else list(output[:n.value])
        return ERRORS[status], data


def outcome(fn, *args):
    try:
        return 'ok', fn(*args)
    except ref.USBError as error:
        return error.code, None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--seed', type=lambda s: int(s, 0), default=0x54_15)
    args = parser.parse_args()
    native, rng = Native(args.library.resolve()), random.Random(args.seed)
    counts = dict(packets=0, malformed=0, crc_vectors=0, capacity=0)

    assert ref.crc5(0) == 2
    assert ref.crc16(b'123456789') == 0xB4C8
    for field in range(2048):
        assert native.lib.softusb_crc5(field) == ref.crc5(field)
        counts['crc_vectors'] += 1
    for nibble in range(256):
        assert native.lib.softusb_pid_byte(nibble) == ref.pid_byte(nibble)
        assert native.lib.softusb_pid_valid(nibble) == (ref.pid_byte(nibble) == nibble)

    def valid(ls, pid, payload):
        packet = ref.build_packet(ls, pid, payload)
        assert native.build(ls, pid, payload) == packet
        assert native.parse(ls, packet) == ('ok', ref.parse_packet(ls, packet))
        expected = ref.encode(ls, packet)
        assert native.lines('encode', ls, packet) == ('ok', expected)
        assert native.lines('encode', ls, [], pid=pid, payload=payload) == ('ok', expected)
        assert ref.decode(ls, expected) == packet
        assert native.lines('decode', ls, expected) == ('ok', packet)
        if pid in ref.DATA:
            assert native.lib.softusb_crc16(native.buf(payload), len(payload)) == ref.crc16(payload)
            counts['crc_vectors'] += 1
        counts['packets'] += 1
        return packet, expected

    def malformed(ls, states, expected_error=None):
        expected = outcome(ref.decode, ls, states)
        actual = native.lines('decode', ls, states)
        if expected[0] == 'ok':
            assert actual == expected
        else:
            assert actual[0] == expected[0], (expected, actual, states)
        if expected_error:
            assert expected[0] == expected_error, (expected_error, expected)
        counts['malformed'] += 1

    # Exhaustive token fields and SOF frame numbers, both polarities.
    for field in range(2048):
        for ls in (True, False):
            for pid in (ref.OUT, ref.IN, ref.SETUP):
                valid(ls, pid, bytes([field % 128, field // 128]))
            valid(ls, ref.SOF, field.to_bytes(2, 'little'))
    for ls in (True, False):
        for pid in (ref.ACK, ref.NAK, ref.STALL):
            valid(ls, pid, b'')
        # All single-byte values, repeated across every LS payload length.
        for byte in range(256):
            for length in range(9):
                for pid in (ref.DATA0, ref.DATA1):
                    valid(ls, pid, bytes([byte]) * length)

    for index in range(3000):
        ls = index % 4 != 0
        length = rng.randrange(9 if ls else 1024)
        payload = rng.randbytes(length)
        pid = rng.choice([ref.DATA0, ref.DATA1])
        packet, states = valid(ls, pid, payload)
        if index < 500:
            # Corrupt a CRC while retaining fully valid NRZI/stuffing/framing.
            corrupt = bytearray(packet)
            corrupt[-1] ^= 1 << rng.randrange(8)
            assert native.parse(ls, corrupt)[0] == 'crc'
            malformed(ls, ref.encode_unchecked(ls, corrupt), 'crc')
        changed = states.copy()
        changed[rng.randrange(len(changed))] = rng.randrange(5)
        malformed(ls, changed)
        if index < 200:
            malformed(ls, states[:rng.randrange(len(states))])

    for ls in (True, False):
        for pid in (ref.DATA0, ref.DATA1):
            for payload in (bytes(8), b'\xff' * 8, b'\x55' * 8, b'\xaa' * 8,
                            b'\x7e\xff\xf8\x1f\xff\xff\xff\xff'):
                packet, states = valid(ls, pid, payload)
                # Every individual physical-state mutation of max LS packets.
                for pos in range(len(states)):
                    for level in range(5):
                        changed = states.copy()
                        changed[pos] = level
                        malformed(ls, changed)
                for length in range(len(states)):
                    malformed(ls, states[:length])
                for cap in (0, len(states) - 1):
                    assert native.lines('encode', ls, packet, capacity=cap)[0] == 'capacity'
                    counts['capacity'] += 1
                for cap in (0, len(packet) - 1):
                    assert native.lines('decode', ls, states, capacity=cap)[0] == 'capacity'
                    counts['capacity'] += 1
        # Full-speed maximum data length, beyond LS limits, several patterns.
        if not ls:
            for payload in (bytes(1023), b'\xff' * 1023, rng.randbytes(1023)):
                packet, states = valid(ls, ref.DATA1, payload)
                assert native.parse(True, packet)[0] == 'length'
                assert native.lines('encode', True, packet)[0] == 'length'
        # Missing/incorrect stuffed bit immediately before EOP.
        for number in range(65536):
            packet = ref.build_packet(ls, ref.DATA0, number.to_bytes(2, 'little'))
            if packet[-1] & 0xFC == 0xFC:
                states = ref.encode(ls, packet)
                malformed(ls, states[:-4] + states[-3:], 'stuff')
                states[-4] = states[-5]
                malformed(ls, states, 'stuff')
                break
        else:
            raise AssertionError('no trailing-stuff test vector found')
        # Hand-crafted framing failures, each with a precise error category.
        _, states = valid(ls, ref.ACK, b'')
        changed = states.copy(); changed[0] = 0
        malformed(ls, changed, 'sync')
        changed = states.copy(); changed[10] = ref.SE1
        malformed(ls, changed, 'line')
        changed = states.copy(); changed[10] = ref.SE0
        malformed(ls, changed, 'eop')
        changed = states.copy(); changed[-2] = ref.DM if ls else ref.DP
        malformed(ls, changed, 'eop')
        changed = states.copy(); changed[-1] ^= 3
        malformed(ls, changed, 'eop')
        for corrupt in (b'\x00', bytes([ref.pid_byte(7)]), b'\xd2\x00',
                        b'\xe1\x00\x11', b'\xc3\x01\x00\x00'):
            expected = outcome(ref.parse_packet, ls, corrupt)[0]
            assert expected != 'ok'
            assert native.parse(ls, corrupt)[0] == expected
            malformed(ls, ref.encode_unchecked(ls, corrupt), expected)

    for ls, length in ((True, 9), (False, 1024)):
        status, _ = native.lines('encode', ls, [], pid=ref.DATA0, payload=bytes(length))
        assert status == 'length'
    print(f'softusb differential: seed={args.seed:#x}; ' + ', '.join(f'{v} {k}' for k, v in counts.items()))


if __name__ == '__main__':
    main()
