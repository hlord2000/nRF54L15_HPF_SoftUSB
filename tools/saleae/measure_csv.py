"""Measure transition timing inside an explicitly selected packet-only window.

Use a SYNC-only window for individual bit-cell jitter (all bits transition).
With NRZI data this measures edge-run durations divided by inferred bit count,
which cannot recover or certify individual unobserved bit boundaries.
"""
import argparse
import csv
import json
import math
from statistics import pstdev


def measure(times, values, bitrate, alternating=False):
    if len(times) != len(values) or len(times) < 3:
        raise ValueError("Need at least three timestamped values")
    if any(not math.isfinite(t) for t in times) or any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError("Timestamps must be finite and strictly increasing")
    if any(v not in (0, 1) for v in values):
        raise ValueError("Digital values must be 0 or 1")
    edges = [times[i] for i in range(1, len(times)) if values[i] != values[i-1]]
    period = 1 / bitrate
    durations = [b-a for a,b in zip(edges, edges[1:])]
    # A stuffed NRZI packet has no transition-free run above seven bit cells.
    runs = [round(d/period) for d in durations]
    if len(runs) < 4 or any(k < 1 or k > 7 for k in runs):
        raise ValueError("Window must contain only packet transitions, at least four intervals; exclude idle/EOP")
    if alternating and any(k != 1 for k in runs):
        raise ValueError("SYNC/alternating window contains a non-single-bit run")
    normalized = [d/k for d,k in zip(durations,runs)]
    estimated = sum(durations) / sum(runs)
    return dict(edge_intervals=len(runs), inferred_bits=sum(runs), nominal_bit_ns=period*1e9,
                estimated_bit_ns=estimated*1e9, frequency_error_percent=(period/estimated-1)*100,
                run_normalized_min_ns=min(normalized)*1e9,
                run_normalized_max_ns=max(normalized)*1e9,
                run_normalized_stddev_ns=pstdev(normalized)*1e9,
                edge_interval_residual_max_ns=max(abs(d-k*period) for d,k in zip(durations,runs))*1e9,
                measurement="single-bit intervals" if alternating else "NRZI edge-run averages; individual bit jitter unresolved",
                compliance="not determined; needs capture timebase accuracy, sampling uncertainty and electrical checks")


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("csv")
    p.add_argument("--column", required=True, help="exact digital.csv column name")
    p.add_argument("--time-column", default="Time [s]")
    p.add_argument("--start", type=float, required=True)
    p.add_argument("--stop", type=float, required=True)
    p.add_argument("--speed", choices=("ls","fs"), default="ls")
    p.add_argument("--sync-only", action="store_true")
    args=p.parse_args()
    with open(args.csv,newline="") as f:
        rows=list(csv.DictReader(f))
    rows=[r for r in rows if args.start <= float(r[args.time_column]) <= args.stop]
    result=measure([float(r[args.time_column]) for r in rows],
                   [int(r[args.column]) for r in rows],
                   1.5e6 if args.speed=="ls" else 12e6, args.sync_only)
    print(json.dumps(result,indent=2))


if __name__=="__main__":
    main()
