#!/usr/bin/env python3
"""SoftUSB data-path test over hidraw (vendor HID 2fe3:0100).

Reads periodic IN reports (LE counter + 'SUSB'), then sends N random 8-byte
OUT reports; the firmware echoes each one as the next IN report.
"""
import glob, os, select, sys, time

def find():
    for d in glob.glob('/sys/class/hidraw/hidraw*'):
        if ':00002FE3:00000100' in open(d + '/device/uevent').read().upper():
            return '/dev/' + os.path.basename(d)
    sys.exit('device not found')

def read(fd, timeout):
    r, _, _ = select.select([fd], [], [], timeout)
    return os.read(fd, 64) if r else None

dev = find(); n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
fd = os.open(dev, os.O_RDWR)
print('device', dev)
counters = []
t0 = time.time()
while len(counters) < 4 and time.time() - t0 < 5:
    d = read(fd, 1.0)
    if d and d[4:8] == b'SUSB':
        counters.append(int.from_bytes(d[:4], 'little'))
print('IN counter reports:', counters)
ok = bad = lost = 0
lat = []
for i in range(n):
    msg = os.urandom(8)
    ts = time.time()
    os.write(fd, b'\x00' + msg)          # report ID 0 + 8 data bytes
    got = None
    while time.time() - ts < 1.0:
        d = read(fd, 1.0)
        if d is None:
            break
        if d[4:8] == b'SUSB':
            continue                      # periodic counter report
        got = d
        break
    if got is None:
        lost += 1
    elif got[:8] == msg:
        ok += 1; lat.append((time.time() - ts) * 1000)
    else:
        bad += 1; print('mismatch', msg.hex(), got.hex())
print(f'echo: {ok}/{n} ok, {bad} mismatched, {lost} lost; '
      f'round trip ms min {min(lat):.1f} avg {sum(lat)/len(lat):.1f} max {max(lat):.1f}' if lat else
      f'echo: {ok}/{n} ok, {bad} mismatched, {lost} lost')
