# Validation results

Date: 2026-10-15. Target: NCS v3.4.1, nRF54L15-DK, HPF FLPR.
**No hardware capture, flash, physical RX loopback, host connection or enumeration
has been verified.** Hardware entries below are pending, not passes.

| Check | Evidence/result | Status |
|---|---|---|
| Saleae MSO presence | USB21a9:1007; saved server log sees device | Detected, not acquisition-ready |
| MSO firmware | Installed1.0.52; preview407561e0 requires1.0.54; wall power unavailable in saved log | Blocked; see AUTOMATION.md |
| DK connection and supply | No DK shown in takeover lsusb; no connected confirmation | Pending user |
| Portable protocol codec | Initial native O2 run: 417,547 checks passed; sanitizer build exposed a CRC5 sign-conversion warning, now fixed with explicit unsigned promotion; sanitizer/reference continuation queued | Native pass; remaining validation pending |
| Device transaction simulation | C simulation implemented; shared native/ztest assertion suites prepared | Validation pending |
| NCS app/FLPR build | First build linked FLPR; app link failed on incompatible HFXO device API. Switched to legacy HF onoff manager; HRT snapshot installed and developer mode disabled; strict rebuild queued | Full build/runtime validation pending |
| Capture analysis tool tests | Five offline synthetic unit tests pass; includes malformed/empty decode rejection and SYNC frequency-offset measurement | Pass, tooling only |
| LS bit rate | Target1.5Mbit/s, nominal666.667ns/bit; device rate tolerance±1.5% | Not measured |
| FS bit rate | Target12Mbit/s, nominal83.333ns/bit; source rate tolerance±0.25% | Not measured; experimental |
| Bit interval jitter / accumulated phase | Requires captured SYNC and packet windows, quantified sample/timebase error | Not measured |
| Rise/fall / differential crossover | Requires analog captures at sufficient resolution and specified USB load | Not measured |
| Packet TX CRC/stuffing/SYNC/EOP | Requires expected fixtures vs Saleae analyzer export | Not measured |
| RX through second physical pair | Requires jumper loopback + real RX engine | Pending |
| Device response turnaround | Requires real RX/TX engine plus sync markers | Not measured |
| Concurrent PD | Architecture/resource reservation only; no PD implementation | Planned |
| Host mode | Feasibility study, no operational host | Stretch |

Rate tolerance does not replace jitter, turnaround, voltage, rise/fall, loading,
or receiver requirements. Consult [RESEARCH.md](RESEARCH.md) for specification
references and the feasibility analysis. Unit-test pass counts must not be
presented as real-bus compliance.

Zephyr wrapper prepared under `tests/zephyr/`: two ztest cases reuse the native
C assertion bodies. Root queued Twister `native_sim/native/64` through the bend
lock and8GiB/400% systemd scope. Result pending; queued is not passed.

The first firmware compile uses HPF developer mode only to generate the initial
HRT assembly. Firmware acceptance additionally requires installing/freezing the
SoC assembly snapshot, setting `CONFIG_HPF_DEVELOPER_MODE=n`, and passing the
strict assembly-check rebuild. Neither stage has yet returned a result.

Initial host-run evidence: `w-t1792083644-131411-132105` passed the native
417,547-check executable, then stopped at sanitizer compilation because strict
`-Werror=sign-conversion` rejected an integer promotion in CRC5. This was a
compile-time diagnostic, not an ASan/UBSan runtime finding. The explicit
unsigned-promotion fix is awaiting sanitizer/reference continuation; no
sanitizer or differential pass has been claimed.

Native_sim first run `w-t1792082209-129711-132190` exited1 at compilation:
`ZTEST(softusb, test_codec)` generated a static `softusb_test_codec` symbol
that collided with the exported shared suite body. No ztest cases executed.
Renamed the ztest suite to `softusb_portable`, preserving the shared assertions;
one rebuild is queued. The earlier watcher timeout was followed by a normal
exit report; no duplicate run or lock bypass was used.

Firmware first build: FLPR linked; app-core HFXO device reference did not link.
The nRF54L implementation now requests HF through
`z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF)` and
`onoff_request()`. Generated HRT assembly was reviewed and installed with
the NCS helper; developer mode is now disabled. Strict build02 result pending.
