# Validation results

Checked 2026-10-15. Target: NCS v3.4.1, nRF54L15-DK, HPF FLPR.
**All passing results below are software tests. No flash, hardware capture,
physical RX loopback, host connection or enumeration has been verified.**

| Check | Actual result | Status |
| --- | --- | --- |
| Optimized native C suite | 417,547 checks | Pass |
| ASan + UBSan C suite | 417,547 checks after explicit unsigned CRC5 promotion fix | Pass |
| Independent Python differential, seed `0x5415` | 28,631 packets; 16,096 malformed inputs; 14,287 CRC vectors; 80 capacity cases | Pass |
| Zephyr `native_sim/native/64` | 1/1 executed configuration; 2/2 executed ztest cases; zero warnings | Pass |
| Capture analysis tools | 5 offline synthetic tests, including invalid/empty decode rejection and SYNC frequency-offset measurement | Pass, tooling only |
| NCS app/FLPR strict build | App + FLPR sysbuild exit 0; frozen HRT assembly unchanged with developer mode off; all 20 recorded source and 6 artifact hashes match | Pass, build only |
| Real LS packet TX and Saleae decode | No acquisition performed | Pending hardware |
| Independent physical RX-pad loopback | Known-schedule sample engine implemented; no wires/capture verified | Pending hardware |
| Asynchronous USB RX / response turnaround | No asynchronous hardware receiver | Not implemented |
| Concurrent PD | App-core SERIAL21/P1 resource reservation and external-controller architecture | Planned only |
| USB host | Token/SOF codec fixtures available; no operational host | Stretch |

Native, sanitizer, differential and ztest suites cover portable NRZI/stuffing,
PID/CRC, framing/error handling, buffer bounds and simulated LS transactions.
They do not establish electrical or timing compliance, a live USB device stack,
or enumeration. The Python reference uses a separate MSb-first CRC formulation
from the C codec's reflected register implementation.

Evidence is retained in [host-validation.log](evidence/host-validation.log) and
[native-sim-validation.log](evidence/native-sim-validation.log). Original work
IDs are `w-t1792083644-131411-132105` (initial optimized native run),
`w-t1792083644-131411-132915` (sanitizer/reference continuation), and
`w-t1792082209-129711-133140` (native_sim retry). Full Twister reports and runtime
log remain under ignored `tests/.build/twister-retry1/`.

The initial sanitizer compilation rejected an integer promotion under
`-Werror=sign-conversion`; explicit unsigned operands resolved it. The first
ztest wrapper generated a symbol colliding with the exported shared suite;
renaming its suite to `softusb_portable` resolved it without copying assertions.
Watcher timeout notifications were followed by normal exit reports; no duplicate
jobs or lock bypasses were used. Each heavy job used the machine-wide `bend`
queue with an 8 GiB/400% systemd scope and at most four build workers.

Firmware evidence: [build-status.json](../firmware/build-evidence/build-status.json),
[strict build log](../firmware/build-evidence/build-02-strict.log), and HRT
[TX disassembly](../firmware/build-evidence/hrt-tx-disassembly.txt). Work
`w-t1792083659-131479-133159` exited 0. The strict log says
`File hrt-nrf54l15.s has not changed.` `CONFIG_HPF_DEVELOPER_MODE=n` is verified.
The build emits one SDK deprecation warning for `NRF_PLATFORM_LUMOS`; it does
not prevent the build. The earlier app HFXO device-link issue was resolved by
using the nRF54L HF-clock onoff manager.

| Image | Linked usage | Configured region |
| --- | --- | --- |
| App code | 62,532 bytes | 1,428 KiB RRAM from 0x00000000 |
| App RAM | 17,664 bytes | 144 KiB from 0x20000000 |
| Shared IPC reservation | 16 KiB reserved | 0x20024000–0x20027fff |
| FLPR RAM | 7,264 bytes | 96 KiB from 0x20028000 |

FLPR entry is 0x20028000; its source image loads at 0x00165000. HEX checksums,
address ranges and image nonoverlap were checked. Images remain under ignored
`firmware/build/{firmware,softusb_flpr}/zephyr/`. Build success establishes no
pad timing, startup, IPC runtime or USB interoperability result.

## Pending hardware measurements

| Measurement | Target / required evidence | Actual |
| --- | --- | --- |
| LS bit rate | 1.5 Mbit/s, nominal 666.667 ns/bit; device source-rate tolerance ±1.5% | Not measured |
| FS bit rate | 12 Mbit/s, nominal 83.333 ns/bit; device source-rate tolerance ±0.25% | Not measured; experimental |
| Bit interval / accumulated phase / crossover jitter | Packet and SYNC windows, independent timebase and uncertainty | Not measured |
| Rise/fall, voltage, crossover and skew | Analog capture with stated probe/load and adequate bandwidth | Not measured |
| EOP and output release | First/last-cell phase, SE0 duration and direction-release timing | Not measured |
| Receive-to-reply delay | Actual asynchronous RX, validation and bounded response | Not measured |
| App-core PD coexistence | Representative controller IRQ/DMA load while FLPR runs | Not measured |

FS 128 MHz 10/11/11-cell scheduling already fails the naive source-jitter budget
on its 10-cycle cell, despite the correct mean rate. LS is the primary target.
Rate tolerance does not replace jitter, turnaround or analog requirements.
See [RESEARCH.md](RESEARCH.md) for specification references and limitations.

## Hardware ownership and prerequisites

