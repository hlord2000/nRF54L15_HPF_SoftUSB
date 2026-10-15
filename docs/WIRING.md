# Bench wiring: nRF54L15 DK SoftUSB

**Status 2026-10-15: physical PCA10156 revision UNKNOWN; no `connected` confirmation has been received. Do not flash.**

Hardware reservation: DK debugger serials `1057720622` and `1057776754`, plus the Android phone, belong exclusively to the closed-loop BLE project. Continue software/research only until ownership is explicitly released. Do not flash/reset either DK or open debug/serial sessions; a `connected` reply alone does not release this reservation. Do not stop Saleae or unrelated hardware.

This plan covers isolated GPIO waveform/loopback tests. Do not attach generated DP/DM to a real USB host. The DK debugger USB connector J3 is not the emulated USB port.

Both images now pass the saved strict NCS HPF build with developer mode disabled ([build evidence](../firmware/build-evidence/build-status.json)); no runtime, hardware or loopback result follows from that build. ABI/pins/configuration are unchanged. This document specifies a proposed wiring plan, not a record that wires or Board Configurator settings have been applied. The root thread owns hardware operation and Saleae captures. See `RESEARCH.md` for timing/electrical rationale and `RESULTS.md` for actual observations.

## Identify the board and route the header first

Read the PCA10156 revision printed on the actual DK. A Saleae hardware revision or lsusb device ID does not establish DK revision.

- **PCA10156 v1.0.0:** use Nordic Board Configurator to select the P2.00–05 **GPIO/header** path instead of external flash. The official schematic sheet 8 places U3/U8 TMUX1574 between the SoC and header/flash. Do not alter SB11 to perform the older procedure: on v1.0.0 it controls the mux selection circuit and cutting it permanently selects flash according to the current guide.
- **Earlier DK:** use the hardware files matching its printed revision. The v0.9.1 hardware guide specifies disconnecting flash with SB11–SB16 and enabling headers with SB17–SB22. This is revision-specific documentation, not an instruction to blindly cut the present board.
- Confirm the resulting path from each pad to its header position and flash isolation before external loopback. Firmware disabling SPI cannot close a missing header path or remove an unwanted flash load.

