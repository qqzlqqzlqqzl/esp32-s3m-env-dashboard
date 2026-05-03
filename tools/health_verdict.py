#!/usr/bin/env python3
"""Independent ESP32Mini health verdict.

This script is intentionally read-only. It fetches or reads `/api/status` and
`/api/health`, then exits non-zero on objective health failures. Warnings keep
exit code 0 so hourly patrols can distinguish degraded-but-running devices from
hard failures.
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.request
from pathlib import Path
from typing import Any


SENSOR_KEYS = ("sht41", "scd41", "sgp41", "bh1750")
REQUIRED_TIME_KEYS = ("sync_time", "time_source", "epoch_s", "local_time")


def load_json_file(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{path} does not contain a JSON object")
    return data


def fetch_json(url: str, timeout: float) -> dict[str, Any]:
    request = urllib.request.Request(url, headers={"Cache-Control": "no-store"})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request, timeout=timeout) as response:
        charset = response.headers.get_content_charset() or "utf-8"
        body = response.read().decode(charset, errors="replace")
    data = json.loads(body)
    if not isinstance(data, dict):
        raise ValueError(f"{url} did not return a JSON object")
    return data


def get_nested(data: dict[str, Any], path: str, default: Any = None) -> Any:
    current: Any = data
    for part in path.split("."):
        if not isinstance(current, dict) or part not in current:
            return default
        current = current[part]
    return current


def as_int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


class Verdict:
    def __init__(self) -> None:
        self.failures: list[str] = []
        self.warnings: list[str] = []
        self.checks: list[str] = []

    def check(self, condition: bool, message: str) -> None:
        if condition:
            self.checks.append(message)
        else:
            self.failures.append(message)

    def warn_if(self, condition: bool, message: str) -> None:
        if condition:
            self.warnings.append(message)


def evaluate(status: dict[str, Any], health: dict[str, Any], args: argparse.Namespace) -> Verdict:
    verdict = Verdict()

    verdict.check(status.get("ok") is True, "/api/status ok=true")
    verdict.check(health.get("ok") is True, "/api/health ok=true")
    verdict.check(get_nested(status, "i2c.clock_hz") == 400000, "I2C clock is 400 kHz")

    for sensor in SENSOR_KEYS:
        verdict.check(get_nested(status, f"{sensor}.online") is True, f"{sensor} online")

    verdict.check(get_nested(status, "storage.mounted") is True, "LittleFS mounted")
    verdict.check(get_nested(status, "storage.ring_ready") is True, "minute ring ready in status")
    verdict.check(health.get("ring_ready") is True, "minute ring ready in health")
    verdict.check(health.get("sensors_ok") is True, "health sensors_ok=true")
    verdict.check(health.get("storage_ok") is True, "health storage_ok=true")
    verdict.check(health.get("power_ok") is True, "health power_ok=true")

    time_block = status.get("time")
    verdict.check(isinstance(time_block, dict), "time block present")
    if isinstance(time_block, dict):
        for key in REQUIRED_TIME_KEYS:
            verdict.check(key in time_block, f"time.{key} present")
        verdict.warn_if(time_block.get("sync_time") is not True, "time is not NTP/RTC synced")
        verdict.warn_if(time_block.get("time_source") == "uptime", "time_source is uptime")

    storage_failures = as_int(get_nested(status, "storage.failures"), default=0)
    loop_stalls = as_int(get_nested(status, "system.loop_stalls", health.get("loop_stalls")), default=0)
    wifi_reconnects = as_int(get_nested(status, "network.reconnects", health.get("wifi_reconnects")), default=0)
    max_loop_gap_ms = as_int(get_nested(status, "system.max_loop_gap_ms", health.get("max_loop_gap_ms")), default=0)
    max_persist_ms = as_int(get_nested(status, "storage.max_persist_duration_ms"), default=0)

    verdict.check(
        storage_failures <= args.max_storage_failures,
        f"storage failures <= {args.max_storage_failures} (observed {storage_failures})",
    )
    verdict.check(
        loop_stalls <= args.max_loop_stalls,
        f"loop stalls <= {args.max_loop_stalls} (observed {loop_stalls})",
    )
    verdict.check(
        wifi_reconnects <= args.max_wifi_reconnects,
        f"wifi reconnects <= {args.max_wifi_reconnects} (observed {wifi_reconnects})",
    )
    verdict.warn_if(
        max_loop_gap_ms > args.warn_max_loop_gap_ms,
        f"max loop gap {max_loop_gap_ms} ms exceeds warn threshold {args.warn_max_loop_gap_ms} ms",
    )
    verdict.warn_if(
        max_persist_ms > args.warn_max_persist_ms,
        f"max persist duration {max_persist_ms} ms exceeds warn threshold {args.warn_max_persist_ms} ms",
    )

    return verdict


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Read-only ESP32Mini health verdict")
    endpoint = parser.add_mutually_exclusive_group(required=True)
    endpoint.add_argument("--ip", help="Device IP, for example 192.168.124.67")
    endpoint.add_argument("--base-url", help="Device base URL, for example http://192.168.124.67")
    parser.add_argument("--status-file", type=Path, help="Use local /api/status JSON instead of HTTP")
    parser.add_argument("--health-file", type=Path, help="Use local /api/health JSON instead of HTTP")
    parser.add_argument("--timeout", type=float, default=8.0, help="HTTP timeout seconds")
    parser.add_argument("--max-storage-failures", type=int, default=0)
    parser.add_argument("--max-loop-stalls", type=int, default=0)
    parser.add_argument("--max-wifi-reconnects", type=int, default=3)
    parser.add_argument("--warn-max-loop-gap-ms", type=int, default=5000)
    parser.add_argument("--warn-max-persist-ms", type=int, default=2000)
    args = parser.parse_args(argv)

    if bool(args.status_file) != bool(args.health_file):
        parser.error("--status-file and --health-file must be used together")
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    base_url = (args.base_url or f"http://{args.ip}").rstrip("/")

    try:
        if args.status_file and args.health_file:
            status = load_json_file(args.status_file)
            health = load_json_file(args.health_file)
        else:
            status = fetch_json(f"{base_url}/api/status", args.timeout)
            health = fetch_json(f"{base_url}/api/health", args.timeout)
    except Exception as exc:  # noqa: BLE001 - CLI should turn all read errors into FAIL.
        print(f"[FAIL] health verdict input error: {exc}")
        return 2

    verdict = evaluate(status, health, args)
    for message in verdict.checks:
        print(f"[CHECK] {message}")
    for message in verdict.warnings:
        print(f"[WARN] {message}")
    for message in verdict.failures:
        print(f"[FAIL] {message}")

    if verdict.failures:
        print(f"[FAIL] health verdict failed: {len(verdict.failures)} failure(s), {len(verdict.warnings)} warning(s)")
        return 1

    print(f"[PASS] health verdict passed: {len(verdict.checks)} check(s), {len(verdict.warnings)} warning(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
