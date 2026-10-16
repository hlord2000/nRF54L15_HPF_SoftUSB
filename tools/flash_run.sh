#!/usr/bin/env bash
# Flash FLPR + app images to the SoftUSB DK, then log serial + kernel USB messages.
set -e
cd "$(dirname "$0")/.."
SN=${SN:-1057720622}; B=firmware/build; DUR=${1:-20}
date +%T > /tmp/flash_t
nrfutil device program --serial-number $SN --firmware $B/softusb_flpr/zephyr/zephyr.hex --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,reset=RESET_NONE | tail -1
nrfutil device program --serial-number $SN --firmware $B/firmware/zephyr/zephyr.hex --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,reset=RESET_NONE | tail -1
nrfutil device reset --serial-number $SN --reset-kind RESET_PIN | tail -1
.venv/bin/python tools/serlog.py $DUR /dev/ttyACM1
journalctl -k --since "$(cat /tmp/flash_t)" --no-pager | grep -v -E "audit|usb2-port5" | cut -c1-170 | tail -${KLINES:-25}
