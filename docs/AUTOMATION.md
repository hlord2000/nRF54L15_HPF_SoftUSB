# Saleae MSO automation

All acquisitions, analyzer setup and exports use Python `logic2-automation`.
The project's preview wheel is **1.2.0**, server build **407561e0** (downloaded
2026-10-15). The GUI automation API tested earlier only exposed simulated
legacy Logic devices; it did not expose this MSO.

## Current hardware blocker

Saved `/tmp/las.log`, 2026-10-15 12:45:41, reports:

```
Firmware update required. device version=1.0.52, required version=1.0.54
Not waiting for wall-power, can't initialize
```

The MSO is visible on USB (`21a9:1007`), but no valid capture has been acquired.
Connect its wall power. A compatible Logic 2 GUI release must perform the
one-time firmware update to **1.0.54**, then close Logic 2 before running the
headless server. The required version is exact: installing some newer firmware
is not automatically compatible. If the GUI proposes a different version,
record it and obtain a matching server/GUI pair rather than guessing.
The installed headless server cannot update MSO firmware.

This is the one unavoidable GUI setup action requested from the user; it does
not replace the Python acquisition workflow. No automatic firmware update,
GUI operation or hardware capture was performed by this takeover.

Saleae documents MSO headless support, channel objects, the preview's firmware
matching restriction, and absence of a headless firmware updater in its
[official preview announcement](https://discuss.saleae.com/t/headless-logic2-automartion-support-for-logic-mso/3798).
The general [headless announcement](https://discuss.saleae.com/t/headless-logic2-automation-support/3793)
explains the server/Python interface. The bundled Python docstrings and proto
are the primary API reference for this preview.

## Setup

Use `uv` with the already downloaded MSO preview wheel (PyPI's stable wheel
may not have this MSO support):

```sh
uv venv .venv
uv pip install --python .venv/bin/python /home/h/.local/share/saleae-headless-407561e0/logic2_automation-1.2.0-py3-none-manylinux_2_27_x86_64.whl
```

Existing Linux udev rules are installed. Do not run hardware tools as root.
Only if rules are missing, stop and request administrator action for the exact
rule installation command, as required by the user's AGENTS.md.

`logic2.py` starts the bundled headless server on localhost port10431 using
`--port`, or connects to an existing server there. `LOGIC2_PORT` overrides it;
`LOGIC2_LOG` sets the default `/tmp/softusb-saleae-server.log` log path.
It reports a failed startup once, cleans up only its own child process, and
never opens Logic 2 or accepts a simulated capture as real evidence.

## Acquisition after wiring and firmware are ready

See [WIRING.md](WIRING.md) for revision-specific DK routing and voltage setup.
Use digital probe port0 D0=D+, D1=D-, D2=sync. Software indices are zero-based;
verify which physical smart-probe connector corresponds to port0. Change
`--probe-port` if needed. Use 10x passive probes with short grounds: physical
scope CH1=D+, CH2=D- (software indices0/1), both to DK GND. Set both physical
probe switches to10x; the script configures that attenuation explicitly.

```sh
.venv/bin/python tools/saleae/capture_usb.py --output captures/ls-001 --speed ls
# Optional analog acquisition; use a sample rate supported by this MSO/mode.
.venv/bin/python tools/saleae/capture_usb.py --output captures/ls-analog-001 --speed ls --analog-rate 100000000
```

Each new output directory receives `manifest.json`, `capture.sal`, `digital.csv`,
optional `analog.csv`, and `usb.csv` from the built-in **USB LS and FS** analyzer
at **Packets** level. The script configures the preview's MSO channel objects,
not legacy integer channel numbers. Analyzer setting names and choices were
checked against the bundled USB analyzer's exposed strings, but have not yet
been accepted by a real MSO capture. An API/settings mismatch fails explicitly;
raw evidence is saved before analyzer setup. No decode success is claimed yet.

Captures are timed (default2ms; maximum100ms). Firmware should repeat the
fixture so server start latency does not miss a single shot. No trigger is
required. Analog sample rate, firmware, timebase uncertainty, probe loading and
output supply must be recorded with measured results. Preview timed digital
modes may restrict analog rate; use a separate analog-trigger capture if the
required edge resolution is unavailable. Never infer nanosecond edge rates from
a low-rate analog trace.

## Decode and timing checks

`check_decode.py usb.csv expected.json` rejects empty output, reported errors,
and missing ordered regex expectations. Populate expectations with the actual
fixture's PID, payload and CRC fields after inspecting the exported CSV schema.
Matching SYNC/ACK/EOP alone is a smoke check, not complete protocol validation.

`measure_csv.py` takes an exact CSV channel name and an explicit packet-only
window. Prefer a SYNC-only window with `--sync-only` for individual bit timing.
For data, it reports transition-run averages divided by inferred bit counts;
that cannot resolve each unobserved bit boundary or certify jitter compliance.
Exclude EOP and idle. Supply the actual column names from the export header:

```sh
.venv/bin/python tools/saleae/measure_csv.py captures/ls-001/digital.csv --column 'ACTUAL_COLUMN' --start 0.00001 --stop 0.000014 --speed ls --sync-only
```

The timing estimator and decode checker have offline synthetic tests:

```sh
.venv/bin/python -m unittest discover -s tools/saleae -p test_analysis.py -v
```

These tests validate tooling behavior. They provide no DK timing measurements.
Analog rise/fall, differential crossover, eye mask, electrical loading and USB
receiver margin remain separate pending measurements. The tools do not issue
an electrical compliance verdict.

Preview wheel SHA256:
`a906116ec156f4c44cfd1cb650ad1cb5ff17aa03434549ebde454bfc3e683522`.
The pinned download is also recorded in `tools/saleae/requirements.txt`.
