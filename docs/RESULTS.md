# Validation results

Date: 2026-10-15. Target: NCS v3.4.1, nRF54L15-DK, HPF FLPR.
**No hardware capture, flash, physical RX loopback, host connection or enumeration
has been verified.** Hardware entries below are pending, not passes.

| Check | Evidence/result | Status |
|---|---|---|
| Saleae MSO presence | USB21a9:1007; saved server log sees device | Detected, not acquisition-ready |
| MSO firmware | Installed1.0.52; preview407561e0 requires1.0.54; wall power unavailable in saved log | Blocked; see AUTOMATION.md |
| DK connection and supply | No DK shown in takeover lsusb; no connected confirmation | Pending user |
| Portable protocol codec | Implementation/tests in progress | Pending integration |
| Device transaction simulation | Implementation/tests in progress | Pending integration |
| NCS app/FLPR build | Implementation in progress | Pending build |
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
