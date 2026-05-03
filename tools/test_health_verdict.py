#!/usr/bin/env python3
"""Offline tests for the independent health verdict CLI."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
HEALTH_VERDICT = ROOT / "tools" / "health_verdict.py"


def base_status() -> dict[str, Any]:
    return {
        "ok": True,
        "i2c": {"clock_hz": 400000},
        "sht41": {"online": True},
        "scd41": {"online": True},
        "sgp41": {"online": True},
        "bh1750": {"online": True},
        "storage": {
            "mounted": True,
            "ring_ready": True,
            "failures": 0,
            "max_persist_duration_ms": 80,
        },
        "system": {
            "loop_stalls": 0,
            "max_loop_gap_ms": 120,
        },
        "network": {"reconnects": 0},
        "time": {
            "sync_time": True,
            "time_source": "ntp",
            "epoch_s": 1777800000,
            "local_time": "2026-05-03 18:30:00",
        },
    }


def base_health() -> dict[str, Any]:
    return {
        "ok": True,
        "ring_ready": True,
        "sensors_ok": True,
        "storage_ok": True,
        "power_ok": True,
        "loop_stalls": 0,
        "wifi_reconnects": 0,
        "max_loop_gap_ms": 120,
    }


def run_verdict(status: dict[str, Any], health: dict[str, Any]) -> subprocess.CompletedProcess[str]:
    assert HEALTH_VERDICT.exists(), "tools/health_verdict.py must exist before running tests"
    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        status_file = tmp_dir / "status.json"
        health_file = tmp_dir / "health.json"
        status_file.write_text(json.dumps(status), encoding="utf-8")
        health_file.write_text(json.dumps(health), encoding="utf-8")
        return subprocess.run(
            [
                sys.executable,
                str(HEALTH_VERDICT),
                "--base-url",
                "http://offline.test",
                "--status-file",
                str(status_file),
                "--health-file",
                str(health_file),
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )


def combined_output(result: subprocess.CompletedProcess[str]) -> str:
    return result.stdout + result.stderr


def test_pass_sample() -> None:
    result = run_verdict(base_status(), base_health())
    output = combined_output(result)

    assert result.returncode == 0, output
    assert "[PASS] health verdict passed" in output
    assert "[CHECK] sht41 online" in output
    assert "[CHECK] scd41 online" in output
    assert "[CHECK] sgp41 online" in output
    assert "[CHECK] bh1750 online" in output
    assert "[CHECK] I2C clock is 400 kHz" in output
    assert "[CHECK] LittleFS mounted" in output
    assert "[CHECK] minute ring ready in health" in output
    assert "[CHECK] health power_ok=true" in output
    assert "[CHECK] time.local_time present" in output
    assert "[CHECK] loop stalls <= 0 (observed 0)" in output
    assert "[CHECK] wifi reconnects <= 3 (observed 0)" in output


def test_sensor_offline_fails() -> None:
    status = base_status()
    health = base_health()
    status["sgp41"]["online"] = False
    health["ok"] = False
    health["sensors_ok"] = False

    result = run_verdict(status, health)
    output = combined_output(result)

    assert result.returncode != 0, output
    assert "[FAIL] sgp41 online" in output
    assert "[FAIL] health verdict failed" in output


def test_i2c_clock_fails() -> None:
    status = base_status()
    health = base_health()
    status["i2c"]["clock_hz"] = 100000

    result = run_verdict(status, health)
    output = combined_output(result)

    assert result.returncode != 0, output
    assert "[FAIL] I2C clock is 400 kHz" in output


def test_warn_threshold_keeps_zero_exit() -> None:
    status = base_status()
    health = base_health()
    status["system"]["max_loop_gap_ms"] = 6000
    status["storage"]["max_persist_duration_ms"] = 2500

    result = run_verdict(status, health)
    output = combined_output(result)

    assert result.returncode == 0, output
    assert "[WARN] max loop gap 6000 ms exceeds warn threshold 5000 ms" in output
    assert "[WARN] max persist duration 2500 ms exceeds warn threshold 2000 ms" in output
    assert "[PASS] health verdict passed" in output


def main() -> None:
    tests = [
        test_pass_sample,
        test_sensor_offline_fails,
        test_i2c_clock_fails,
        test_warn_threshold_keeps_zero_exit,
    ]
    failures: list[tuple[str, str]] = []
    for test in tests:
        try:
            test()
        except AssertionError as exc:
            failures.append((test.__name__, str(exc)))

    if failures:
        for name, message in failures:
            print(f"[FAIL] {name}: {message}")
        sys.exit(1)

    print(f"[PASS] {len(tests)} health verdict tests")


if __name__ == "__main__":
    main()