Sources: [current external-memory guide](https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_dk/page/ug/nrf54l15_dk/hw_desription/external_memory.html), [current solder bridge table](https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_dk/page/ug/nrf54l15_dk/hw_desription/solder_bridge.html), [older vendor guide v0.9.1](https://docs-be.nordicsemi.com/bundle/nRF54L15_DK/raw/resource/enus/nRF54L15_DK_HW_User_Guide_v0.9.1.pdf).

## Voltage and power

Power the DK through its ordinary debugger supply. Factory VDD:nRF is **1.8 V**, not 3.3 V. Select **3.3 V** using Board Configurator before any later 3.3 V external signaling. On v1.0.0 buffered VDD:IO follows VDD:nRF; verify both with a meter. Do not inject a separate 3.3 V source into an actively driven DK rail. Recheck the persisted settings after power cycling. [Nordic power guide](https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_dk/page/ug/nrf54l15_dk/hw_desription/power_supply.html), [board control persistence](https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_dk/page/ug/nrf54l15_dk/hw_desription/board_control.html).

GPIO must remain between ground and the powered IO rail during ordinary operation. The absolute limit VDD+0.3 V is not permission to drive an unpowered/1.8 V DK from 3.3 V. Never connect GPIO to 5 V, USB VBUS, CC1/CC2 or a negotiated PD rail. Internal pulls are disabled on the waveform and loopback pads. Select S0S1 initially; stronger drive/slew is a measured experiment requiring a current and edge budget. [GPIO electrical table](https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/_tmp/nrf54l15/autodita/gpio/parameters.elec_spec.html).

## Exact proposed P2 connector assignment

The numeric header pins below are verified from **PCA10156 v1.0.0 official schematic sheet 7**, file `pca10156_sheet7_connectors.SchDoc`, not guessed from `P2.xx`. P2 is a 2x10 connector. Identify pin 1 by the board's mark and confirm ground with power removed; do not orient by a photograph of a different board revision. The header's `00` label denotes SoC P2.00, not header pin 0.

| Signal | SoC pad | Header position v1.0.0 | VIO bit / mask | Initial condition |
| --- | --- | --- | --- | --- |
| DP waveform output | P2.00 | **P2 pin 3** | VIO4 / `0x10` | High-Z, no pull |
| DM waveform output | P2.01 | **P2 pin 4** | VIO0 / `0x01` | High-Z, no pull |
| Capture marker | P2.02 | **P2 pin 5** | VIO1 / `0x02` | High-Z until armed |
| RX DP loop input | P2.03 | **P2 pin 6** | VIO3 / `0x08` | Input only, no pull |
| RX DM loop input | P2.04 | **P2 pin 7** | VIO2 / `0x04` | Input only, no pull |
| Reference rail | — | **P2 pins 1 or 20** | — | VDD:IO, meter/reference only |
| Ground | — | **P2 pins 2 or 19** | — | Common signal ground |

P2 pin 8 is P2.05, currently spare; P2 pins 9–13 are P2.06–10. Pins 14–18 are unconnected in this schematic. Do not use arbitrary spare-looking pins as ground. The P30/P31/P32 power headers provide **5 V**, not VDDIO. [Official hardware package](https://nsscprodmedia.blob.core.windows.net/prod/software-and-other-downloads/dev-kits/nrf54l15-dk/pca10156-nrf54l15-dk-1_0_0.zip), [vendor hardware download/changelog](https://www.nordicsemi.com/Products/Development-hardware/nRF54L15-DK/Hardware-files), [GPIO interface](https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_dk/page/ug/nrf54l15_dk/hw_desription/connector_if.html).

DP/DM/sync remain the agreed provisional function assignment; their silicon VIO mapping is confirmed. Ownership is `CTRLSEL=VPR` via `NRF_GPIO_PIN_SEL_VPR`, not a literal nRF54L15 MCUSEL register. RX pads must never appear in the output-direction mask. Automatic-clock shifter mode is forbidden for this assignment because VIO0 is DM.

## Isolated loopback and instrument plan

For the initial unloaded digital fixture, connect **no USB connector, host cable, pull-up or external source**. Leave DP/DM high-Z until an explicit bench arm command after the interlock is satisfied.

Proposed short loopback wiring, installed with power removed:

```text
P2 pin 3 (P2.00 DP) ---- 1 kΩ ---- P2 pin 6 (P2.03 RX_DP)
P2 pin 4 (P2.01 DM) ---- 1 kΩ ---- P2 pin 7 (P2.04 RX_DM)
P2 pin 5 (P2.02 marker) ---------- instrument digital marker input
P2 pin 2 or 19 (GND) ------------ instrument common ground
```

The two **1 kΩ loopback resistors** are bench current limiting, not USB terminations. If firmware accidentally drives opposite levels, 3.3 V / 1 kΩ is at most 3.3 mA per branch before output resistance; two branches plus other loads must remain within the 15 mA total GPIO recommendation. They add RC delay and are suitable for LS functional loopback characterization only. Changing or removing them for faster testing requires a new contention/edge analysis. RX pads stay input-only. Do not tie two outputs directly together.

Suggested instrument allocation for the root thread (logical channel numbers are a proposal; record actual assignments in RESULTS):

| Instrument input | Connection | Purpose |
| --- | --- | --- |
| Digital probe port 0, D0 | DP / P2 pin 3 | Source-state stream |
| Digital probe port 0, D1 | DM / P2 pin 4 | Source-state stream |
| Digital probe port 0, D2 | Marker / P2 pin 5 | Arm/packet trigger |
| Digital probe port 0, D3 | RX_DP / P2 pin 6 | Independent external return path |
| Digital probe port 1, D0 | RX_DM / P2 pin 7 | Independent external return path |
| Scope CH1 (API index 0) | DP near header | Voltage/edge/overshoot |
| Scope CH2 (API index 1) | DM near header | Voltage/edge/skew; math CH1-CH2 if supported |
| Digital and analog returns | P2 pin 2 or 19 / nearby verified ground | Short common return |

Each MSO smart digital probe has four channels, so the five-signal allocation
above requires two probes. Verify physical connector to zero-based API port
mapping. `capture_usb.py` defaults to the first three source signals; add
`--rx-probe-port 1` to record primary D3 and secondary D0 for the two RX paths.
If only one smart probe is available, use separate source/return acquisitions
and record the changed wiring. Analog probes must have their physical switches
set to 10x, matching the script's configured attenuation.

Use high-impedance probes and short ground springs/leads. Two grounded single-ended channels both reference board ground; never clip a grounded probe return to DM to imitate a differential probe. Record all probe capacitance, ground placement, threshold, sample rate, bandwidth and resistor locations. If the instrument cannot acquire this channel set simultaneously, use separate documented captures without pretending they are simultaneous.

Digital decode verifies sampled logic and packet structure. It cannot establish differential crossover jitter, USB edge-rate compliance, input common-mode margin or contention current. Long flying wires, header stubs, U3/U8 and five probe inputs affect the electrical result. No capture bandwidth or sample rate is asserted here: the root thread must verify actual instrument capabilities and uncertainty. Do not interpret an unconnected floating input as a valid USB idle state.

## Pull-up and series resistor scope

**For the current isolated fixture:** no attach pull-up and no USB line termination are fitted. The only added resistors are the 1 kΩ loopback limiters above. The first milestone is digital/protocol correctness with known load.

**For a later peripheral attach plan:** LS identifies with a 1.5 kΩ ±5% pull-up on **D-** to a valid approximately 3.3 V termination supply; FS would use D+. An internal Nordic pull-up is not a substitute. Pull-up connection must be controlled or physically disconnected until firmware, VBUS-valid sensing and power sequencing are established. Do not use the marker pin as a pull-up supply. This stage requires an explicit reviewed hardware plan and user action; it is not part of current bench wiring. [USB 2.0 §§7.1.5 and 7.2.4](https://www.usb.org/document-library/usb-20-specification), [original specification text mirror](https://doksi.net/en/get.php?lid=1972).

No fixed “USB series resistor” value is qualified for these GPIO pads. Published software-USB projects use values such as 22, 33 or 47 Ω on their own platforms; porting the value does not establish our impedance, current safety or LS slew. Those small values are not adequate accidental-output-contention protection. Choose any future DP/DM series network from measured pad behavior and USB load requirements; use a differential USB PHY if needed. No ferrite separator is planned.

## Application-core PD reservation (not connected)

Reserve SERIAL21 on the app core and the following P1 positions. Clock pins P1.11/P1.12 are documented clock-capable pins; neither carries a DK button or LED. P1.04–07 are also the default UART20/UART1 signals, so keep that UART routing disabled and verify/disconnect the debugger connection before reusing them. The current firmware overlay disables UART20 and selects UART30/P0; default UART30 pinctrl uses P0.00/01 plus P0.02/03 RTS/CTS. P1 PD is otherwise untouched. Confirm the actual board-controller serial routing before attaching an external controller.

| Function | SoC pin | P1 header pin v1.0.0 |
| --- | --- | ---: |
| I2C SCL, or alternative SPI SCK | P1.11 | 14 |
| I2C SDA, or alternative SPI MOSI | P1.12 | 15 |
| PD controller interrupt to app | P1.04 | 7 |
| PD controller reset/control, polarity controller-specific | P1.05 | 8 |
| Alternative SPI MISO | P1.06 | 9 |
| Alternative SPI CS | P1.07 | 10 |
| Ground | — | 2 or 19 |
| VDDIO reference | — | 1 or 20 |

TWIM21 and SPIM21 share SERIAL21 base/IRQ and are **alternatives**, not simultaneous independent peripherals. Start I2C at a controller-supported rate; choose its pull-ups from bus capacitance and sink-current limits, counting any existing module pulls. No pull-up resistor value or module IO voltage is assumed. A controller with a 1.8 V-only interface needs compatible translation even when the DK is at 3.3 V. No CC1, CC2, VCONN, raw VBUS or negotiated voltage connects to these pins. [NCS board pinctrl](https://github.com/nrfconnect/sdk-zephyr/blob/ncs-v3.4.1/boards/nordic/nrf54l15dk/nrf54l15dk_nrf54l_05_10_15-pinctrl.dtsi), [official DK schematic](https://www.nordicsemi.com/Products/Development-hardware/nRF54L15-DK/Hardware-files).

Before first flash, hardware ownership must be explicitly released; then the root thread must present the revision-matched final wiring and receive the user's exact **`connected`** confirmation. A `connected` reply by itself does not override the hardware reservation. No confirmation, power measurement, solder change or hardware connection is inferred by this document.