Bub reserved both attached DKs (debugger serials `1057720622` and `1057776754`)
and the Android phone for another BLE project. SoftUSB holds no debug/serial
sessions and has no hardware job to cancel. **No flash/reset/debug/serial access
is permitted until ownership is explicitly released.** The revision-matched
wiring, measured 3.3 V VDDIO, header routing and explicit user `connected`
confirmation remain required afterward. Physical PCA10156 revision is unknown.
The earlier takeover USB list did not identify a DK; the serials above are from
Bub's later reservation, not a SoftUSB hardware session.

The Saleae MSO was USB-visible (`21a9:1007`). Saved server log `/tmp/las.log`
reports firmware 1.0.52 versus preview407561e0's required 1.0.54, plus unavailable
wall power. No later firmware/power state has been checked. Headless API cannot
perform that update; see [AUTOMATION.md](AUTOMATION.md). Saleae and unrelated
hardware were not stopped.

## Hardware: host attach detect with internal pull-up only (2026-10-16, measured)

Setup: DK `1057720622`, VDD 3.3 V (user-set), onboard flash routing disabled (user). P2.00 = D+ and P2.01 = D- wired directly to a USB breakout on this PC's hub port `1-5.1.1-port2`. No external resistors; VBUS not connected to the DK. Firmware `firmware/attach_test`: both pins input-only; P2.01 internal pull-up on for 10 s, off for 2 s.

Result: the host **does** detect a low-speed attach with only the nRF internal pull-up on D-. Every cycle shows `new low-speed USB device`, then `device descriptor read/64, error -32` and `Device not responding to setup address` / `error -71` (expected: nothing answers yet), then `unable to enumerate USB device`. Log: [evidence/attach-test-dmesg-2026-10-16.log](evidence/attach-test-dmesg-2026-10-16.log).

Caveat: this is below the USB 2.0 guarantee (internal pull ~12-18 kOhm vs. 15 kOhm host pull-down gives ~1.5-1.8 V, under the 2.0 V VIH). It works on this hub; other hosts may not detect it. D- voltage was not measured (Saleae not attached).

## Hardware: enumeration on a real host through the Zephyr USB stack (2026-10-16, measured)

Firmware: `firmware/` (FLPR LS link-layer engine + app-core Zephyr UDC driver `udc_softusb` + stock `device_next` stack with a vendor-defined HID class). Same wiring as the attach test: P2.00 = D+, P2.01 = D- straight to the PC hub port, no resistors, internal pull-up on D-.

Result: **Linux enumerates the device as a low-speed USB HID** (`New USB device found, idVendor=2fe3, idProduct=0100`, `hid-generic ... hidraw9: USB HID v1.11 Device [HPF SoftUSB nRF54L15 FLPR LS HID]`). Descriptors read back by `lsusb -v`: bcdUSB 1.10, bMaxPacketSize0 8, EP1 IN/OUT interrupt, wMaxPacketSize 8, bInterval 10. FLPR counters after enumeration: 86 tokens, 13 SETUPs, 40 IN ACKed, 0 CRC / stuffing / sync errors, 0 IN retries. Evidence: [enumeration-dmesg](evidence/enumeration-dmesg-2026-10-16.log), [lsusb -v](evidence/enumeration-lsusb-v.txt).

Bring-up findings (all measured on this DK):
- With only the internal pull-up, idle D- sits below the nRF VIH (reads 0), so idle J and SE0 are indistinguishable to the FLPR. Bus reset is therefore inferred from >3 ms of silence followed by traffic; host-driven J/K levels read correctly.
- `mcycle` does not count on the FLPR; time comes from VTIM CNT1 in WRAP mode (extended to 32 bits).
- VTIM RELOAD period is TOP+1 cycles: measured 25502 cycles for 300 periods at TOP=84 (85.007).
- Without an HFXO request the FLPR clock measured ~129.0 MHz (+0.8%): tokens (32 bits) decoded but 96-bit DATA packets failed CRC. With HFXO requested by the app it measures 128.0 MHz and data packets decode cleanly.
- `nrfutil ... reset=RESET_SYSTEM` does not restart the FLPR; a pin reset is needed after flashing (`tools/flash_run.sh`).

Not yet verified: interrupt IN/OUT data transfers (needs hidraw access), timing/eye on the Saleae, other hosts.

## Hardware: interrupt data path and reconnects (2026-10-16, measured)

Host access via a udev rule (user-installed) for 2fe3:0100 hidraw. Tool: `tools/hid_echo_test.py` (random 8-byte OUT report, firmware echoes it as the next IN report).

| Test | Result |
|---|---|
| Periodic IN reports | counter 1,2,3,4 received in order |
| OUT->IN echo, dev-mode HRT | 500/500 ok, 0 mismatched, 0 lost; RTT min 5.2 / avg 8.2 / max 96 ms |
| OUT->IN echo, frozen HRT strict build | 300/300 ok; RTT avg 8.3 ms |
| Pin-reset reconnect + 50 echoes each (`tools/reconnect_test.sh`) | 5/5 then 3/3 pass; each enumerated first try, 0 kernel `error -` lines |
| FLPR counters after a test | 430 tokens, 96 IN ACKed, 0 IN retries, 0 CRC/stuff/sync errors |

RTT ~8 ms is consistent with the 10 ms bInterval on both endpoints. Bit timing, jitter, edge rates and levels still need the Saleae (pending MSO firmware update and probe access).
