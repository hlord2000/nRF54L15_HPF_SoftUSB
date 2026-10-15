# nRF54L15 HPF SoftUSB bench firmware

This is an isolated waveform/packet transmitter for NCS **v3.4.1**, with an
app-core control shell and an SRAM-resident FLPR hard-real-time engine. **LS
peripheral work is the target.** There is no enumerating USB device, host stack,
asynchronous packet receiver, endpoint service, or compliant USB PHY here.
FS is a **noncompliant experimental generator** only.

No firmware has been flashed or hardware timing measured during implementation.
**Build verified:** app-core and FLPR sysbuild passed with strict HPF assembly
checking (`CONFIG_HPF_DEVELOPER_MODE=n`). Evidence is in
[`build-evidence/build-status.json`](build-evidence/build-status.json) and
[`build-evidence/build-02-strict.log`](build-evidence/build-02-strict.log). The hardware interlocks
in the project `AGENTS.md` remain in force: root presents revision-appropriate
wiring and waits for the user's explicit `connected` before first flash.
A shell arm command does not satisfy that first-flash interlock.

**Current hardware reservation:** DK serials1057720622 and1057776754 and the
Android phone belong exclusively to the separate BLE project. Do not flash,
reset, attach a debugger, or open their serial ports for this fixture. Only
software builds and source review are authorized. This reservation overrides
any later bench-command examples until the user explicitly releases hardware.

## Wiring and ownership

Read [the project wiring plan](../docs/WIRING.md) before any hookup. The actual
DK revision is still unverified. P2.00..04 can be connected to external flash
rather than the header. Revision1.0.0 uses Board Configurator routing; older
boards differ. Do not blindly rework solder bridges. DK VDDIO defaults to1.8V;
configure the intended bench supply and **measure3.3V** first. Do not connect
these generated signals to a real USB host.

The official PCA10156 revision1.0.0 header positions are a candidate plan pending
identification/configuration of the actual board:

| Signal | SoC pin | VIO index / mask | Header P2 contact |
|---|---|---|---|
| D+ TX | P2.00 | 4 /0x0010 |3|
| D− TX | P2.01 | 0 /0x0001 |4|
| capture sync | P2.02 | 1 /0x0002 |5|
| independent D+ sample | P2.03 | 3 /0x0008 |6|
| independent D− sample | P2.04 | 2 /0x0004 |7|
| GND | GND | — |2 or19|
| VDDIO reference | VDDIO | — |1 or20|

The optional external loopback jumpers are **P2.00→P2.03** and
**P2.01→P2.04**, with a shared analyzer ground. They are not fitted or verified
by firmware. Both sample pins stay inputs, with no pull. No pull-up is driven by
this fixture. A released line's electrical idle therefore depends on external
biasing; do not interpret high-Z as an actively held USB J state.

Firmware disables the board's SPI00 flash driver. Arm configures only P2.00..04
as input, standard S0S1 drive, no pulls and `CTRLSEL=VPR`, using Nordic HAL.
It sets VIO.DIR=0 before the handoff. TX alone drives VIO4/0/1 and releases them
at the end; a leading J interval before the sync marker is preparation time,
not a separately counted USB cell. All16 VIO output/direction bits are owned by
this fixture; do not run another HPF personality on FLPR concurrently.

The app console is **UART30** on P0.00 TX/P0.01 RX,115200baud. Default pinctrl
also contains P0.02 RTS/P0.03 CTS; hardware flow control is not requested. Board
routing of this UART to the debugger COM port must be verified separately.
UART20 is disabled so P1.04..07 remain available for PD. Reserved, untouched PD
pins are P1.11 SCL/P1.12 SDA on TWIM21, P1.04 IRQ, P1.05 reset. An alternative
SPIM21 would add P1.06 MISO/P1.07 CS; SPIM21 and TWIM21 share SERIAL21 and
cannot run together. No PD peripheral or pin is enabled by this firmware.

## Build

SDK: `/home/h/ncs/v3.4.1`; bundled toolchain:
`/home/h/ncs/toolchains/8285d8ad56`. `tools/build_firmware.sh` reads that bundle's
`environment.json`, invokes west sysbuild for
`nrf54l15dk/nrf54l15/cpuapp`, and builds the `softusb_flpr` child for
`nrf54l15dk/nrf54l15/cpuflpr`. It wraps itself in:

```sh
systemd-run --user --scope -p MemoryMax=8G -p CPUQuota=400%
```

