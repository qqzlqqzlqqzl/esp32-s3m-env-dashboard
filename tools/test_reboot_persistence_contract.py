#!/usr/bin/env python3
"""Offline contract tests for tools/reboot_persistence_check.ps1."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "reboot_persistence_check.ps1"
TESTING = ROOT / "TESTING.md"


def read(path: Path) -> str:
    assert path.exists(), f"{path.relative_to(ROOT)} must exist before running this contract"
    return path.read_text(encoding="utf-8")


def normalized(text: str) -> str:
    text = re.sub(r"<#[\s\S]*?#>", "", text)
    text = re.sub(r"#.*", "", text)
    return text.lower()


def assert_contains(text: str, needle: str, label: str) -> None:
    assert needle.lower() in text.lower(), f"{label} must contain {needle!r}"


def assert_regex(text: str, pattern: str, label: str) -> None:
    assert re.search(pattern, text, re.IGNORECASE | re.MULTILINE | re.DOTALL), (
        f"{label} must match /{pattern}/"
    )


def test_captures_before_and_after_readonly_state() -> None:
    text = read(SCRIPT)

    for endpoint in (
        "/api/status",
        "/api/history?range=all",
    ):
        assert_contains(text, endpoint, "reboot persistence check")

    for name in ("beforeStatus", "beforeConfig", "beforeHistory"):
        assert_regex(text, rf"\${name}\b", "before snapshot variables")

    for name in ("afterStatus", "afterHistory"):
        assert_regex(text, rf"\${name}\b", "after snapshot variables")
    assert_regex(text, r"(\$afterConfig\b|\$afterStatus\.config\b)", "after config snapshot")


def test_dangerous_mutations_are_blocked_by_default() -> None:
    text = read(SCRIPT)
    code = normalized(text)

    assert "/api/log/clear?confirm=1" not in code, (
        "reboot persistence check must never clear LittleFS/RAM logs"
    )
    assert_regex(text, r"param\s*\(", "PowerShell script should expose explicit parameters")

    config_posts = re.findall(
        r"(?:curl\.exe|invoke-webrequest|invoke-restmethod)[^\r\n;|]*-x\s+post[^\r\n;|]*?/api/config",
        code,
    )
    if config_posts:
        assert any(
            token in code
            for token in (
                "safeconfig",
                "restoreconfig",
                "knownsafeconfig",
                "allowconfigwrite",
                "setknownsafeconfig",
            )
        ), "POST /api/config must be behind an explicit safe/restore opt-in"
        assert "beforeconfig" in code and "restore" in code, (
            "any POST /api/config path must capture and restore the prior config"
        )


def test_reboot_trigger_and_recovery_wait_are_present() -> None:
    text = read(SCRIPT)

    assert_regex(
        text,
        r"(DtrEnable|RtsEnable|SerialPort|esptool|Restart-Computer|api/reboot|Reset-Device|Invoke-Reboot)",
        "reboot trigger",
    )
    assert_regex(text, r"(Start-Sleep|Wait-|Stopwatch|deadline|timeout)", "recovery wait loop")
    assert_regex(text, r"(while|for)\s*\(", "recovery should poll until the device is reachable")
    assert_regex(text, r"(/api/status|/api/health)", "recovery readiness endpoint")


def test_config_history_and_ring_are_validated_after_reboot() -> None:
    text = read(SCRIPT)

    assert_regex(text, r"(Compare-|ConvertTo-Json|ConvertFrom-Json|SequenceEqual|foreach)", "config comparison")
    for key in (
        "power_mode",
        "lcd_brightness_pct",
        "backlight_timeout_ms",
        "wifi_sta_sleep",
        "low_power_sensor_interval_ms",
    ):
        assert_contains(text, key, "persisted config key checks")

    assert_regex(text, r"(ring_count|minute_rows|ring_total_writes|rows)", "LittleFS ring/history counters")
    assert_regex(
        text,
        r"(afterHistory|afterRows|rowsAfter|minuteRowsAfter)[\s\S]{0,240}(-ge|>=)[\s\S]{0,160}(beforeHistory|beforeRows|rowsBefore|minuteRowsBefore)",
        "history rows must not shrink after reboot",
    )


def test_low_power_sta_only_contract_is_checked() -> None:
    text = read(SCRIPT)

    assert_contains(text, "low_power", "low-power mode check")
    assert_regex(text, r"(sta|station)", "STA mode check")
    assert_regex(text, r"(softap|ap_enabled|ap_|ap\s*mode|wifi_mode_ap)", "SoftAP disabled check")


def test_health_and_time_continuity_tools_are_run() -> None:
    text = read(SCRIPT)

    assert_contains(text, "health_verdict.py", "health verdict integration")
    assert_contains(text, "time_continuity_check.py", "time continuity integration")
    assert_regex(text, r"Assert-True|\[PASS\]|\[FAIL\]|throw", "script should fail loudly on contract violations")


def test_testing_docs_include_offline_contract_command() -> None:
    docs = read(TESTING)

    assert_contains(docs, "test_reboot_persistence_contract.py", "TESTING.md")
    assert_contains(docs, "reboot_persistence_check.ps1", "TESTING.md")
    assert_contains(docs, "no `/api/log/clear?confirm=1`", "TESTING.md")


def main() -> None:
    if not SCRIPT.exists():
        print("[SKIP] tools/reboot_persistence_check.ps1 is not present yet")
        return

    tests = [
        test_captures_before_and_after_readonly_state,
        test_dangerous_mutations_are_blocked_by_default,
        test_reboot_trigger_and_recovery_wait_are_present,
        test_config_history_and_ring_are_validated_after_reboot,
        test_low_power_sta_only_contract_is_checked,
        test_health_and_time_continuity_tools_are_run,
        test_testing_docs_include_offline_contract_command,
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

    print(f"[PASS] {len(tests)} reboot persistence contract tests")


if __name__ == "__main__":
    main()
