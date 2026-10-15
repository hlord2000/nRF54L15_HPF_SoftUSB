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