**Start it only through Backplane `work_run` with lock`bend`**; this is the
machine-wide heavy-job interlock. Do not start a second bare west/ninja build or
use the linked faulty-RAM machine. The build script fixes parallelism to`-j4`.
An example tool request is:

```json
{"command":"/home/h/Documents/Nordic/nRF54L15_HPF_SoftUSB/tools/build_firmware.sh > /home/h/Documents/Nordic/nRF54L15_HPF_SoftUSB/firmware/build-evidence/build.log 2>&1","lock":"bend","note":"SoftUSB firmware build"}
```

Built image artifacts:

* App: `build/firmware/zephyr/zephyr.elf`,`.hex`,`.bin`.
* FLPR: `build/softusb_flpr/zephyr/zephyr.elf`,`.hex`,`.bin`.
* Domain mapping: `build/domains.yaml`.

Both HEX files are needed for the two-image fixture; no merged HEX is claimed.
Their ranges do not overlap. `build-evidence/build-status.json` records artifact
and source SHA256 hashes. The final sysbuild output is not a
first-flash authorization. This repository's firmware build script never flashes.

This uses NCS's actual HPF assembly helpers in`nrf/cmake/hpf.cmake`:
`hpf_assembly_install()` generates assembly from`flpr/src/hrt/hrt.c`, checks it
against the SoC assembly snapshot, and builds the snapshot. The initial build used
`CONFIG_HPF_DEVELOPER_MODE=y` to generate the assembly. The reviewed snapshot
was installed with NCS's`hpf_asm_install.cmake`; developer mode is now disabled
so future builds enforce exact assembly equality. The strict rebuild passed and reported`hrt-nrf54l15.s has not changed`. SDK files are not patched. A custom sysbuild child
is necessary because NCS's built-in HPF choices are GPIO and MSPI, with incompatible
protocols/overlays; no stock HPF GPIO driver is masquerading as SoftUSB.

## Commands after authorized flash

Firmware does **not** transmit at startup. FLPR starts with VIO.DIR=0 and
`armed=0`. App core retains an HFXO request for the fixture's lifetime and arm/TX
require actual`PLL.CURRENTFREQ=128MHz`. The SDK startup selects128MHz before
peripheral initialization. Readback is checked rather than trusting devicetree.
Check the marker with a scope/analyzer; a clock request/readback is not measured
edge timing.

```text
softusb status
softusb arm CONFIRM_BENCH_WIRING_3V3
softusb wave ls 96
softusb tx ls 0xd2
softusb tx ls 0xc3 0x00 0xff 0x55 0xaa
softusb loop 0xc3 0x00 0xff 0x55 0xaa
softusb disarm
```

`arm` leaves all bench pins inputs. Its literal confirmation acknowledges the
operator's revision/routing/voltage/bench-only checks; it cannot verify them.
`wave ls N` emits N alternating K/J cells followed by SE0,SE0,J; N is8..1021.
This is a waveform fixture, not a valid packet. `tx ls PID [bytes]` calls
`softusb_encode_packet()` from`common/softusb.c`; it precomputes SYNC,PID,CRC,
bit-stuffing and EOP outside the timing loop. PIDs are full complemented bytes:
ACK0xd2,NAK0x5a,DATA00xc3,DATA10x4b. DATA payloads are at most8bytes for LS.
Token payloads are `{address,endpoint}`. Numeric arguments are decimal or`0x`
hexadecimal. The shell supports up to64payload bytes for experimental FS, with
an overall1024-cell limit. There is no automatic repeat or host response.

`wave fs N` and`tx fs PID [bytes]` run the experimental FS timing path.
**Neither command establishes FS USB compliance.** Do not attach it to a host.

`loop PID [bytes]` is LS TX plus **raw independent input sampling**, followed by
a comparison and portable decoder call on app core. It reads actual VIO.IN
bits3/2 after16NOPs of settling per output update; it never copies TX states
into the RX buffer. Output text reports sample count, mismatches and codec
status, with no wiring inferred. It is not a center-tracking/asynchronous USB
receiver and cannot receive a host packet. Missing jumpers, unconfigured muxes,
noise or electrical problems should fail the comparison. Even a successful
comparison requires external timing/analog evidence before any USB claims.

## Timing and execution

