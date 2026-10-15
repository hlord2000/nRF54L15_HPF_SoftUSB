# SoftUSB research and evidence

Checked 2026-10-15. Target: nRF54L15 DK / NCS **v3.4.1**, with an LS peripheral as the accepted primary goal. FS and host remain stretch work. This is source research and calculated timing, **not measured hardware results**. Physical PCA10156 revision, routing state, VDDIO and user wiring confirmation remain unknown. Bench results belong in `RESULTS.md`.

## Release and framework verification

Nordic's official GitHub release page and releases/latest API both identify **v3.4.1** as the latest published release at this check, published 2026-09-17. The installed `nrf` checkout describes itself as `v3.4.1`, commit `b20f8619ba9a5530f8c34b0a130d829947cfe55d`; `VERSION` says 3.4.1 with `VERSION_METADATA = lts`. Installed Zephyr identifies as `ncs-v3.4.1`. A website labeled `latest` currently renders development **3.4.99**, so it must not silently define this project's APIs. [Nordic release](https://github.com/nrfconnect/sdk-nrf/releases/tag/v3.4.1), [release API](https://api.github.com/repos/nrfconnect/sdk-nrf/releases/latest), [tagged VERSION](https://github.com/nrfconnect/sdk-nrf/blob/v3.4.1/VERSION).

HPF is experimental. It supplies an execution/integration framework for custom peripheral emulation, with GPIO and MSPI examples; it does not supply a ready-made USB peripheral. The GPIO example separates app-core driver/IPC, FLPR control and an HRT module. Its configuration disables multithreading, logging, console and kernel ticks. Use IPC to submit work, never to schedule individual USB edges. [Tagged HPF guide](https://github.com/nrfconnect/sdk-nrf/blob/v3.4.1/doc/nrf/app_dev/device_guides/coprocessors/hpf.rst), [GPIO example](https://github.com/nrfconnect/sdk-nrf/blob/v3.4.1/applications/hpf/gpio/README.rst), [configuration](https://github.com/nrfconnect/sdk-nrf/blob/v3.4.1/applications/hpf/gpio/prj.conf).

Local evidence is under `/home/h/ncs/v3.4.1/`:

| Evidence | Relative path |
| --- | --- |
| HPF control/HRT boundary and fixed GPIO-to-VIO map | `nrf/applications/hpf/gpio/src/main.c` |
| Paired/masked CSR operations | `nrf/applications/hpf/gpio/src/hrt/hrt.c` |
| Timer/IO behavior | `nrf/doc/nrf/app_dev/device_guides/coprocessors/rt_peripherals.rst` |
| Actual API declarations | `modules/hal/nordic/nrfx/hal/nrf_vpr_csr.h`, `nrf_vpr_csr_vio.h`, `nrf_vpr_csr_vtim.h` in the same directory |
| GPIO owner selector and register constants | `modules/hal/nordic/nrfx/hal/nrf_gpio.h`; `nrfx/bsp/stable/mdk/nrf54l15_types.h` |
| DK shared functions and UART routing | `zephyr/boards/nordic/nrf54l15dk/nrf54l15dk_common.dtsi`, `nrf54l15dk_nrf54l_05_10_15-pinctrl.dtsi` |

The installed HAL repository commit is `69865c20dc4afbdd27060ad1e81af1b62d31f7af`. Preserve SDK manifest revisions when reproducing the experiment.

## FLPR, clocks and timing API

The nRF54L15 FLPR is Nordic's RISC-V VPR implementation, with E/M/C extensions, and operates at the **same frequency as the application processor**. The SoC starts at 64 MHz and supports 64 or 128 MHz; select the clock at startup before HF peripherals run. Live frequency changes are explicitly unsupported. Thus “128 MHz FLPR” is a configuration to verify, not a universal reset condition. [VPR chapter](https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/vpr.html), [CPU frequency selection](https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/chapters/oscillators.html-pll_desc).

VTIM has two 16-bit down-counters, independently usable or combined into 32 bits, at CPU frequency without a prescaler. Enable RT peripherals before using them. The installed header is authoritative where prose misspells `csr` as `cst`. Exact useful names are:

```c
nrf_vpr_csr_rtperiph_enable_set(true);
nrf_vpr_csr_vtim_count_mode_set(counter, mode);
nrf_vpr_csr_vtim_simple_counter_set(counter, value);
nrf_vpr_csr_vtim_simple_counter_top_set(counter, value);
nrf_vpr_csr_vtim_simple_counter_add_set(counter, value);
nrf_vpr_csr_vtim_simple_wait_set(counter, write, value);
nrf_vpr_csr_vtim_combined_counter_set(value);
nrf_vpr_csr_vtim_combined_wait_trigger();
nrf_vpr_csr_vio_out_set(mask);
nrf_vpr_csr_vio_dir_set(mask);  /* 0=input, 1=output */
nrf_vpr_csr_vio_in_get();
```

Counter modes include STOP, WRAP, RELOAD and TRIGGER_COMBINED. Wait writes stall the CPU until the corresponding event. Counter-zero/reload behavior and instruction placement must be included in an edge schedule; a delay argument is not automatically a pad-to-pad bit period. Inspect assembly and measure the generated stream before assigning timing capability. [Tagged RT-peripheral guide](https://github.com/nrfconnect/sdk-nrf/blob/v3.4.1/doc/nrf/app_dev/device_guides/coprocessors/rt_peripherals.rst), [VTIM HAL at installed revision](https://github.com/nrfconnect/sdk-hal_nordic/blob/69865c20dc4afbdd27060ad1e81af1b62d31f7af/nrfx/hal/nrf_vpr_csr_vtim.h).

Request and retain HFXO on the application core before arming the fixture. Nordic describes XOSTART -> crystal locking -> XOSTARTED, followed by final XOTUNED; HFINT is the free-running PLL alternative. HFXO improves frequency accuracy but does not remove edge quantization or pad delay. Read `PLL.CURRENTFREQ` and clock status, record the request, and validate a marker against an independent timebase. A 64-million-cycle example is not evidence that every FLPR configuration runs at 64 MHz. [HFCLK controller](https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/clock.html-concept_rp5_fgp_bs).

## VIO mapping and ownership

The silicon mapping is fixed and **not numerically identical to GPIO indices**:

| Planned role | SoC pad | VIO bit | Mask |
| --- | --- | ---: | ---: |
| DP | P2.00 | 4 | `0x0010` |
| DM | P2.01 | 0 | `0x0001` |
| Capture marker (not USB SYNC field) | P2.02 | 1 | `0x0002` |
| Separate RX DP loopback input | P2.03 | 3 | `0x0008` |
| Separate RX DM loopback input | P2.04 | 2 | `0x0004` |

P2.05 maps to VIO5; P2.06–10 to VIO6–10. The GPIO example verifies this table in `pin_to_vio_map`. Use `nrf_gpio_pin_control_select(NRF_GPIO_PIN_MAP(2, n), NRF_GPIO_PIN_SEL_VPR)` (or its `nrfy` wrapper) for FLPR pad control. On this SoC the field is `PIN_CNF[n].CTRLSEL`, with VPR value 1; **MCUSEL is a generic ownership term from other Nordic devices, not the field name to copy here**. Pad input/pull/drive and access permissions still need configuration. Configure ownership before use to avoid glitches. [Tagged implementation](https://github.com/nrfconnect/sdk-nrf/blob/v3.4.1/applications/hpf/gpio/src/main.c), [Nordic VIO mapping](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/app_dev/device_guides/coprocessors/rt_peripherals.html).

Use one OUT update for both DP and DM; preserve a software-owned complete OUT shadow. Separate per-pin writes introduce skew and transient states. `SHIFT_OUTB_TOGGLE` reserves **VIO0**, which is the proposed DM, for an automatic clock. Do not enable it with this pin assignment. Direct CSR OUT is suitable for initial characterization; an alternative packed shifter design needs its own pin/frame proof. A contiguous GPIO pair does not imply contiguous VIO channels.

## USB requirements and calculated feasibility

Normative basis is USB 2.0, especially §§7.1.11, 7.1.13, 7.1.18.1 and Tables 7-9/7-10. The USB-IF landing page was accessible, but direct downloads returned 403 here; the original specification's text was cross-checked through a mirror. OCR loses decimal points: e.g. `15%` means **1.5%**, not fifteen percent. [USB-IF specification](https://www.usb.org/document-library/usb-20-specification), [original USB 2.0 text mirror](https://doksi.net/en/get.php?lid=1972).

| Function transmitter | Nominal rate | Allowed average rate error | Nominal bit period |
| --- | ---: | ---: | ---: |
| LS-only peripheral | 1.5 Mb/s | ±1.5% (15,000 ppm) | 666.6667 ns |
| FS-only peripheral | 12 Mb/s | ±0.25% (2,500 ppm) | 83.3333 ns |

Host/hub clock requirements are tighter (±0.05%). Average-rate tolerance is separate from source jitter. FS total next-transition/paired limits are ±3.5/±4 ns; LS upstream totals are ±95/±150 ns, including frequency error. Response delay is measured EOP SE0-to-J to next SOP J-to-K: minimum 2 response bit times, maximum 6.5 with a detachable cable or less than 7.5 with a captive cable. At nominal rate, the 2–6.5-bit window is 1.333–4.333 µs LS and 166.667–541.667 ns FS. [USB 2.0 §§7.1.11, 7.1.18.1, Tables 7-9/7-10](https://doksi.net/en/get.php?lid=1972).

The following are **calculations**, assuming an exact 128 MHz reference, one-cycle edge granularity and fully accounted software overhead:

| Schedule | Cell periods | Mean rate | Cell error from ideal |
| --- | --- | --- | --- |
| LS fixed 85 cycles | 664.0625 ns | 1.505882 Mb/s (+0.3922%) | -2.6042 ns |
| LS rational 85,85,86 | 664.0625, 664.0625, 671.875 ns | 1.5 Mb/s | -2.6042, -2.6042, +5.2083 ns |
| FS fixed 11 cycles | 85.9375 ns | 11.636364 Mb/s (-3.0303%) | +2.6042 ns |
| FS rational 10,11,11 | 78.125, 85.9375, 85.9375 ns | 12 Mb/s | -5.2083, +2.6042, +2.6042 ns |

An LS rational sequence has bounded phase error and plausible digital timing margin. It still needs RX clock recovery and measured turnaround. An FS rational sequence repairs average frequency while leaving 10-cycle consecutive transitions outside even the ±3.5 ns total budget. The pure fixed-grid proposal therefore does **not** establish compliant FS. At 64 MHz the tick is 15.625 ns, worsening the constraint. Do not overclock beyond documented settings to hide this problem. FS progress requires a demonstrated timing architecture and independent analog evidence, not merely a descriptor setting or analyzer decode.

The two cores share memory/interconnect. App-core PD DMA, logging, interrupts, cache misses and arbitration can change FLPR memory-fetch timing even when FLPR interrupts are masked. Precompute symbols, keep HRT/data in RAM, avoid IPC/logging during HRT and characterize under app-core load. Nordic explicitly says FLPR has no access to the Cortex-M33 instruction cache; RAM placement is an intentional architecture choice. [Memory chapter](https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/chapters/memory.html).

## GPIO electrical limitations

Nordic GPIO input thresholds are supply-relative: VIH >=0.7 VDD and VIL <=0.3 VDD (2.31/0.99 V at 3.3 V). Internal pulls are roughly 12–18 kΩ, unsuitable as the USB 1.5 kΩ attach pull-up. Standard-drive VOH/VOL guarantees use 0.5 mA; high-drive VOH is specified at 5 mA for VDD >=2.7 V. Recommended total sustained GPIO current is 15 mA. Typical 12 pF, 20–80% transitions are 4 ns high-drive / 0.9 ns extra-drive; those are not USB edge compliance measurements. Extra-drive output-enable maximum is **855 ns**. Start characterization with S0S1; do not select E0E1 merely for speed. P2 extra-drive slew is controlled by GPIOHSPADCTRL.BIAS.HSBIAS. [GPIO electrical table](https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/_tmp/nrf54l15/autodita/gpio/parameters.elec_spec.html), [GPIO chapter](https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/gpio.html).

Normal SoC supply/GPIO range is 1.7–3.6 V. Absolute IO limits at VDD <=3.6 V are -0.3 to VDD+0.3 V; they are damage limits, not working levels. An unpowered or default-1.8 V DK must not receive 3.3 V on its pads. Do not connect VBUS, CC or PD voltages to GPIO. [Absolute maximum ratings](https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/chapters/abs_max_ratings/doc/abs_max_ratings.html).

USB requires a differential receiver plus single-ended line-state detection and tri-state drivers, with controlled drive impedance, edge shape and fault tolerance. Two CMOS input buffers approximate digital J/K/SE0 detection on a short bench link but do not reproduce a USB differential PHY's common-mode/differential sensitivity. USB edge targets are 75–300 ns LS and 4–20 ns FS; the Nordic 20–80% unloaded numbers cannot be compared directly to the USB test fixture. A pair of 22–47 Ω resistors does not make an unknown GPIO driver USB compliant. Treat GPIO-only electrical behavior as unqualified; an external USB transceiver is the planned path if electrical/RX constraints prevent reliable LS. [USB electrical §§7.1.1–7.1.4](https://doksi.net/en/get.php?lid=1972).

## Comparable implementations: correct speed assumptions

| Project and primary source | What it actually demonstrates | Relevance and limit |
| --- | --- | --- |
| [V-USB, Objective Development](https://www.obdev.at/products/vusb/index.html) | Software **low-speed** USB for AVR; vendor describes an almost-complete LS implementation with specific exceptions. | Useful cycle-counted RX/TX and protocol precedent, not evidence for FS GPIO USB. Respect implementation/license differences. |
| [rv003usb, original repository](https://github.com/cnlohr/rv003usb) | **Low-speed** software USB on CH32V003; assembly pin-change ISR and higher-level C. D- attach pull-up confirms LS. | Corrects the presumed FS comparison. Its timing, instruction set and HSI trimming are platform-specific; reuse ideas only after independent validation. |
| [Pico-PIO-USB, original repository](https://github.com/sekigon-gonnoc/Pico-PIO-USB) | Working FS device, FS/LS host and hub support on RP2040/RP2350 PIO. README budgets one PIO, three state machines and 32 instructions. | Genuine FS software-defined precedent using dedicated state machines. Does not prove an FLPR loop has equivalent instruction throughput, timing grid or analog behavior. |

## Board and concurrent PD findings

The DK defaults to **1.8 V**; use Board Configurator to select 3.3 V VDD:nRF, with buffered VDD:IO following it, and measure the actual rail. Nordic's hardware changelog says v1.0.0 adds analog switches between flash and SoC. In its schematic sheet 8, **U3/U8 TMUX1574** route P2.00–05 either to header or flash. Include switch/trace/probe capacitance and impedance in characterization; TI's typical values are 7.5 pF on-capacitance and 2 Ω on-resistance under its stated conditions, not a measured DK value. Older v0.9.x guides use solder bridges instead. Physical board revision remains unknown. Exact connector plan is in `WIRING.md`. [DK power](https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_dk/page/ug/nrf54l15_dk/hw_desription/power_supply.html), [official hardware package/changelog](https://www.nordicsemi.com/Products/Development-hardware/nRF54L15-DK/Hardware-files), [TI TMUX1574](https://www.ti.com/lit/ds/symlink/tmux1574.pdf).

Concurrent PD means an **external CC/PD controller**, supervised from the application core over I2C or SPI. Prefer a controller with autonomous PHY/protocol/policy handling so no raw CC BMC timing or GoodCRC deadline enters the FLPR loop. A TCPC with only a PHY still leaves policy/deadline work to the application; select the functional split deliberately. TI TPS25751 documents standalone PD with PHY, protocol and policy engine and an I2C target interface; it is an architectural example, not a selected BOM or completed power design. SPI is reserved only as an alternative for a controller that actually offers it. [TI TPS25751 datasheet](https://www.ti.com/lit/ds/symlink/tps25751.pdf).

Planned reservation: application-core TWIM21 on P1.11 SCL/P1.12 SDA, P1.04 IRQ/P1.05 reset; alternative SPIM21 adds P1.06 MISO/P1.07 CS while reusing P1.11 SCK/P1.12 MOSI. Serial instance 21 aliases one register block and IRQ: I2C and SPI alternatives are mutually exclusive. Disable/reassign UART20 use of P1.04–07 and use UART30/P0 for console. See exact header pins and electrical caveats in `WIRING.md`. No PD controller is wired or implemented by this research work.

## Outstanding evidence

- Physical DK revision, persisted board-controller routing and measured VDDIO; explicit user `connected` confirmation before first flash remains required.
- HFXO/PLL runtime state and externally measured timebase; no timing measurements invented here.
- Actual pin-to-header path, ownership/glitch behavior, independent loopback captures and RX thresholds.
- Minimum achievable HRT schedule including fetch/branch/stall latency and first/final cell timing.
- Driver release/re-enable time under load, worst-case receive-to-reply budget and analog crossover timing.
- Protocol error handling, asynchronous RX and transaction state correctness before enumeration.
- Selected external PD controller, IO voltage, interrupt/service budget and protected power path. No raw CC wiring, ferrite separator, host VBUS source or negotiated-voltage routing is authorized by this bench plan.

Browser provenance: Backplane preview was used for Nordic vendor pages (Cloudflare verification blocked them), the official Nordic GitHub release (rendered successfully), and TI TPS25751 (rendered successfully). Primary-source web search, vendor hardware ZIP and local tagged NCS sources supplied the detailed evidence. All hardware-package extracts used for research were kept under `/tmp`, outside project implementation files.
