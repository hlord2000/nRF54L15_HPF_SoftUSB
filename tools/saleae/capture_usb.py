"""Capture real bench traffic via Saleae MSO headless Python automation.

No UI control. Run --help without hardware. No capture is claimed verified until
an expected packet sequence has been compared with the resulting analyzer CSV.
"""
import argparse
import dataclasses
import json
from pathlib import Path
import sys

from saleae import automation as a
from logic2 import connect


def device_config(probe_port, analog_rate, rx_probe_port=None):
    scope = []
    if analog_rate:
        scope = [a.MsoScopeChannelConfiguration(
            index=i, analog=a.MsoScopeAnalogChannelConfiguration(
                center_voltage=1.65, voltage_range=5.0, probe_attenuation=10.0))
                 for i in (0, 1)]
    probes = [a.MsoDigitalProbeConfiguration(
        port_index=probe_port, threshold_voltage=1.65,
        channels=[a.MsoDigitalProbeChannelConfiguration(index=i)
                  for i in ((0, 1, 2, 3) if rx_probe_port is not None else (0, 1, 2))])]
    if rx_probe_port is not None:
        if rx_probe_port == probe_port:
            raise ValueError("RX_DM needs a second four-channel smart probe port")
        probes.append(a.MsoDigitalProbeConfiguration(
            port_index=rx_probe_port, threshold_voltage=1.65,
            channels=[a.MsoDigitalProbeChannelConfiguration(index=0)]))
    return a.MsoDeviceConfiguration(
        scope_channels=scope, analog_sample_rate=analog_rate or None,
        digital_probes=probes)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--speed", choices=("ls", "fs"), default="ls")
    p.add_argument("--probe-port", type=int, default=0, help="MSO digital probe port index; verify physical port")
    p.add_argument("--rx-probe-port", type=int, help="second smart-probe port: RX_DM on its D0; RX_DP on primary D3")
    p.add_argument("--analog-rate", type=int, default=0, help="0 disables analog; e.g. 100000000 if supported")
    p.add_argument("--seconds", type=float, default=0.002)
    p.add_argument("--device-id", help="required when more than one real MSO is present")
    args = p.parse_args()
    if not 0 < args.seconds <= 0.1 or args.analog_rate < 0 or args.probe_port < 0:
        p.error("duration must be >0 and <=0.1 s; rate and port must be nonnegative")
    if args.rx_probe_port is not None and (args.rx_probe_port < 0 or args.rx_probe_port == args.probe_port):
        p.error("--rx-probe-port must be a distinct nonnegative port")
    args.output.mkdir(parents=True, exist_ok=False)
    manifest = dict(status="started", source="real-hardware", speed=args.speed,
                    seconds=args.seconds, analog_rate_requested=args.analog_rate,
                    probe_port=args.probe_port, rx_probe_port=args.rx_probe_port, analog_probe_attenuation=10,
                    validation="pending expected-packet comparison; no compliance claim")
    try:
        with connect() as manager:
            manifest["app"] = dataclasses.asdict(manager.get_app_info())
            devices = [d for d in manager.get_devices() if not d.is_simulation
                       and d.device_type == a.DeviceType.LOGIC_MSO
                       and (args.device_id is None or d.device_id == args.device_id)]
            if len(devices) != 1:
                raise RuntimeError(f"Expected one real MSO, found {len(devices)}. Check wall power and FW1.0.54; see docs/AUTOMATION.md")
            device = devices[0]
            manifest["device_id"] = device.device_id
            config = a.CaptureConfiguration(capture_mode=a.TimedCaptureMode(duration_seconds=args.seconds))
            with manager.start_capture(device_id=device.device_id,
                                       device_configuration=device_config(args.probe_port, args.analog_rate, args.rx_probe_port),
                                       capture_configuration=config) as capture:
                capture.wait(timeout_seconds=30)
                # Save evidence before adding the analyzer, so decode failure preserves data.
                capture.save_capture(filepath=str((args.output / "capture.sal").resolve()))
                channels = [a.DigitalProbeChannel(port_index=args.probe_port, index=i) for i in (0, 1, 2)]
                if args.rx_probe_port is not None:
                    channels += [a.DigitalProbeChannel(port_index=args.probe_port, index=3),
                                 a.DigitalProbeChannel(port_index=args.rx_probe_port, index=0)]
                if args.analog_rate:
                    channels += [a.ScopeChannel(index=i, type=a.ScopeChannelType.ANALOG) for i in (0, 1)]
                capture.export_raw_data_csv(directory=str(args.output.resolve()), mso_channels=channels)
                analyzer = capture.add_analyzer("USB LS and FS", label="SoftUSB",
                    settings={"USB D+ (green)": channels[0], "USB D- (white)": channels[1],
                              "USB bit-rate": "Low speed (1.5 Mbps)" if args.speed == "ls" else "Full speed (12 Mbps)",
                              "USB decode level": "Packets"})
                capture.export_data_table(filepath=str((args.output / "usb.csv").resolve()), analyzers=[analyzer])
                manifest["status"] = "captured; decode exported; validation pending"
    except Exception as exc:
        manifest["status"] = "failed"
        manifest["error"] = str(exc)
        raise
    finally:
        (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"Capture failed: {exc}", file=sys.stderr)
        sys.exit(1)
