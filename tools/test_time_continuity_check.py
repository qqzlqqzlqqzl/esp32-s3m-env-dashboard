#!/usr/bin/env python3
"""Offline tests for the NTP/time continuity checker."""

from __future__ import annotations

import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
TIME_CHECK = ROOT / "tools" / "time_continuity_check.py"


def base_status() -> dict[str, Any]:
    return {
        "time": {
            "sync_time": True,
            "time_source": "ntp_rtc",
            "epoch_s": 1777804200,
            "epoch_minute": 29630070,
            "local_time": "2026-05-03 19:50:00",
        }
    }


def make_history(minutes: list[int]) -> list[dict[str, Any]]:
    return [
        {
            "minute": minute,
            "temperature_c": 24.5 + index,
            "humidity_pct": 48.0,
        }
        for index, minute in enumerate(minutes)
    ]


def make_csv_text(minutes: list[int]) -> str:
    rows = [{"minute": str(minute), "temperature_c": f"{24.5 + index:.1f}"} for index, minute in enumerate(minutes)]
    output = ["minute,temperature_c"]
    output.extend(f"{row['minute']},{row['temperature_c']}" for row in rows)
    return "\n".join(output) + "\n"


def run_check(
    status: dict[str, Any],
    history_minutes: list[int],
    csv_minutes: list[int],
) -> subprocess.CompletedProcess[str]:
    assert TIME_CHECK.exists(), "tools/time_continuity_check.py must exist before running tests"
    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        status_file = tmp_dir / "status.json"
        history_file = tmp_dir / "history.json"
        csv_file = tmp_dir / "log.csv"
        status_file.write_text(json.dumps(status), encoding="utf-8")
        history_file.write_text(json.dumps({"rows": make_history(history_minutes)}), encoding="utf-8")
        csv_file.write_text(make_csv_text(csv_minutes), encoding="utf-8")
        return subprocess.run(
            [
                sys.executable,
                str(TIME_CHECK),
                "--status-file",
                str(status_file),
                "--history-file",
                str(history_file),
                "--csv-file",
                str(csv_file),
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )


def combined_output(result: subprocess.CompletedProcess[str]) -> str:
    return result.stdout + result.stderr


def assert_common_report_fields(output: str) -> None:
    for token in (
        "time_source",
        "epoch_minute",
        "monotonic",
    ):
        assert token in output, f"output missing {token!r}: {output}"
    assert "history rows" in output or "history minute non-monotonic" in output, output
    assert "csv rows" in output or "csv minute non-monotonic" in output, output
    assert "[PASS]" in output or "[FAIL]" in output, output


def assert_csv_fixture_is_valid() -> None:
    rows = list(csv.DictReader(make_csv_text([29630068, 29630069]).splitlines()))
    assert [row["minute"] for row in rows] == ["29630068", "29630069"]


def test_pass_sample_ntp_rtc_and_strict_minutes() -> None:
    result = run_check(base_status(), [29630068, 29630069, 29630070], [29630068, 29630069, 29630070])
    output = combined_output(result)

    assert result.returncode == 0, output
    assert "[PASS] time continuity passed" in output
    assert "[CHECK] time_source=ntp_rtc" in output
    assert "[CHECK] epoch_minute=29630070" in output
    assert "[CHECK] local_time=2026-05-03 19:50:00" in output
    assert "[CHECK] history rows=3 monotonic=true" in output
    assert "[CHECK] csv rows=3 monotonic=true" in output


def test_unsynced_uptime_fails() -> None:
    status = base_status()
    status["time"] = {
        "sync_time": False,
        "time_source": "uptime",
        "epoch_s": 0,
        "epoch_minute": 0,
        "local_time": "unsynced",
    }

    result = run_check(status, [29630068, 29630069, 29630070], [29630068, 29630069, 29630070])
    output = combined_output(result)

    assert result.returncode != 0, output
    assert "[FAIL] time sync_time is not true: False" in output
    assert "[FAIL] time_source is uptime" in output
    assert "[FAIL] epoch_minute invalid: 0" in output
    assert "[FAIL] time continuity failed" in output
    assert_common_report_fields(output)


def test_history_repeated_minute_fails() -> None:
    result = run_check(base_status(), [29630068, 29630069, 29630069], [29630068, 29630069, 29630070])
    output = combined_output(result)

    assert result.returncode != 0, output
    assert "[FAIL] history minute non-monotonic at row 2: 29630069 -> 29630069" in output
    assert "[CHECK] csv rows=3 monotonic=true" in output
    assert "[FAIL] time continuity failed" in output
    assert_common_report_fields(output)


def test_csv_minute_rollback_fails() -> None:
    result = run_check(base_status(), [29630068, 29630069, 29630070], [29630068, 29630070, 29630069])
    output = combined_output(result)

    assert result.returncode != 0, output
    assert "[CHECK] history rows=3 monotonic=true" in output
    assert "[FAIL] csv minute non-monotonic at row 2: 29630070 -> 29630069" in output
    assert "[FAIL] time continuity failed" in output
    assert_common_report_fields(output)


def main() -> None:
    tests = [
        assert_csv_fixture_is_valid,
        test_pass_sample_ntp_rtc_and_strict_minutes,
        test_unsynced_uptime_fails,
        test_history_repeated_minute_fails,
        test_csv_minute_rollback_fails,
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

    print(f"[PASS] {len(tests)} time continuity tests")


if __name__ == "__main__":
    main()
