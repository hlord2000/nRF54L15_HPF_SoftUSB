# nRF54L15 HPF SoftUSB

USB low-speed peripheral research and bench firmware for the nRF54L15 FLPR
RISC-V core using Nordic's High-Performance Framework, targeting NCS v3.4.1.
The current goal is packet correctness before USB enumeration.

The repository contains a portable packet codec and transaction simulator,
independent Python reference tests, a Zephyr native_sim ztest wrapper, an
application-core/FLPR HPF bench fixture, and Python-only Saleae MSO capture tools.
Native, sanitizer, Python differential and native_sim protocol tests pass;
firmware acceptance review is pending. Consult [actual results](docs/RESULTS.md)
for evidence and hardware limitations. Source implementation is not hardware validation.

**No firmware has been flashed and no physical loopback or USB host operation
has been verified.** The physical DK revision and measured IO voltage are
unknown. First flash requires the user's explicit `connected` confirmation
following the revision-matched [wiring plan](docs/WIRING.md).

- [Research and feasibility](docs/RESEARCH.md): primary sources, clocks/VIO,
  USB timing limits, electrical gaps and prior art.
- [Architecture](docs/ARCHITECTURE.md): core split, IPC, receiver milestones and
  application-core pin/peripheral reservations for an external PD controller.
- [Wiring](docs/WIRING.md): PCA10156 revision gate, voltage, exact verified
  v1.0.0 connector positions and isolated instrument/loopback setup.
- [Saleae automation](docs/AUTOMATION.md): MSO firmware prerequisite, Python
  capture/export and timing analysis limitations.
- [Host tests](tests/README.md): native, sanitizer, differential reference and
  native_sim runners.
- [Firmware](firmware/README.md): build, clock and memory layout, manual bench
  commands and implementation limitations.

LS is the primary target. FS remains an explicitly noncompliant timing
experiment: an exact average12Mbit/s from128MHz10/11/11-cycle cells does not
satisfy the FS source-jitter budget. Host mode is a stretch study. PD has an
architecture/resource reservation only; no CC/PD implementation exists.

Every heavy job must run through Backplane `work_run` with `lock: "bend"`,
inside an8GiB/400% systemd scope with modest build concurrency. Do not bypass
the workstation queue or use the linked machine with faulty RAM. Keep commits
local; do not push.
