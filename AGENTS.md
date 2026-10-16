# SoftUSB project agreements

- 2026-10-16 user decision: user wired DK `1057720622` P2.00 (D+) / P2.01 (D-) DIRECTLY to a USB breakout on this PC (no resistors, VBUS not to DK) and said "go ahead and try". This releases that DK for SoftUSB host-attach experiments; first test is `firmware/attach_test` (internal pull-up on D- only, pins input-only). Saleae is not attached for now. DK `1057776754` stays reserved for BLE. Bub informed.

- Hardware reservation (Bub, 2026-10-15): DK debugger serials `1057720622` and `1057776754`, plus the Android phone, belong exclusively to the new closed-loop BLE project. Do not flash/reset either DK or open exclusive debug/serial sessions. Continue software/research only until that ownership is explicitly released. A `connected` reply alone does not override this reservation. Do not stop Saleae or unrelated hardware. SoftUSB held no debug/serial sessions or queued hardware jobs at handoff.

- Read `/home/h/.codex/AGENTS.md` and preserve the user's original hardware interlocks.
- Target NCS v3.4.1 at `/home/h/ncs/v3.4.1`. USB LS peripheral is the primary goal; FS and host are experimental stretch work.
- Before any first flash: give exact revision-appropriate wiring, and wait for the user's explicit `connected` confirmation. Do not infer it from USB enumeration or a helper report. No confirmation has been received as of 2026-10-15 takeover.
- Never attach the generated USB signals to a real USB host without an explicit next-stage hardware plan and user action. Current firmware is a bench fixture, not a USB-compliant device.
- DK defaults to 1.8V; board revision, header routing and measured VDDIO3.3V must be checked. Do not blindly alter solder bridges: v1.0.0 uses Board Configurator routing.
- Use Saleae Python automation only for captures/decoding/export. Headless MSO preview407561e0 needs MSO firmware1.0.54; installed device1.0.52 at takeover. The headless API cannot update it; the user was asked to do that one GUI setup action and close Logic2.
- One heavy build/test job machine-wide, only Backplane `work_run` with lock `bend`. Wrap firmware builds with `systemd-run --user --scope -p MemoryMax=8G -p CPUQuota=400%`, modest `-j4`. Never use the linked remote machine with faulty RAM.
- Do not report synthetic test output as measured hardware results. Keep `docs/RESULTS.md` explicit about pending hardware checks.
- Commit with the configured hlord2000 identity. Remote: github.com/hlord2000/nRF54L15_HPF_SoftUSB (user asked to publish 2026-10-16); push only finished, tested work.
