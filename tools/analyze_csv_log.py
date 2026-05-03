#!/usr/bin/env python3
"""Analyze exported ESP32Mini CSV logs for Excel encoding and dirty samples."""

from __future__ import annotations

import csv
import sys
from pathlib import Path


EXPECTED_HEADER = [
    "minute",
    "count",
    "co2_ppm",
    "temp_c",
    "humidity_pct",
    "voc_index",
    "nox_index",
    "lux",
    "ok_ratio",
]


def fail(message: str) -> None:
    print(f"[FAIL] {message}")
    raise SystemExit(1)


def load_rows(path: Path) -> list[dict[str, float]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != EXPECTED_HEADER:
            fail(f"unexpected header: {reader.fieldnames}")
        rows: list[dict[str, float]] = []
        for index, raw in enumerate(reader, start=2):
            try:
                rows.append({key: float(value) for key, value in raw.items()})
            except ValueError as exc:
                fail(f"line {index} has non-numeric data: {exc}")
    return rows


def analyze(path: Path, require_bom: bool = False) -> int:
    data = path.read_bytes()
    has_bom = data.startswith(b"\xef\xbb\xbf")
    rows = load_rows(path)
    if not rows:
        fail("CSV has no data rows")

    zero_lux = [row for row in rows if row["lux"] == 0.0]
    zero_core = [
        row
        for row in rows
        if row["co2_ppm"] == 0.0 or row["temp_c"] == 0.0 or row["humidity_pct"] == 0.0
    ]
    low_count = [row for row in rows if row["count"] < 1.0]
    minute_gaps = []
    for prev, cur in zip(rows, rows[1:]):
        prev_minute = int(prev["minute"])
        cur_minute = int(cur["minute"])
        if cur_minute != prev_minute + 1:
            minute_gaps.append((prev_minute, cur_minute))

    print(f"rows={len(rows)}")
    print(f"utf8_bom={has_bom}")
    print(f"zero_lux_rows={len(zero_lux)}")
    print(f"zero_core_rows={len(zero_core)}")
    print(f"low_count_rows={len(low_count)}")
    print(f"minute_gaps={len(minute_gaps)}")
    if zero_lux:
        print("zero_lux_first=" + ",".join(str(int(row["minute"])) for row in zero_lux[:12]))
    if zero_core:
        print("zero_core_first=" + ",".join(str(int(row["minute"])) for row in zero_core[:12]))
    if minute_gaps:
        print("minute_gap_first=" + ",".join(f"{a}->{b}" for a, b in minute_gaps[:12]))

    failed = False
    if require_bom and not has_bom:
        print("[FAIL] CSV does not start with UTF-8 BOM, Excel may show mojibake")
        failed = True
    if zero_core:
        print("[FAIL] CSV contains zero core sensor rows")
        failed = True
    if minute_gaps:
        print("[FAIL] CSV contains non-monotonic or discontinuous minute sequence")
        failed = True
    if failed:
        return 1
    print("[PASS] CSV log analysis passed")
    return 0


def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: analyze_csv_log.py <log.csv> [--require-bom]")
        raise SystemExit(2)
    path = Path(sys.argv[1])
    require_bom = "--require-bom" in sys.argv[2:]
    raise SystemExit(analyze(path, require_bom=require_bom))


if __name__ == "__main__":
    main()