`common/softusb.h` physical states are SE0=0,DP=1,DM=2,SE1=3. LS J=DM and
FS J=DP. App core maps DP to VIO4 and DM to VIO0; SE1 is rejected by both cores.
The sync bit VIO1 is included in every output cell and stays high through EOP,
then becomes high-Z with the data pins. An external bias may be required to
produce a deterministic falling marker once released.

At128MHz the requested rational periods are LS85,85,86cycles and experimental
FS10,11,11cycles. Each cell descriptor contains `(period-1)<<16 | vio_out`.
Assembly writes the complete D+/D− pair with a single direct OUT CSR write.
No data-dependent branch or instruction count exists for J/K/SE0. VTIM counter0
reload and WAIT0 anchor boundaries; counter1 is stopped. IRQs are disabled for
the bounded burst, and code/data live in FLPR SRAM. The first edge's timer-start
phase and last cell's direction-release phase require measurement just like
steady-state timing. A delayed WAIT can miss a timer event and stretch a cell;
SRAM/bus interference and instruction latency remain hardware validation work.

The shifter is deliberately off: `SHIFT_OUTB_TOGGLE` reserves VIO0 for a clock,
but VIO0 is this fixture's D−. No assumption is made that adjacent physical
pins form a packed VIO pair.

FS10cycles=78.125ns is−5.208ns from83.333ns;11cycles=85.9375ns is+2.604ns.
The short cell already exceeds the USB2 FS next-transition total jitter budget
of±3.5ns before pad/clock/routing errors. The correct12Mbps average is
insufficient. LS cell quantization is much smaller than the LS upstream
next/paired budgets (±95/±150ns), but LS electrical and overall timing compliance
remain unmeasured. See[research](../docs/RESEARCH.md).

## IPC ABI1

Fixed address`0x20024000`,16KiB reserved in **both** device trees. App RAM is
`0x20000000..0x20023fff` (144KiB); FLPR SRAM is
`0x20028000..0x2003ffff` (96KiB). App RRAM is`0..0x164fff`; FLPR RRAM starts
`0x165000` and spans96KiB. The app's Zephyr VPR launcher copies that FLPR image
to its SRAM execution region before starting it. No second launcher image,
partition manager, bootloader or overlapping default application partitions.

`include/bench_ipc.h` is the authority for offsets. It uses only naturally aligned
32-bit words and fixed-width arrays, no cross-core pointers or`size_t`.
Magic is`0x53554231`, ABI1; header128bytes,1024packed32-bit TX cells and
1024raw16-bit sample words. Header size and region fit are compile-time checked.
CPUAPP owns requests and cell contents; FLPR owns status and sample contents.
Only one request can be in flight. Release-publishing`request_seq` transfers
ownership, and release-publishing`completed_seq` returns it; consumers acquire
before accessing contents. App serializes shell calls with a mutex, with data
cache disabled and no mailbox/logging ISR in the timing path. FLPR clears stale
commands/arm state on boot, then release-publishes`ready`.

Commands are DISARM1,ARM2,TX3. ARM/TX require the key`0x42454e43`; this is an
accidental-command guard, not authentication. TX validates count, flags,
per-cell pin masks/SE1 and period range before enabling any output. RX sampling
and FS flags cannot be combined. Status records result,armed,TXcount,samplecount,
VIO.IN,VIO.DIR and validated core frequency. Errors are explicit; timeout leaves
the in-flight slot untouched and does not claim the device has disarmed.
Reboot/recovery is required if FLPR is stuck. A bounded successful TX always
releases outputs before completing its IPC response.

## Remaining work

* Software build gate passed: app FLASH62532bytes/RAM17664bytes;
  FLPR SRAM7264bytes, image load address0x165000 and execution entry0x20028000.
  Intel HEX checksums/ranges/nonoverlap and generated/frozen HRT equality passed.
  The SDK emitted its existing deprecated`NRF_PLATFORM_LUMOS` warning.
* Root handles actual DK identification, mux/supply/wiring checks and explicit
  user`connected` gate before flash.
* Measure first/steady/last-cell timing, crossover skew, output levels, rise/fall
  time, release time and marker alignment. Validate real separate-pad captures.
* Implement asynchronous RX with reset/EOP detection, sample phase tracking,
  host-to-device turnarounds and bounded timeout before connecting a host.
* Connect the portable LS transaction simulator to verified RX/TX timing; build
  enumeration/descriptors and full device state separately. Common codec APIs
  are present; no new protocol API is required for this bench TX stage.
