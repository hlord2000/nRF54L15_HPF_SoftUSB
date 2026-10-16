#!/usr/bin/env bash
# Reset the DK N times; each time require enumeration + a 50-report echo pass.
cd "$(dirname "$0")/.."; SN=${SN:-1057720622}; N=${1:-5}; pass=0
for i in $(seq 1 $N); do
  nrfutil device reset --serial-number $SN --reset-kind RESET_PIN >/dev/null
  ok=0
  for t in $(seq 1 40); do sleep 0.5; lsusb -d 2fe3:0100 >/dev/null && ls /sys/class/hidraw/*/device/uevent >/dev/null && { ok=1; break; }; done
  sleep 1
  res=$(timeout 60 .venv/bin/python tools/hid_echo_test.py 50 2>&1 | tail -1)
  echo "cycle $i: enumerated=$ok after ${t}x0.5s; $res"
  [[ $ok == 1 && $res == "echo: 50/50 ok"* ]] && pass=$((pass+1))
done
echo "PASS $pass/$N"
