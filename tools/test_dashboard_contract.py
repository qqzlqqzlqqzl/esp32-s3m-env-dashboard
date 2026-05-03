#!/usr/bin/env python3
"""Host-side dashboard/API contract tests for history UX, log clear, and time sync."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INO = ROOT / "esp32_s3m_env_dashboard.ino"
README = ROOT / "README.md"
TESTING = ROOT / "TESTING.md"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def extract_html(source: str) -> str:
    match = re.search(
        r'const\s+char\s+kIndexHtml\[\]\s+PROGMEM\s*=\s*R"HTML\((?P<html>.*?)\)HTML";',
        source,
        re.DOTALL,
    )
    assert match, "kIndexHtml raw string not found"
    return match.group("html")


def extract_function_body(source: str, name: str) -> str:
    match = re.search(rf"\b(?:void|bool|String|uint32_t|SampleRow)\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
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


def assert_contains(text: str, needle: str, label: str) -> None:
    assert needle in text, f"{label} must contain {needle!r}"


def assert_regex(text: str, pattern: str, label: str) -> None:
    assert re.search(pattern, text, re.MULTILINE | re.DOTALL), (
        f"{label} must match /{pattern}/"
    )


def test_history_frontend_cache_and_fast_range_switch() -> None:
    source = read(INO)
    html = extract_html(source)

    for token in [
        "minuteCache",
        "ensureMinuteCache",
        "sliceRowsForRange",
        "renderHistoryRange",
        "requestAnimationFrame",
        "historyMeta",
    ]:
        assert_contains(html, token, "root HTML")

    assert_regex(html, r"rangeBtn[\s\S]+renderHistoryRange", "range buttons should render from cache")
    assert_regex(html, r"fetch\('/api/history\?range=all'", "all minute history should be fetched once into cache")
    assert_regex(html, r"MAX_CHART_POINTS", "chart drawing should decimate large datasets")
    assert_contains(source, "readMinuteRingSlotCached", "minute ring streaming")
    for token in [
        "appendBufferedContent",
        "flushBufferedContent",
        "minuteRecordJsonLine",
        "minuteRecordCsvLine",
    ]:
        assert_contains(source, token, "buffered minute history streaming")


def test_web_interaction_boost_contract() -> None:
    source = read(INO)
    html = extract_html(source)

    for token in [
        "boostPerf",
        "/api/performance/boost",
        "webBoostActive",
        "noteWebActivity",
        "noteWebActivity(kDefaultWebBoostMs)",
        "web_boost_active",
        "web_boost_until_ms",
    ]:
        assert_contains(source + "\n" + html, token, "web boost contract")

    assert_regex(source, r"WiFi\.setSleep\([^;]*webBoostActive\(\)", "web boost should disable WiFi sleep while active")
    assert_regex(html, r"fetch\('/api/performance/boost", "HTML boost button should call boost endpoint")


def test_log_clear_button_and_post_endpoint() -> None:
    source = read(INO)
    html = extract_html(source)

    assert_contains(html, "clearLog", "root HTML")
    assert_contains(html, "清零", "root HTML")
    assert_contains(html, "confirm(", "clear log UX")
    assert_contains(html, "/api/log/clear", "clear log UX")

    clear_body = extract_function_body(source, "handleLogClear")
    assert_contains(clear_body, "HTTP_POST", "handleLogClear")
    assert_contains(clear_body, "confirm", "handleLogClear")
    assert_contains(clear_body, "400", "handleLogClear")
    assert_contains(clear_body, "clearMinuteLog", "handleLogClear")
    assert_contains(source, 'server.on("/api/log/clear"', "server routes")


def test_log_export_quality_guards() -> None:
    source = read(INO)
    make_sample = extract_function_body(source, "makeSample")
    publish_sample = extract_function_body(source, "publishSampleIfDue")
    update_aggregate = extract_function_body(source, "updateMinuteAggregate")
    log_download = extract_function_body(source, "handleLogDownload")
    clear_log = extract_function_body(source, "clearMinuteLog")
    status_json = extract_function_body(source, "statusJson")

    assert_contains(log_download, "\\xEF\\xBB\\xBFminute,count", "CSV download should include UTF-8 BOM for Excel")
    assert_contains(make_sample, "env.shtOk > 0", "sample validity should require SHT readings")
    assert_contains(make_sample, "env.scdOk > 0", "sample validity should require SCD readings")
    assert_contains(make_sample, "env.sgpOk > 0", "sample validity should require SGP readings")
    assert_contains(make_sample, "env.bhOk > 0", "sample validity should require BH1750 readings")
    assert_contains(make_sample, "env.sgpConditioning == 0", "sample validity should reject SGP41 conditioning data")
    assert_contains(make_sample, "row.co2 > 0", "sample validity should reject zero CO2")
    assert_contains(make_sample, "env.srawVoc > 0", "sample validity should reject empty SGP raw VOC")
    assert_contains(make_sample, "env.srawNox > 0", "sample validity should reject empty SGP raw NOx")
    assert_contains(publish_sample, "shouldHoldLoggingForTimeSync", "logging should wait for real time when STA is configured")
    assert_contains(publish_sample, "gDroppedUnsyncedSamples", "unsynced drops should be counted")
    assert_contains(publish_sample, "gDroppedInvalidSamples", "invalid drops should be counted")
    assert_contains(update_aggregate, "minuteKeyIsEpoch", "minute aggregation should detect time-domain switches")
    assert_contains(update_aggregate, "gDroppedTimeDomainSamples", "time-domain drops should be counted")
    for token in ["dropped_invalid_samples", "dropped_unsynced_samples", "dropped_time_domain_samples"]:
        assert_contains(status_json, token, "statusJson should expose dropped sample counters")
    for token in ["gDroppedInvalidSamples", "gDroppedUnsyncedSamples", "gDroppedTimeDomainSamples"]:
        assert_contains(clear_log, token, "clear log should reset drop counters")


def test_http_read_only_patrol_script_exists() -> None:
    script = ROOT / "tools" / "http_perf_check.ps1"
    text = read(script)

    for token in [
        "/api/status",
        "/api/health",
        "/api/history?range=60",
        "/api/history?range=all",
        "/api/log.csv",
        "minuteCache",
        "ensureMinuteCache",
        "数据清零",
        "加速查看",
        "web_boost_active",
        "CurlMaxSeconds",
    ]:
        assert_contains(text, token, "http_perf_check.ps1")

    assert_contains(text, "/api/log/clear", "http_perf_check.ps1")
    assert_contains(text, "confirm=1", "http_perf_check.ps1")
    assert_regex(text, r"-X\s+POST.+/api/log/clear(?!\?confirm=1)", "clear protection should test unconfirmed POST only")
    assert "api/config" not in text, "read-only patrol must not POST /api/config"


def test_time_sync_contract() -> None:
    source = read(INO)
    html = extract_html(source)

    for token in [
        "#include <time.h>",
        "kNtpServer3",
        "ntp.aliyun.com",
        "configTzTime",
        "kNtpServer1, kNtpServer2, kNtpServer3",
        "maintainTimeSync",
        "currentMinuteKey",
        "time_source",
        "epoch_s",
        "local_time",
    ]:
        assert_contains(source, token, "time sync firmware")

    assert_contains(html, "timeState", "root HTML")
    assert_contains(html, "sync_time", "root HTML")
    assert_contains(html, "formatMinuteLabel", "root HTML")


def test_single_button_lcd_menu_contract() -> None:
    source = read(INO)

    for token in [
        "ButtonEvent",
        "BUTTON_SHORT",
        "BUTTON_LONG",
        "gMenuMode",
        "handleShortPress",
        "handleLongPress",
        "drawSettingsMenu",
        "applyMenuSelection",
    ]:
        assert_contains(source, token, "single button LCD menu")

    for setting in [
        "低功耗",
        "均衡",
        "性能",
        "背光",
        "WiFi省电",
        "SHT精度",
        "BH模式",
    ]:
        assert_contains(source, setting, "LCD menu labels")


def test_docs_describe_new_dashboard_contract() -> None:
    docs = read(README) + "\n" + read(TESTING)
    for token in [
        "history cache",
        "/api/log/clear",
        "NTP",
        "time_source",
        "数据清零",
        "网页加速",
        "BOOT",
        "长按",
    ]:
        assert_contains(docs, token, "README/TESTING")


def main() -> None:
    tests = [
        test_history_frontend_cache_and_fast_range_switch,
        test_web_interaction_boost_contract,
        test_log_clear_button_and_post_endpoint,
        test_log_export_quality_guards,
        test_http_read_only_patrol_script_exists,
        test_time_sync_contract,
        test_single_button_lcd_menu_contract,
        test_docs_describe_new_dashboard_contract,
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
    print(f"[PASS] {len(tests)} dashboard contract tests")


if __name__ == "__main__":
    main()
