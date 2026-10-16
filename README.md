# nRF54L15 HPF SoftUSB

A USB 1.1 **low-speed device** bit-banged on GPIO by the nRF54L15's FLPR
RISC-V core (Nordic HPF), exposed to Zephyr as a UDC driver so the stock
`device_next` USB stack runs on top. The demo enumerates on Linux as a
vendor HID and passes interrupt IN/OUT echo and reconnect tests
([results](docs/RESULTS.md)).

## Hardware

nRF54L15-DK with VDD = 3.3 V and the P2 external-flash routing disabled
(Board Configurator). Wire a USB cable/breakout straight to the DK:

| USB | DK |
|---|---|
| D+ | P2.00 |
| D- | P2.01 |
| GND | GND |
| VBUS | not connected |

No resistors: the internal pull-up on D- signals attach. This is out of
spec electrically; treat it as a bench setup.

## Build

Requires nRF Connect SDK **v3.4.1**. From an initialised NCS workspace:

```sh
west build --sysbuild -b nrf54l15dk/nrf54l15/cpuapp -d build path/to/firmware
```

This builds the app-core image and the FLPR image (`firmware/flpr`) together.

## Flash

Program both images, then pin-reset (a system reset does not restart the FLPR):

```sh
nrfutil device program --firmware build/softusb_flpr/zephyr/zephyr.hex --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,reset=RESET_NONE
nrfutil device program --firmware build/firmware/zephyr/zephyr.hex --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,reset=RESET_NONE
nrfutil device reset --reset-kind RESET_PIN
```

The device appears as `2fe3:0100 nRF54L15 FLPR LS HID`. Test the data path
with `python3 tools/hid_echo_test.py` (needs hidraw access).

More detail: [firmware/README.md](firmware/README.md),
[architecture](docs/ARCHITECTURE.md), [research](docs/RESEARCH.md).
