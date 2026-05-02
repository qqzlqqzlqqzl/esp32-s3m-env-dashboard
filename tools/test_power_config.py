#!/usr/bin/env python3
"""Host-side power-management contract tests.

These tests intentionally parse the Arduino sketch and project docs as text.
They define the low-power feature surface that the firmware should expose
before board-side integration tests are trusted.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INO = ROOT / "esp32_s3m_env_dashboard.ino"
README = ROOT / "README.md"
TESTING = ROOT / "TESTING.md"

POWER_CONFIG_KEYS = [
    "power_mode",
    "lcd_brightness_pct",
    "backlight_timeout_ms",
    "wifi_sta_sleep",
    "low_power_sensor_interval_ms",
]

POWER_STATUS_KEYS = [
    "power",
    "power_mode",
    "lcd_brightness_pct",
    "backlight_on",
    "backlight_timeout_ms",
    "wifi_sta_sleep",
    "sensor_interval_ms",
]

POWER_HEALTH_KEYS = [
    "power_mode",
    "backlight_on",
    "wifi_sta_sleep",
    "sleep_eligible",
]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def assert_contains(text: str, needle: str, label: str) -> None:
    assert needle in text, f"{label} must contain {needle!r}"


def assert_regex(text: str, pattern: str, label: str) -> None:
    assert re.search(pattern, text, re.MULTILINE | re.DOTALL), (
        f"{label} must match /{pattern}/"
    )


def extract_struct_body(source: str, name: str) -> str:
    match = re.search(rf"struct\s+{re.escape(name)}\s*\{{(?P<body>.*?)\}}\s*\w*\s*;", source, re.DOTALL)
    assert match, f"{name} struct not found"
    return match.group("body")


def extract_function_body(source: str, name: str) -> str:
    match = re.search(rf"\b(?:void|bool|String)\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
    assert match, f"{name} function not found"
    start = match.end()
    depth = 1
    i = start
    while i < len(source) and depth:
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
        i += 1
    assert depth == 0, f"{name} function body is not balanced"
    return source[start : i - 1]


def extract_html(source: str) -> str:
    match = re.search(r'const\s+char\s+kIndexHtml\[\]\s+PROGMEM\s*=\s*R"HTML\((?P<html>.*?)\)HTML";', source, re.DOTALL)
    assert match, "kIndexHtml raw string not found"
    return match.group("html")


def test_power_config_defaults_and_persistence() -> None:
    source = read(INO)
    config = extract_struct_body(source, "EnvConfig")
    load_config = extract_function_body(source, "loadConfig")
    save_config = extract_function_body(source, "saveConfig")

    for key in POWER_CONFIG_KEYS:
        assert_contains(load_config, key, "loadConfig")
        assert_contains(save_config, key, "saveConfig")

    assert_regex(config, r"(powerMode|power_mode)\s*=\s*(PowerMode::)?(POWER_)?BALANCED", "EnvConfig")
    assert_regex(config, r"(lcdBrightnessPct|lcd_brightness_pct)\s*=\s*(6[0-9]|7[0-9]|80)", "EnvConfig")
    assert_regex(config, r"(backlightTimeoutMs|backlight_timeout_ms)\s*=\s*(30000|60000)", "EnvConfig")
    assert_regex(config, r"(wifiStaSleep|wifi_sta_sleep)\s*=\s*true", "EnvConfig")
    assert_regex(config, r"(lowPowerSensorIntervalMs|low_power_sensor_interval_ms)\s*=\s*(30000|60000)", "EnvConfig")


def test_power_config_api_validation() -> None:
    source = read(INO)
    normalize_config = extract_function_body(source, "normalizeConfig")
    handle_config = extract_function_body(source, "handleConfig")

    for key in POWER_CONFIG_KEYS:
        assert_contains(handle_config, key, "handleConfig")

    assert_regex(normalize_config, r"(lcdBrightnessPct|lcd_brightness_pct).*clampU32\([^;]+0UL[^;]+100UL", "normalizeConfig")
    assert_regex(normalize_config, r"(backlightTimeoutMs|backlight_timeout_ms).*clampU32\([^;]+5000UL[^;]+600000UL", "normalizeConfig")
    assert_regex(normalize_config, r"(lowPowerSensorIntervalMs|low_power_sensor_interval_ms).*clampU32\([^;]+30000UL[^;]+600000UL", "normalizeConfig")
    assert_regex(handle_config, r'parse(?:Uint|Bool|Enum)Arg\("power_mode"', "handleConfig")
    assert_regex(handle_config, r'parse(?:Uint|Bool)Arg\("wifi_sta_sleep"', "handleConfig")


def test_status_and_health_expose_power_fields() -> None:
    source = read(INO)
    status_json = extract_function_body(source, "statusJson")
    health = extract_function_body(source, "handleHealth")

    for key in POWER_STATUS_KEYS:
        assert_contains(status_json, key, "statusJson")

    for key in POWER_HEALTH_KEYS:
        assert_contains(health, key, "handleHealth")

    assert_regex(status_json, r'"power"\s*:\s*\{', "statusJson")
    assert_regex(health, r'"power_ok"\s*:', "handleHealth")


def test_wifi_sleep_and_lcd_backlight_hooks_exist() -> None:
    source = read(INO)

    assert_regex(source, r"WiFi\.setSleep\(\s*cfg\.(wifiStaSleep|wifi_sta_sleep)\s*\)", "WiFi power save")
    assert_regex(source, r"(ledcWrite|analogWrite|setBrightness|writeBrightness)\([^;]*(lcdBrightnessPct|lcd_brightness_pct)", "LCD brightness control")
    assert_regex(source, r"(backlightTimeoutMs|backlight_timeout_ms)", "backlight timeout control")
    assert_regex(source, r"(millis\(\)\s*-\s*gLast.*ActivityMs|gLast.*ActivityMs\s*\+)", "backlight idle activity tracking")


def test_root_html_contains_power_controls() -> None:
    html = extract_html(read(INO))

    for token in ["power", "LCD", "WiFi"]:
        assert_contains(html, token, "root HTML")

    for key in POWER_CONFIG_KEYS:
        assert_contains(html, key, "root HTML")


def test_docs_describe_power_contract() -> None:
    docs = read(README) + "\n" + read(TESTING)

    for token in [
        "Power optimization",
        "power_mode",
        "lcd_brightness_pct",
        "backlight_timeout_ms",
        "wifi_sta_sleep",
        "low_power_sensor_interval_ms",
        "/api/status",
        "/api/health",
        "tools/test_power_config.py",
    ]:
        assert_contains(docs, token, "README/TESTING")


def main() -> None:
    tests = [
        test_power_config_defaults_and_persistence,
        test_power_config_api_validation,
        test_status_and_health_expose_power_fields,
        test_wifi_sleep_and_lcd_backlight_hooks_exist,
        test_root_html_contains_power_controls,
        test_docs_describe_power_contract,
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
    print(f"[PASS] {len(tests)} power contract tests")


if __name__ == "__main__":
    main()
