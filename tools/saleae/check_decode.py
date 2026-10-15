"""Check Saleae analyzer CSV against explicit ordered regex expectations.

Expected JSON: {"patterns": ["SYNC", "ACK", "EOP"], "min_rows": 3}.
Use packet-specific PID/payload/CRC patterns after inspecting the actual CSV
format. Passing a weak expectation is not proof of packet correctness.
"""
import argparse
import csv
import json
import re


def check(rows, expectation):
    patterns=expectation.get("patterns", [])
    if not patterns or not all(isinstance(p,str) and p for p in patterns):
        raise ValueError("Nonempty ordered patterns are required")
    if len(rows) < max(1,expectation.get("min_rows",1)):
        raise ValueError("Empty or insufficient decoder output")
    text=[" | ".join(row.values()) for row in rows]
    errors=re.compile(r"\b(error|invalid|malformed|bad crc|crc mismatch|stuffing error)\b",re.I)
    bad=[s for s in text if errors.search(s)]
    if bad:
        raise ValueError(f"Decoder errors: {bad[:5]}")
    position=0
    for pattern in patterns:
        for i in range(position,len(text)):
            if re.search(pattern,text[i]):
                position=i+1
                break
        else:
            raise ValueError(f"Missing ordered decoder expectation: {pattern}")
    return {"status":"expected decode matched", "rows":len(rows), "patterns":len(patterns)}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("csv")
    p.add_argument("expectation")
    a=p.parse_args()
    with open(a.csv,newline="") as f:
        rows=list(csv.DictReader(f))
    with open(a.expectation) as f:
        expected=json.load(f)
    print(json.dumps(check(rows,expected),indent=2))


if __name__=="__main__":
    main()
