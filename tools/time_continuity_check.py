#!/usr/bin/env python3
"""Read-only NTP and minute-history continuity check for ESP32Mini."""

from __future__ import annotations

import argparse
import csv
import io
import json
import sys
import urllib.request
from pathlib import Path
from typing import Any


def fetch_text(url: str, timeout: float) -> str:
    request = urllib.request.Request(url, headers={"Cache-Control": "no-store"})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request, timeout=timeout) as response:
        charset = response.headers.get_content_charset() or "utf-8"
        return response.read().decode(charset, errors="replace")


def load_status(args: argparse.Namespace) -> dict[str, Any]:
    if args.status_file:
        data = json.loads(args.status_file.read_text(encoding="utf-8"))
    else:
        data = json.loads(fetch_text(f"{args.base_url}/api/status", args.timeout))
    if not isinstance(data, dict):
        raise ValueError("status JSON must be an object")
    return data


def load_history(args: argparse.Namespace) -> list[dict[str, Any]]:
    if args.history_file:
        data = json.loads(args.history_file.read_text(encoding="utf-8"))
    else:
        data = json.loads(fetch_text(f"{args.base_url}/api/history?range=all", args.timeout))
    if isinstance(data, dict):
        rows = data.get("rows", [])
    else:
        rows = data
    if not isinstance(rows, list):
        raise ValueError("history rows must be a list")
    return [row for row in rows if isinstance(row, dict)]


def load_csv_rows(args: argparse.Namespace) -> list[dict[str, str]]:
    if args.csv_file:
        text = args.csv_file.read_text(encoding="utf-8-sig")
    else:
        text = fetch_text(f"{args.base_url}/api/log.csv", args.timeout)
        if text.startswith("\ufeff"):
            text = text[1:]
    reader = csv.DictReader(io.StringIO(text))
    return list(reader)


def minute_values(rows: list[dict[str, Any]], label: str) -> list[int]:
    minutes: list[int] = []
    for index, row in enumerate(rows):
        if "minute" not in row:
            raise ValueError(f"{label} row {index} missing minute")
        try:
            minutes.append(int(float(row["minute"])))
        except (TypeError, ValueError) as exc:
            raise ValueError(f"{label} row {index} has invalid minute {row.get('minute')!r}") from exc
    return minutes


def first_non_monotonic(minutes: list[int]) -> tuple[int, int, int] | None:
    previous = None
    for index, minute in enumerate(minutes):
        if previous is not None and minute <= previous:
            return index, previous, minute
        previous = minute
    return None


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Read-only NTP/time continuity check")
    endpoint = parser.add_mutually_exclusive_group()
    endpoint.add_argument("--ip", help="Device IP, for example 192.168.124.67")
    endpoint.add_argument("--base-url", help="Device base URL")
    parser.add_argument("--status-file", type=Path)
    parser.add_argument("--history-file", type=Path)
    parser.add_argument("--csv-file", type=Path)
    parser.add_argument("--timeout", type=float, default=12.0)
    parser.add_argument("--allow-unsynced", action="store_true")
    args = parser.parse_args(argv)
    if not args.base_url and args.ip:
        args.base_url = f"http://{args.ip}"
    if args.base_url:
        args.base_url = args.base_url.rstrip("/")
    files = [args.status_file, args.history_file, args.csv_file]
    if any(files) and not all(files):
        parser.error("--status-file, --history-file, and --csv-file must be used together")
    if not args.base_url and not all(files):
        parser.error("provide --ip/--base-url or all three fixture files")
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    failures: list[str] = []
    checks: list[str] = []

    try:
        status = load_status(args)
        history_rows = load_history(args)
        csv_rows = load_csv_rows(args)
    except Exception as exc:  # noqa: BLE001 - CLI should turn read errors into FAIL.
        print(f"[FAIL] time continuity input error: {exc}")
        return 2

    time_block = status.get("time") if isinstance(status, dict) else None
    if not isinstance(time_block, dict):
        failures.append("status.time missing")
        time_block = {}

    sync_time = time_block.get("sync_time")
    time_source = str(time_block.get("time_source", "missing"))
    epoch_s = int(time_block.get("epoch_s") or 0)
    epoch_minute = int(time_block.get("epoch_minute") or 0)
    local_time = str(time_block.get("local_time", ""))

    if sync_time is True or args.allow_unsynced:
        checks.append(f"time sync_time={sync_time}")
    else:
        failures.append(f"time sync_time is not true: {sync_time}")
    if time_source not in ("uptime", "missing"):
        checks.append(f"time_source={time_source}")
    else:
        failures.append(f"time_source is {time_source}")
    if epoch_s > 0:
        checks.append(f"epoch_s={epoch_s}")
    else:
        failures.append(f"epoch_s invalid: {epoch_s}")
    if epoch_minute > 0:
        checks.append(f"epoch_minute={epoch_minute}")
    else:
        failures.append(f"epoch_minute invalid: {epoch_minute}")
    if local_time and local_time != "unsynced":
        checks.append(f"local_time={local_time}")
    else:
        failures.append(f"local_time invalid: {local_time}")

    try:
        history_minutes = minute_values(history_rows, "history")
        csv_minutes = minute_values(csv_rows, "csv")
    except ValueError as exc:
        failures.append(str(exc))
        history_minutes = []
        csv_minutes = []

    if history_minutes:
        bad = first_non_monotonic(history_minutes)
        if bad:
            index, previous, minute = bad
            failures.append(f"history minute non-monotonic at row {index}: {previous} -> {minute}")
        else:
            checks.append(f"history rows={len(history_minutes)} monotonic=true")
    else:
        failures.append("history has no minute rows")

    if csv_minutes:
        bad = first_non_monotonic(csv_minutes)
        if bad:
            index, previous, minute = bad
            failures.append(f"csv minute non-monotonic at row {index}: {previous} -> {minute}")
        else:
            checks.append(f"csv rows={len(csv_minutes)} monotonic=true")
    else:
        failures.append("csv has no minute rows")

    if history_minutes and csv_minutes and history_minutes[-1] != csv_minutes[-1]:
        failures.append(f"history/csv latest minute mismatch: {history_minutes[-1]} vs {csv_minutes[-1]}")
    elif history_minutes and csv_minutes:
        checks.append(f"latest_minute={history_minutes[-1]}")

    for check in checks:
        print(f"[CHECK] {check}")
    for failure in failures:
        print(f"[FAIL] {failure}")

    if failures:
        print(f"[FAIL] time continuity failed: {len(failures)} failure(s)")
        return 1
    print(f"[PASS] time continuity passed: {len(checks)} check(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
