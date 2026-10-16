import serial, sys, time
ports = sys.argv[2:]; dur = float(sys.argv[1])
ss = []
for p in ports:
    try: ss.append((p, serial.Serial(p, 115200, timeout=0)))
    except Exception as e: print(p, e)
t = time.time(); buf = {p: b'' for p in ports}
while time.time() - t < dur:
    for p, s in ss:
        d = s.read(4096)
        if d: buf[p] += d
    time.sleep(0.01)
for p in ports:
    print('=====', p); print(buf[p].decode(errors='replace'))
