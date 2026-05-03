# Testing

This project uses a zero-trust smoke test. A build that only "looks OK" is not
accepted.

The old `esp32_sensor_hub` project is useful as a test-closure reference, but
only its engineering pattern is reusable here. Keep build/upload proof, serial
URL discovery, API health checks, dashboard checks, config persistence, reboot
persistence, soak checks, and explicit PASS evidence. Do not inherit its DHT,
camera, audio, AP3216C, QMA6100P, XL9555, or old LCD requirements.

Run from PowerShell:

```powershell
$env:WIFI_STA_SSID="your-ssid"
$env:WIFI_STA_PASS="your-password"
.\tools\smoke_test.ps1 -Port COM20 -Ip 192.168.124.67
```

Host-only tests:

```powershell
python .\tools\test_ring_log.py
python .\tools\test_power_config.py
python .\tools\test_power_regression_check.py
python .\tools\test_dashboard_contract.py
python .\tools\test_health_verdict.py
python -m py_compile .\tools\analyze_csv_log.py
```

`tools/test_power_regression_check.py` is an offline power gate test. It waits
for `tools/power_regression_check.py`, then feeds JSON fixtures through
`--input-json` or an importable checker function and verifies PASS/WARN/FAIL
output without using a real SmartUSBHub.

Downloaded CSV log quality check:

```powershell
python .\tools\analyze_csv_log.py "C:\Users\lyl\Downloads\log (1).csv" --require-bom
```

This check fails if the export lacks the UTF-8 BOM needed for reliable Excel
opening, contains zero core sensor rows, or has non-monotonic minute keys. Gaps
are reported because old dirty rows may be filtered out, but gaps alone do not
fail the export.

Hardware read-only perf check (no `/api/config`, no data clear):

```powershell
.\tools\http_perf_check.ps1 -Port COM20
```

Hourly read-only QA patrol runner:

```powershell
.\tools\hourly_qa_patrol.ps1 -Port COM20 -WithBoost
```

This wraps git status, host tests, `health_verdict.py`, `http_perf_check.ps1`,
browser UX range-click checks, and optional SmartUSBHub CH1 power sampling. It
does not call `/api/config` and does not clear logs.

Independent health verdict only:

```powershell
python .\tools\health_verdict.py --ip 192.168.124.67
```

This is a read-only objective checker for `/api/status` and `/api/health`.
It exits non-zero for hard health failures and prints `[WARN]` for degraded but
still-running conditions.

Browser UX range-click check (headless Chrome, no npm dependencies):

```powershell
node .\tools\browser_ux_check.mjs --url http://192.168.124.67/
```

Use `--chrome "C:\Program Files\Google\Chrome\Application\chrome.exe"` if
Chrome is installed in a non-default location, and `--port 9222` when a fixed
Chrome DevTools Protocol port is needed. The check opens an isolated temporary
profile, waits for the dashboard minute history preload, clicks 实时/1小时/6小时/1天/3天/全部分钟,
fails duplicate `/api/history?range=all` requests, verifies all chart canvases
are non-empty, and prints `[PASS]` with timing metrics.

Optional: include a temporary web boost window for more stable timing:

```powershell
.\tools\http_perf_check.ps1 -Port COM20 -WithBoost
```

Power measurement:

```powershell
python .\tools\measure_ch1_current.py --port COM9 --channel 1 --samples 80
```

Power regression gate:

```powershell
python .\tools\power_regression_check.py --mode idle --port COM9 --channel 1 --samples 80
python .\tools\power_regression_check.py --mode boost --port COM9 --channel 1 --samples 40
```

`idle` defaults to a `75 mA` target and returns non-zero if exceeded. `boost`
allows a higher active-viewing target. Use `--input-json` for offline threshold
tests.

Smoke test side effect:

- `tools/smoke_test.ps1` POSTs a fast test configuration to `/api/config`.
- After smoke, restore the desired runtime mode, usually `low_power`, if the
  device should continue running as a low-power station.
- Full AI closure sequence is in `docs/AI_TEST_CLOSURE_RUNBOOK.md`.

The smoke test checks:

- Host ring-buffer logic.
- Host Power optimization contract in `tools/test_power_config.py`.
- Arduino CLI compile succeeds.
- Serial telemetry reports all four sensors online:
  - SHT41
  - SCD41
  - SGP41
  - BH1750
- `/api/status` returns valid JSON.
- API reports all four sensors online.
- I2C clock is `400000` Hz.
- LittleFS is mounted.
- Data interpretation and config blocks are present.
- `/api/config` can persist a fast test configuration.
- `/api/history` collects at least one row.
- `/api/history?range=60` returns minute aggregate history.
- `/api/health` reports OK.
- `/api/health` reports the minute ring ready.
- `/api/log.csv` downloads a minute aggregate CSV header and data row.
- `/api/status.time` exposes NTP/RTC state through `sync_time`, `time_source`,
  `epoch_s`, and `local_time`.
- Root HTML uses a browser history cache for minute ranges and exposes 数据清零
  through `POST /api/log/clear?confirm=1`.
- Root HTML exposes 网页加速 through `POST /api/performance/boost`, and status
  exposes `web_boost_active`.
- BOOT key firmware exposes short/long press handling and an LCD settings menu
  for core controls.
- CO2, SHT41 temperature/humidity, and BH1750 lux values are in plausible ranges.
- Root HTML page is served and includes the trend chart and interpretation panel.
- Test reports should record the command, endpoint or script used, the observed
  result, and the key metric that proves the check, rather than only saying
  that the page looked normal.

The Power optimization contract currently verifies that future firmware exposes
`power_mode`, `lcd_brightness_pct`, `backlight_timeout_ms`, `wifi_sta_sleep`,
and `low_power_sensor_interval_ms` through `/api/config`, `/api/status`,
`/api/health`, and the root HTML. It also checks defaults, validation ranges,
WiFi STA sleep handling, LCD brightness control, and backlight timeout hooks.

Latest CH1 power readings:

- Balanced: `128.0 mA` average, `105-207 mA`, AP on, CPU 160 MHz.
- Low power: `66.1 mA` average, `41-218 mA`, STA only, CPU 80 MHz,
  backlight off, 30-second low-power sensor interval.

Additional long-run checks used in the latest validation:

- After crossing a minute boundary, `/api/status` showed `ring_count=1` and
  `ring_total_writes=1`.
- After another minute, `ring_count=2`, `ring_total_writes=2`, `health_ok=true`,
  `failures=0`, and `wifi_reconnects=0`.
- Segmented ring write time was measured at `30-60 ms`; the previous single
  large fixed file random-write approach measured around `8645 ms` and was
  removed.

Do not upload or call the integration done until this test passes.
