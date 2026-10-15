#!/usr/bin/env python3
"""Independent USB LS/FS packet/line reference, no native-library dependency.

USB 2.0 sections 7.1.8/7.1.9/7.1.13 and 8.3; integer physical states are
SE0=0, DP=1, DM=2, SE1=3. Inputs/outputs describe ideal bit cells, not timing.
SOF codec support does not imply LS SOF transmission is valid on a real bus.
CRC uses MSb-first polynomial registers with LSb-first input; the C codec uses
reflected polynomials and right shifts. This is deliberately a separate model.
"""
from __future__ import annotations

import argparse
import json

SE0, DP, DM, SE1 = range(4)
OUT, IN, SOF, SETUP = 0xE1, 0x69, 0xA5, 0x2D
DATA0, DATA1, ACK, NAK, STALL = 0xC3, 0x4B, 0xD2, 0x5A, 0x1E
TOKEN = {OUT, IN, SETUP}
DATA = {DATA0, DATA1}
HANDSHAKE = {ACK, NAK, STALL}


class USBError(ValueError):
    """A precise packet or line-format failure; .code matches the native API."""

    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


def bits_of(value: int, width: int) -> list[int]:
    return [(value >> i) & 1 for i in range(width)]


def _crc(bits: list[int], width: int, polynomial: int) -> int:
    mask = (1 << width) - 1
    register = mask
    for bit in bits:
        feedback = (register >> (width - 1)) ^ bit
        register = (register << 1) & mask
        if feedback:
            register ^= polynomial
    # Wire order is LSb first; reflect the register and apply final inversion.
    return int(f'{register:0{width}b}'[::-1], 2) ^ mask


def crc5(field: int) -> int:
    return _crc(bits_of(field, 11), 5, 0x05)


def crc16(payload: bytes) -> int:
    return _crc([bit for byte in payload for bit in bits_of(byte, 8)], 16, 0x8005)


def pid_byte(nibble: int) -> int:
    return (nibble & 15) | (((nibble & 15) ^ 15) << 4)


def build_packet(low_speed: bool, pid: int, payload: bytes = b'') -> bytes:
    if pid in TOKEN:
        if len(payload) != 2:
            raise USBError('length')
        address, endpoint = payload
        if address > 127 or endpoint > 15:
            raise USBError('argument')
        field = address + 128 * endpoint
        return bytes([pid, field & 255, (field >> 8) | (crc5(field) << 3)])
    if pid == SOF:
        if len(payload) != 2:
            raise USBError('length')
        field = int.from_bytes(payload, 'little')
        if field > 2047:
            raise USBError('argument')
        return bytes([pid, field & 255, (field >> 8) | (crc5(field) << 3)])
    if pid in DATA:
        if len(payload) > (8 if low_speed else 1023):
            raise USBError('length')
        return bytes([pid]) + payload + crc16(payload).to_bytes(2, 'little')
    if pid in HANDSHAKE:
        if payload:
            raise USBError('length')
        return bytes([pid])
    raise USBError('unsupported' if pid_byte(pid) == pid else 'pid')


def parse_packet(low_speed: bool, packet: bytes) -> dict:
    if not packet:
        raise USBError('length')
    pid = packet[0]
    if pid_byte(pid) != pid:
        raise USBError('pid')
    if pid in TOKEN or pid == SOF:
        if len(packet) != 3:
            raise USBError('length')
        field = int.from_bytes(packet[1:], 'little') & 2047
        if packet[2] >> 3 != crc5(field):
            raise USBError('crc')
        if pid == SOF:
            return {'kind': 3, 'pid': pid, 'frame': field}
        return {'kind': 0, 'pid': pid, 'address': field % 128, 'endpoint': field // 128}
    if pid in DATA:
        if not 3 <= len(packet) <= (11 if low_speed else 1026):
            raise USBError('length')
        if int.from_bytes(packet[-2:], 'little') != crc16(packet[1:-2]):
            raise USBError('crc')
        return {'kind': 1, 'pid': pid, 'payload': packet[1:-2]}
    if pid in HANDSHAKE:
        if len(packet) != 1:
            raise USBError('length')
        return {'kind': 2, 'pid': pid}
    raise USBError('unsupported')


def encode_unchecked(low_speed: bool, packet: bytes) -> list[int]:
    """Encode arbitrary bytes, including bad PID/CRC, for negative RX tests."""
    logical = bits_of(0x80, 8) + [b for byte in packet for b in bits_of(byte, 8)]
    stuffed, run = [], 0
    for bit in logical:
        stuffed.append(bit)
        run = run + 1 if bit else 0
        if run == 6:
            stuffed.append(0)
            run = 0
    level = DM if low_speed else DP
    physical = []
    for bit in stuffed:
        if bit == 0:
            level = 3 - level
        physical.append(level)
    return physical + [SE0, SE0, DM if low_speed else DP]


def encode(low_speed: bool, packet: bytes) -> list[int]:
    parse_packet(low_speed, packet)
    return encode_unchecked(low_speed, packet)


def decode(low_speed: bool, states: list[int]) -> bytes:
    j = DM if low_speed else DP
    k = 3 - j
    limit = 11 + (11 if low_speed else 1026) * 8 + ((11 if low_speed else 1026) * 8 + 1) // 6
    if len(states) < 19 or len(states) > limit:
        raise USBError('length')
    if states[:8] != [k, j, k, j, k, j, k, k]:
        raise USBError('sync')
    if states[-3:] != [SE0, SE0, j]:
        raise USBError('eop')
    # Decode all NRZI bits independently, then remove stuffed bits in a pass.
    previous, logical = k, []
    for level in states[8:-3]:
        if level not in (j, k):
            raise USBError('eop' if level == SE0 else 'line')
        logical.append(int(level == previous))
        previous = level
    bits, index, run = [], 0, 1
    while index < len(logical):
        bit = logical[index]
        bits.append(bit)
        run = run + 1 if bit else 0
        index += 1
        if run == 6:
            if index == len(logical) or logical[index] != 0:
                raise USBError('stuff')
            index += 1
            run = 0
    if len(bits) % 8:
        raise USBError('length')
    packet = bytes(sum(bit << i for i, bit in enumerate(bits[pos:pos + 8]))
                   for pos in range(0, len(bits), 8))
    parse_packet(low_speed, packet)
    return packet


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['encode', 'decode', 'parse'])
    parser.add_argument('value', help='packet hex, or physical state digits for decode')
    parser.add_argument('--full-speed', action='store_true')
    args = parser.parse_args()
    ls = not args.full_speed
    try:
        if args.operation == 'encode':
            print(''.join(map(str, encode(ls, bytes.fromhex(args.value)))))
        elif args.operation == 'decode':
            print(decode(ls, [int(x) for x in args.value]).hex())
        else:
            print(json.dumps(parse_packet(ls, bytes.fromhex(args.value)),
                             default=lambda value: value.hex()))
    except USBError as error:
        parser.exit(1, f'{error.code}\n')


if __name__ == '__main__':
    main()
