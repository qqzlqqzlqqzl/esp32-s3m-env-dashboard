#!/usr/bin/env python3
"""Offline tests for the power regression gate.

The mainline script is expected at tools/power_regression_check.py. These tests
exercise it with JSON fixtures only, so they do not require a SmartUSBHub.
"""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[1]
POWER_CHECK = ROOT / "tools" / "power_regression_check.py"


def make_sample(mode: str, avg_ma: float, target_ma: float, voltage_v: float = 5.02) -> dict[str, Any]:
    values = [avg_ma - 3.0, avg_ma, avg_ma + 3.0]
    return {
        "mode": mode,
        "target_ma": target_ma,
        "threshold_ma": target_ma,
        "voltage_v": voltage_v,
        "avg_ma": avg_ma,
        "min_ma": min(values),
        "max_ma": max(values),
        "currents_ma": values,
        "voltages_mv": [voltage_v * 1000 for _ in values],
        "samples": [
            {
                "current_ma": value,
                "ma": value,
                "voltage_v": voltage_v,
                "voltage": voltage_v,
            }
            for value in values
        ],
    }


def combined_output(result: subprocess.CompletedProcess[str]) -> str:
    return result.stdout + result.stderr


def run_cli(sample: dict[str, Any]) -> subprocess.CompletedProcess[str] | None:
    with tempfile.TemporaryDirectory() as tmp:
        input_file = Path(tmp) / "power.json"
        input_file.write_text(json.dumps(sample), encoding="utf-8")
        result = subprocess.run(
            [
                sys.executable,
                str(POWER_CHECK),
                "--input-json",
                str(input_file),
                "--mode",
                str(sample["mode"]),
                "--target-ma",
                str(sample["target_ma"]),
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        output = combined_output(result).lower()
        if result.returncode == 2 and (
            "unrecognized arguments" in output
            or "no such option" in output
            or "--input-json" in output
        ):
            return None
        return result


def load_module() -> Any:
    spec = importlib.util.spec_from_file_location("power_regression_check", POWER_CHECK)
    assert spec and spec.loader, "cannot load tools/power_regression_check.py"
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def find_checker(module: Any) -> Callable[[dict[str, Any]], Any]:
    for name in (
        "check_power_regression",
        "evaluate_power_regression",
        "evaluate_sample",
        "evaluate",
        "check_sample",
    ):
        fn = getattr(module, name, None)
        if callable(fn):
            return fn
    raise AssertionError(
        "tools/power_regression_check.py must support --input-json or expose "
        "check_power_regression/evaluate_power_regression/evaluate_sample/evaluate/check_sample"
    )


def normalize_import_result(value: Any) -> tuple[int, str]:
    if isinstance(value, tuple):
        code = int(value[0])
        text = " ".join(str(part) for part in value[1:])
        return code, text
    if isinstance(value, dict):
        status = str(value.get("status") or value.get("verdict") or value.get("result") or "")
        code = int(value.get("exit_code", 0 if status.upper() == "PASS" else 1))
        return code, json.dumps(value, sort_keys=True)
    if isinstance(value, bool):
        return (0 if value else 1), str(value)
    if isinstance(value, int):
        return value, str(value)
    text = str(value)
    code = 0 if "PASS" in text.upper() else 1
    return code, text


def run_check(sample: dict[str, Any]) -> tuple[int, str]:
    if not POWER_CHECK.exists():
        raise FileNotFoundError(str(POWER_CHECK))

    cli_result = run_cli(sample)
    if cli_result is not None:
        return cli_result.returncode, combined_output(cli_result)

    checker = find_checker(load_module())
    return normalize_import_result(checker(sample))


def assert_report_fields(output: str, mode: str, target_ma: float) -> None:
    lowered = output.lower()
    for token in ("avg", "min", "max", "voltage", "mode"):
        assert token in lowered, f"output must include {token!r}: {output}"
    assert "threshold" in lowered or "target" in lowered, (
        f"output must include threshold/target information: {output}"
    )
    assert mode.lower() in lowered, output
    assert str(int(target_ma)) in output or str(float(target_ma)) in output, output
    assert any(status in output for status in ("[PASS]", "[WARN]", "[FAIL]")), output


def test_idle_under_target_passes() -> None:
    sample = make_sample("idle", avg_ma=67.0, target_ma=75.0)
    code, output = run_check(sample)

    assert code == 0, output
    assert "[PASS]" in output, output
    assert_report_fields(output, "idle", 75.0)


def test_idle_over_target_warns_or_fails() -> None:
    sample = make_sample("idle", avg_ma=90.0, target_ma=75.0)
    code, output = run_check(sample)

    assert "[FAIL]" in output or "[WARN]" in output, output
    if "[FAIL]" in output:
        assert code != 0, output
    assert_report_fields(output, "idle", 75.0)


def test_boost_under_target_passes() -> None:
    sample = make_sample("boost", avg_ma=125.0, target_ma=160.0)
    code, output = run_check(sample)

    assert code == 0, output
    assert "[PASS]" in output, output
    assert_report_fields(output, "boost", 160.0)


def main() -> None:
    if not POWER_CHECK.exists():
        print("[SKIP] tools/power_regression_check.py is not present yet")
        return

    tests = [
        test_idle_under_target_passes,
        test_idle_over_target_warns_or_fails,
        test_boost_under_target_passes,
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

    print(f"[PASS] {len(tests)} power regression tests")


if __name__ == "__main__":
    main()
