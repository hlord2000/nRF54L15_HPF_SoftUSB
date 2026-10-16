# nRF54L15 HPF SoftUSB: low-speed USB device on the FLPR

A USB 1.1 **low-speed (1.5 Mbit/s) device** bit-banged on P2.00 (D+) and
P2.01 (D-) by a custom HPF personality on the FLPR RISC-V core, exposed to the
app core as a **Zephyr UDC driver** so the stock `device_next` USB stack and
classes run unmodified. Demo: a vendor-defined HID (2fe3:0100) that echoes
8-byte OUT reports and sends a counter report every 500 ms.

Status (measured on DK `1057720622`, Linux host via hub, see
[`docs/RESULTS.md`](../docs/RESULTS.md)): enumerates first try, 500/500
interrupt OUT->IN echoes, 0 CRC/stuff/sync errors, 5/5 reset-reconnect cycles.
Timing/eye not yet measured on the Saleae. Not USB-compliant hardware: no series
resistors, internal pull-up instead of 1.5 kOhm.

## Wiring

| USB | DK |
|---|---|
| D+ | P2.00 (P2 header pin 3) |
| D- | P2.01 (P2 header pin 4) |
| GND | GND |
| VBUS | **not connected** |

DK VDD set to 3.3 V and the P2.00-05 external-flash routing disabled (Board
Configurator). No external resistors: the FLPR enables the nRF internal pull-up
on D- to signal attach (below the USB spec's 1.5 kOhm guarantee; works on the
tested host). Console: UART30 (`/dev/ttyACM1` on this PC), 115200.

## Layout

| Path | Role |
|---|---|
| `flpr/src/hrt/hrt.c` (+ frozen `hrt-nrf54l15.s`) | HRT bit engines: `su_rx` (SYNC edge alignment, NRZI, unstuffing, EOP) and `su_tx` (NRZI, stuffing, EOP). Bit clock = VTIM CNT0 RELOAD 85/85/86 cycles = 1.5 Mbit/s exactly; WAIT0 gives zero-jitter OUT/IN timing. |
| `flpr/src/main.c` | Link layer: token CRC5, address/EP filter, SETUP/OUT/IN handling, toggles, ACK/NAK/STALL within the turnaround, SET_ADDRESS switch after the status stage, reset/suspend inference. |
| `include/softusb_ipc.h` | Shared-RAM ABI (seq-counter ownership, single writer per field). FLPR -> app wake-up via VEVIF event 20. |
| `src/udc_softusb.c` | Zephyr UDC driver: stages IN packets (+CRC16), drains OUT, forwards SETUP/reset, EP enable/halt/address. |
| `src/main.c` | Demo app: HFXO request, USBD setup (bcdUSB 1.10, EP0 MPS 8), vendor HID. |

## Build, flash, test

```sh
tools/build_firmware.sh            # sysbuild app + FLPR, capped systemd scope
tools/flash_run.sh 10              # program both images, PIN reset, log serial + dmesg
.venv/bin/python tools/hid_echo_test.py 500
tools/reconnect_test.sh 5
```

A pin reset is required after flashing: a system reset leaves the FLPR running
the old image. HRT changes need `CONFIG_HPF_DEVELOPER_MODE=y` in `flpr/prj.conf`
while iterating, then `ninja asm_install` in `build/softusb_flpr` and back to `n`.

## Limits

- Low speed only, 4 endpoints (EP0 control + EP1-3 interrupt), MPS 8.
- The UDC API has no LS speed value; the driver reports FS and descriptors are
  kept within LS rules by the app (EP0 MPS 8, report size 8, bInterval >= 10).
- With the weak pull-up, idle J reads as 0 on the nRF, so reset is inferred from
  >3 ms silence followed by traffic; suspend/resume is untested; remote wakeup
  is not supported.
- An IN packet already handed to the FLPR cannot be revoked by `ep_dequeue`.
- Requires HFXO: on the RC clock the FLPR ran ~0.8 % fast and long packets failed.
