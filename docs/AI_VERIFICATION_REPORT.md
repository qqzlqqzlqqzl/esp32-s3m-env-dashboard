# AI Verification Report

This records the latest meaningful verification evidence. Future agents should
append new evidence instead of replacing facts unless they have re-run the same
checks.

## Last Verified Firmware Build

Command pattern:

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi" --build-path "$env:TEMP\esp32mini-arduino-build" --build-property 'build.extra_flags=-DWIFI_STA_SSID="<ssid>" -DWIFI_STA_PASS="<redacted>"' "C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard"
```

Observed result after low-power implementation:

- Compile passed.
- Sketch size around `942357 bytes`, about `71%` of program storage.
- Global variables around `78404 bytes`, about `23%` of dynamic memory.

Do not copy the real WiFi password into committed docs. Use environment variable
examples in docs.

## Last Upload

Command:

```powershell
arduino-cli upload -p COM20 --fqbn "esp32:esp32:esp32s3:PSRAM=opi" --input-dir "$env:TEMP\esp32mini-arduino-build" "C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard"
```

Observed result:

- Upload passed.
- Port: `COM20`
- Chip: ESP32-S3 revision v0.2
- Hard reset after upload.

## Smoke Test

Command:

```powershell
$env:WIFI_STA_SSID="<ssid>"
$env:WIFI_STA_PASS="<redacted>"
.\tools\smoke_test.ps1 -Port COM20 -Ip 192.168.124.67 -SerialSeconds 20
```

Observed result:

- `tools/test_ring_log.py`: passed, 5 tests.
- `tools/test_power_config.py`: passed, 6 tests.
- Arduino CLI compile inside smoke test: passed.
- Serial telemetry: all four sensors online.
- HTTP API checks: passed.
- `/api/history?range=60`: minute history present.
- `/api/log.csv`: minute aggregate CSV present.
- Root HTML checks: passed.

Last observed valid serial line during smoke:

```text
[ENV] sht=on 26.50C 73.21% scd=on co2=578 sgp=on voc=0 nox=0 raw=30321/0 bh=on 14.17lx fs=on log=22 fail=0 ip=192.168.124.67
```

## Current Low-Power API Health

Observed `/api/health` after restoring low-power mode:

- `ok=true`
- `sensors_ok=true`
- `storage_ok=true`
- `ring_ready=true`
- `wifi_reconnects=0`
- `loop_stalls=0`
- `power_ok=true`
- `power_mode=low_power`
- `backlight_on=false`
- `wifi_sta_sleep=true`
- `ap_enabled=false`
- `cpu_mhz=80`
- `sleep_eligible=true`

Observed `/api/status.power`:

- `power_mode=low_power`
- `lcd_brightness_pct=15`
- `backlight_on=false`
- `backlight_timeout_ms=5000`
- `wifi_sta_sleep=true`
- `sensor_interval_ms=30000`
- `low_power_sensor_interval_ms=30000`
- `ap_enabled=false`
- `cpu_mhz=80`
- `sleep_eligible=true`

## Power Measurements

Measurement tool:

```powershell
python .\tools\measure_ch1_current.py --port COM9 --channel 1 --samples 80
```

Balanced measurement:

- Config: AP on, CPU 160 MHz, 1-second sensor loop, backlight timeout behavior.
- Result: `128.0 mA` average, `105-207 mA`, 60 samples.

Low-power measurement:

- Config: STA only, AP off, CPU 80 MHz, backlight off, 30-second low-power
  sensor interval.
- Result: `66.1 mA` average, `41-218 mA`, 80 samples.

## Host Tests

Commands:

```powershell
python .\tools\test_ring_log.py
python .\tools\test_power_config.py
python -m py_compile .\tools\measure_ch1_current.py
```

Observed result:

- Ring tests passed.
- Power contract tests passed.
- Current measurement script compiled.

## GitHub Push

Initial repository push:

- Repo: `https://github.com/qqzlqqzlqqzl/esp32-s3m-env-dashboard`
- Commit: `9d287a2 Add ESP32-S3M environment dashboard power optimization`

Docs update should be pushed as a follow-up commit.

## 2026-05-03 Workspace Separation Check

Purpose:

- Move Mini project knowledge into `C:\Users\lyl\Desktop\ESP32Mini` while
  keeping `C:\Users\lyl\Desktop\ESP32` independently usable.
- Copy common tools instead of making one workspace depend on the other.

Observed evidence:

- Copied Arduino CLI archive to `C:\Users\lyl\Desktop\ESP32Mini\tools`; SHA256
  matched the source archive in `C:\Users\lyl\Desktop\ESP32\tools`.
- Copied `smartusbhub` back to `C:\Users\lyl\Desktop\ESP32\smartusbhub`; Git
  status was clean and remote remained
  `https://github.com/qqzlqqzlqqzl/smartusbhub.git`.
- Mini SmartUSBHub import resolved to
  `C:\Users\lyl\Desktop\ESP32Mini\smartusbhub\smartusbhub.py`.
- Old workspace SmartUSBHub import resolved to
  `C:\Users\lyl\Desktop\ESP32\smartusbhub\smartusbhub.py`.
- Mini host tests passed:
  - `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
  - `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`
  - `python -m py_compile .\tools\measure_ch1_current.py` -> exit 0
- Mini Arduino CLI compile passed:
  - Sketch: `910989 bytes (69%)`
  - Globals: `78252 bytes (23%)`
- Old `esp32_sensor_hub` Arduino CLI compile passed:
  - Sketch: `1299221 bytes (99%)`
  - Globals: `186176 bytes (56%)`
- Mini CH1 current script ran through SmartUSBHub:
  - `avg_mA=102.2`
  - `min_mA=40.0`
  - `max_mA=121.0`
  - `samples=10`
  - `avg_voltage_mV=5118`

Residual note:

- `C:\Users\lyl\Desktop\ESP32\esp32_sensor_hub\.arduino-build-check` is an
  untracked build cache created for compile verification. It is not source code.

## 2026-05-03 Post-Move Upload And Readback Check

Purpose:

- Prove the moved Mini project can still compile, upload to the ESP32-S3M
  minimal system board, and read the connected environment sensors.

Build and upload:

- Project root: `C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard`
- Upload port: `COM20`
- SmartUSBHub control port avoided: `COM9`
- Arduino CLI compile passed:
  - Sketch: `911005 bytes (69%)`
  - Globals: `78252 bytes (23%)`
- Arduino CLI upload passed:
  - Chip: ESP32-S3 rev v0.2
  - MAC: `1c:db:d4:99:2b:c4`
  - Hash verified for bootloader, partition table, boot app, and application.
  - Hard reset completed.

Serial readback:

```text
[ENV] sht=on 25.28C 70.51% scd=on co2=655 sgp=on voc=0 nox=0 raw=30625/0 bh=on 7.50lx fs=on log=114 fail=0 ip=192.168.124.67
```

HTTP/API readback used raw TCP GET requests to avoid host HTTP proxy/client
interference seen with normal `curl` in this environment.

Observed `/api/status`:

- HTTP: `200 OK`
- `ok=true`
- I2C clock: `400000`
- Sensors online:
  - SHT41: `true`
  - SCD41: `true`
  - SGP41: `true`
  - BH1750: `true`
- Readings:
  - SHT41 temperature: `25.66 C`
  - SHT41 humidity: `74.5 %RH`
  - SCD41 CO2: `677 ppm`
  - SGP41 VOC index: `0`
  - SGP41 NOx index: `0`
  - BH1750 lux: `7.5 lx`
- Storage:
  - mounted: `true`
  - ring ready: `true`
  - ring count: `128`
  - failures: `0`

Observed `/api/health`:

- HTTP: `200 OK`
- `ok=true`
- `sensors_ok=true`
- `storage_ok=true`
- `power_ok=true`
- `power_mode=low_power`
- `cpu_mhz=80`
- `minute_rows=129`
- `wifi_reconnects=0`

Observed history, CSV, and HTML:

- `/api/history?range=60`: HTTP `200 OK`, source `minute`, rows `60`
- `/api/log.csv`: HTTP `200 OK`, CSV header present, rows `128`
- `/`: HTTP `200 OK`, HTML bytes `21474`; required Chinese dashboard markers
  present:
  - `ESP32-S3M 环境监测站`
  - `趋势曲线`
  - `数据解读`
  - `阈值依据`

Notes:

- Normal `curl` and `Invoke-WebRequest` showed intermittent timeout or proxy
  behavior, while raw TCP requests to the board returned valid HTTP responses.
- Health reported accumulated `loop_stalls`; because `health.ok=true` and
  storage/sensor checks passed, this was recorded as evidence to watch during
  longer soak rather than treated as a move-regression.

## 2026-05-03 Dashboard History Performance And Local Menu Check

Purpose:

- Fix slow HTML range switching after hundreds or thousands of minute records.
- Add protected data clear, NTP time status, web interaction boost, and BOOT
  single-button LCD settings controls.

Root cause found:

- The HTML did not already have all minute data locally. Every range click
  fetched `/api/history?range=<range>` from the ESP32.
- The ESP32 backend read one minute record by opening a LittleFS file per row.
  With hundreds of rows this became slow enough that `range=all` and CSV could
  hit host-side timeouts.

Implemented:

- HTML preloads `/api/history?range=all` into a browser `minuteCache`.
- Range buttons slice the browser cache locally and chart drawing decimates to
  `MAX_CHART_POINTS=360`.
- Backend history/CSV streaming now uses `readMinuteRingSlotCached`, keeping the
  current segment file open instead of reopening LittleFS per row.
- Added `POST /api/performance/boost` and a HTML `加速查看` button.
- Web requests and boot start a temporary boost window. CPU remains low-power
  capable, but WiFi sleep is disabled while `web_boost_active=true`.
- Added `POST /api/log/clear?confirm=1` for protected 数据清零. A POST without
  `confirm=1` rejects and does not delete data.
- Added NTP/RTC status in `/api/status.time`.
- Added BOOT short/long press LCD menu contract for power mode, LCD brightness,
  backlight timeout, WiFi sleep, SHT41 precision, BH1750 mode, and web boost.

Host tests:

- `python .\tools\test_dashboard_contract.py` -> `[PASS] 6 dashboard contract tests`
- `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
- `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`

Build/upload:

- Arduino CLI compile passed:
  - Sketch: `943221 bytes (71%)`
  - Globals: `78404 bytes (23%)`
- Arduino CLI upload to `COM20` passed and hash verified.

Hardware/API verification after upload:

- HTTP port 80 reachable after boot boost.
- `/api/status`:
  - `ok=true`
  - I2C: `400000`
  - SHT41/SCD41/SGP41/BH1750: all online
  - CO2: `427 ppm`
  - SHT41: `22.21 C`, `64.2 %RH`
  - BH1750: `32.5 lx`
  - NTP/RTC: `sync_time=true`, `time_source=ntp_rtc`,
    `local_time=2026-05-03 10:35:31`
- `/api/history?range=all`:
  - Rows: `707`
  - Total rows: `707`
  - Time: `4532 ms`
- `/api/log.csv`:
  - Rows: `707`
  - Time: `2760 ms`
- `/`:
  - HTML bytes: `26941`
  - Time: `332 ms`
  - Markers present: `数据清零`, `加速查看`, `minuteCache`, `ensureMinuteCache`
- `/api/performance/boost?duration_ms=60000`:
  - `boosted=true`
  - `web_boost_active=true`
  - `wifi_sta_sleep_effective=false`
  - Time: `47 ms`
- `POST /api/log/clear` without confirmation:
  - HTTP `400 Bad Request`
  - Data not cleared.

Residual note:

- BOOT long/short press menu is covered by host source contract and compile.
  Physical button navigation still needs manual observation or a future GPIO
  injection jig to verify every LCD menu transition end-to-end.

## 2026-05-03 Hourly QA Patrol (15:32 +08:00)

Host-only tests:

- `python .\tools\test_dashboard_contract.py` -> `[PASS] 6 dashboard contract tests`
- `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
- `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`
- `python -m py_compile .\tools\measure_ch1_current.py` -> exit 0

Hardware serial snapshot:

```text
[ENV] sht=on 18.99C 55.34% scd=on co2=406 sgp=on voc=98 nox=1 raw=31543/15839 bh=on 0.83lx fs=on log=966 fail=0 ip=192.168.124.67
```

HTTP perf sampling (curl.exe with `--noproxy "*"`):

- `/api/status`: initially hit an 8s timeout once, then stabilized at ~`257-350 ms`.
- `/api/health`: stabilized at ~`71-132 ms`.
- `/api/history?range=all`: `~16676 ms` and `~18357 ms` for `~131880 bytes` (slow).
- `/api/log.csv`: `~10421 ms` for `~44640 bytes` (slow).
- `/`: `~1246 ms` for `~26940 bytes`; required markers present (`数据清零`, `加速查看`, `minuteCache`, etc).
- `POST /api/log/clear` without confirmation: returned `confirm=1 required` and did not clear data.

Power sampling (SmartUSBHub `COM9`, CH1, 40 samples @ 0.25s):

- `CH1 avg_mA=123.1 min_mA=105.0 max_mA=230.0 avg_voltage_mV=5110`
- `/api/status.power`: `power_mode=low_power`, `cpu_mhz=80`, `backlight_on=false`, `web_boost_active=true`.

Findings / next:

- History/CSV transfer is substantially slower than the earlier 2026-05-03 evidence (`4532 ms` history-all, `2760 ms` CSV at 707 rows). This now reproduces with ~968 minute rows and boost active; likely needs deeper streaming/buffering optimization beyond `readMinuteRingSlotCached`.
- Added a read-only hardware perf script `tools/http_perf_check.ps1` (no `/api/config`, no data clear) to make future patrols produce PASS/FAIL evidence and timings.
- Arduino CLI compile could not be closed in this sandboxed run: `arduino-cli` fails to enumerate hardware platforms from a workspace-local data directory with `Error loading hardware platform: following symlink ...\\packages: Access is denied.` Run compile/upload closure from a non-sandboxed shell until this environment limitation is resolved.

## 2026-05-03 Hourly QA Patrol (16:01 +08:00)

Host-only tests:

- `python .\tools\test_dashboard_contract.py` -> `[PASS] 6 dashboard contract tests`
- `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
- `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`
- `python -m py_compile .\tools\measure_ch1_current.py` -> exit 0

Hardware perf check (read-only, no config writes, no data clear):

- Serial: `[ENV] ... log=1037 ... ip=192.168.124.67`
- `.\tools\http_perf_check.ps1 -Port COM20 -SerialSeconds 20 -WithBoost`:
  - `POST /api/performance/boost`: `9405 ms` (slow)
  - `GET /api/status`: `1305 ms`
  - `GET /api/health`: `74 ms`
  - `GET /api/history?range=60`: `1158 ms`
  - `GET /api/history?range=all`: `18809 ms` for `~141 KB` (slow)
  - `GET /api/log.csv`: `11539 ms` for `~48 KB` (slow)
  - `GET /`: `1466 ms` for `~29 KB` (markers present)
  - `POST /api/log/clear` without confirm: `HTTP 400` (protected clear OK)

Power sampling (SmartUSBHub `COM9`, CH1, 40 samples @ 0.25s):

- `CH1 avg_mA=136.1 min_mA=102.0 max_mA=268.0 samples=40 avg_voltage_mV=5104`

Changes made (needs firmware rebuild+upload to validate on-device):

- Updated `readMinuteRingSlotCached` to avoid redundant `seek()` calls when streaming sequential minute rows (uses `file.position()` fast-path).
- Hardened `tools/http_perf_check.ps1` to always print the endpoint timing table and list failures before exiting non-zero.

Known blocker:

- Arduino CLI compile/upload closure still blocked in this sandbox: `arduino-cli` fails with `Error loading hardware platform: following symlink ...\\packages: Access is denied.` (even for workspace-local Arduino data dirs).

## 2026-05-03 Hourly QA Patrol (17:02 +08:00)

Host-only tests:

- `python .\tools\test_dashboard_contract.py` -> `[PASS] 7 dashboard contract tests`
- `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
- `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`
- `python -m py_compile .\tools\measure_ch1_current.py` -> exit 0

Arduino CLI compile:

- Default Arduino data dir attempt failed before compile with `Access is denied`
  under `C:\Users\lyl\AppData\Local\Arduino15\packages/tmp`.
- Workspace-local config attempt also did not reach compile; a later retry timed
  out after 90 seconds while refreshing Arduino package indexes.
- No firmware was uploaded in this patrol. The performance/NTP firmware changes
  remain host-tested but not compile/upload verified.

Hardware read-only HTTP patrol:

- Serial from `COM20`: `[ENV] sht=on 24.15C 81.12% scd=on co2=451 sgp=on voc=115 nox=1 raw=31245/17346 bh=on 12.50lx fs=on log=1086 fail=0 ip=192.168.124.67`
- `.\tools\http_perf_check.ps1 -Port COM20 -SerialSeconds 20 -WithBoost`:
  - `POST /api/performance/boost`: `87 ms`
  - `GET /api/status`: `628 ms`
  - `GET /api/health`: `71 ms`
  - `GET /api/history?range=60`: `1343 ms`
  - `GET /api/history?range=all`: `19413 ms` for `1087` rows / `148318` bytes (slow)
  - `GET /api/log.csv`: `12607 ms` for `50498` bytes (slow)
  - `GET /`: `1113 ms` for `26940` bytes; required HTML markers present
  - `POST /api/log/clear` without confirm: `HTTP 400`, protected clear OK

Power and time observations:

- Immediate post-HTTP/boost CH1 current: `avg_mA=140.1 min_mA=111.0 max_mA=271.0 samples=40 avg_voltage_mV=5097`
- After waiting 70 seconds with no HTTP request: `avg_mA=115.0 min_mA=105.0 max_mA=205.0 samples=40 avg_voltage_mV=5109`
- `/api/status.power` still reported low-power config: `power_mode=low_power`, `lcd_brightness_pct=15`, `backlight_on=false`, `wifi_sta_sleep=true`, `ap_enabled=false`, `cpu_mhz=80`.
- `/api/status.time` reported `sync_time=false`, `time_source=uptime`, `epoch_s=0`, `local_time=unsynced`. NTP is a current hardware/runtime finding.

Changes made (needs compile/upload verification):

- Buffered minute history and CSV streaming with `appendBufferedContent`,
  `minuteRecordJsonLine`, and `minuteRecordCsvLine` to reduce per-row
  `server.sendContent` overhead.
- `readMinuteRingSlotCached` now skips redundant `seek()` for sequential reads.
- `tools/http_perf_check.ps1` now has `CurlMaxSeconds=30`, so slow endpoints
  are recorded as warnings and the script still checks CSV, root HTML, and
  clear protection.
- Added `ntp.aliyun.com` as `kNtpServer3` and pass three NTP servers to
  `configTzTime`; dashboard contract now checks this.

Residual risks / next:

- Compile/upload must be completed from a non-sandboxed shell or after fixing
  Arduino CLI data-dir access, then rerun the HTTP patrol to prove the buffered
  streaming fix on hardware.
- NTP should be rechecked after flashing the three-server build; if it still
  reports `uptime`, investigate UDP/NTP reachability on the STA network.
- Low-power current is still above the documented `65-75 mA` baseline in this
  patrol. Re-measure after a clean flash and a longer idle window with no HTTP
  requests.

## 2026-05-03 Manual Closure After Patrol Fixes (17:12 +08:00)

Root cause fixed:

- Arduino CLI was hanging because build/cache directories such as
  `.arduino-build-*`, `.arduino-data`, and `arduino_data*` were left inside the
  sketch root. Arduino CLI copied those directories into `build/sketch`, causing
  recursive/heavy scanning. Build paths for this project must live outside the
  sketch, for example under `$env:TEMP`.

Code and test changes:

- Minute history JSON and CSV now stream through a 2 KB response buffer via
  `appendBufferedContent` / `flushBufferedContent` instead of calling
  `server.sendContent()` once per row.
- `minuteRecordJsonLine` and `minuteRecordCsvLine` format rows into stack
  buffers, reducing temporary `String` churn during large history exports.
- `tools/http_perf_check.ps1` is the read-only patrol script: no `/api/config`,
  no confirmed data clear, and `POST /api/log/clear` without `confirm=1` must
  return HTTP 400.
- `tools/test_dashboard_contract.py` now checks buffered streaming and the
  read-only patrol script contract.

Verification:

- `python .\tools\test_dashboard_contract.py` -> `[PASS] 7 dashboard contract tests`
- `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
- `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`
- PowerShell parse check for `tools/http_perf_check.ps1` -> pass
- Arduino CLI compile from `$env:TEMP\esp32mini-arduino-build-verify` -> pass:
  sketch `942357` bytes (`71%`), globals `78404` bytes (`23%`)
- Upload to `COM20` -> pass, all flash writes hash verified

Hardware read-only patrol after flashing:

- Serial: `[ENV] sht=on 24.25C 81.70% scd=on co2=449 sgp=on voc=0 nox=0 raw=31188/0 bh=on 12.50lx fs=on log=1088 fail=0 ip=192.168.124.67`
- `.\tools\http_perf_check.ps1 -Port COM20 -SerialSeconds 18 -WithBoost`:
  - `/api/status`: `350 ms`
  - `/api/health`: `86 ms`
  - `POST /api/performance/boost`: `49 ms`
  - `/api/history?range=60`: `268 ms`
  - `/api/history?range=all`: `1862 ms` for `1089` rows / `148587` bytes
  - `/api/log.csv`: `1402 ms` for `50587` bytes
  - `/`: `143 ms`
  - `POST /api/log/clear` without confirm: `72 ms`, protected HTTP 400
- NTP after flashing: `sync_time=true`, `time_source=ntp_rtc`,
  `local_time=2026-05-03 17:12:08`.

Browser UX audit through Edge DevTools Protocol:

- Dashboard preload: `分钟历史已预加载 1093 点`.
- `/api/history?range=all` resource count stayed `1 -> 1` after clicking range
  buttons, proving range switching used browser cache.
- Range click timings: 1h `17 ms`, 6h `28 ms`, 1d `33 ms`, 3d `34 ms`, all
  `33 ms`, realtime `34 ms`.
- `chartCo2`, `chartVoc`, `chartNox`, and `chartLux` canvases were nonblank.

Power note:

- With active web viewing/boost, CH1 measured around `116.5 mA`.
- After closing the test browser and waiting beyond the boost window, CH1 still
  measured around `124.1 mA` in this run. Status before/after showed low-power
  config (`STA`, AP off, 80 MHz, backlight off), so this remains an open power
  investigation rather than a closed regression. Track under the SmartUSBHub
  power regression issue.

## 2026-05-03 CSV Export Dirty Log Fix (17:34 +08:00)

User-provided downloaded log:

- File: `C:\Users\lyl\Downloads\log (1).csv`
- `python .\tools\analyze_csv_log.py "C:\Users\lyl\Downloads\log (1).csv" --require-bom`:
  - `rows=1053`
  - `utf8_bom=False`
  - `zero_lux_rows=387`
  - `zero_core_rows=5`
  - `minute_gaps=12`
  - Result: FAIL. This reproduces the Excel/dirty-data complaint with objective evidence.

Root causes:

- `/api/log.csv` did not prepend a UTF-8 BOM, so Excel could guess the wrong
  encoding for CSV content.
- `SampleRow.ok` only checked sensor online flags; startup rows with no valid
  SCD41/SGP41/BH1750 data could still be aggregated and persisted.
- Uptime-minute records and later NTP epoch-minute records could coexist in the
  ring when time sync happened after logging started.

Fixes:

- `/api/log.csv` now starts with UTF-8 BOM bytes `EF BB BF`.
- `makeSample()` rejects rows until each sensor has produced at least one valid
  reading, SGP41 conditioning is complete, CO2 is non-zero, SRAW VOC/NOx are
  non-zero, and numeric ranges are plausible.
- When STA credentials are compiled in, persistent logging waits until NTP/RTC
  time is synced instead of writing uptime-minute rows.
- If an aggregate ever switches between uptime and epoch minute domains, the
  in-progress aggregate is dropped rather than persisted.
- `/api/status.storage` exposes `dropped_invalid_samples`,
  `dropped_unsynced_samples`, and `dropped_time_domain_samples`.
- `tools/analyze_csv_log.py` checks exported logs for BOM, zero core rows, and
  minute continuity.

Verification:

- `python .\tools\test_dashboard_contract.py` -> `[PASS] 8 dashboard contract tests`
- `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
- `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`
- `python -m py_compile .\tools\analyze_csv_log.py` -> exit 0
- Arduino CLI compile from `$env:TEMP\esp32mini-arduino-build-logfix` -> pass:
  sketch `943017` bytes (`71%`), globals `78420` bytes (`23%`)
- Upload to `COM20` -> pass, all flash writes hash verified
- `.\tools\http_perf_check.ps1 -Port COM20 -SerialSeconds 18 -WithBoost` -> PASS:
  `/api/history?range=all` `2104 ms` for `1124` rows, `/api/log.csv` `1601 ms`,
  `/` `132 ms`, protected clear `HTTP 400`.
- New `/api/log.csv` first bytes: `EF BB BF 6D 69 6E...`, proving Excel BOM is present.
- `/api/status.storage`: `dropped_invalid_samples=6`,
  `dropped_unsynced_samples=1`, `dropped_time_domain_samples=0`.
- `/api/status.time`: `sync_time=true`, `time_source=ntp_rtc`,
  `local_time=2026-05-03 17:34:15`.

Important residual:

- Existing dirty records remain in LittleFS until the user explicitly approves
  `POST /api/log/clear?confirm=1`. The fix prevents new dirty samples from being
  added; it does not rewrite old Flash history in place.

## 2026-05-03 Read-Side Dirty Log Filtering (18:03 +08:00)

Problem:

- The previous fix stopped new dirty samples but default `/api/log.csv` still
  exported old dirty LittleFS rows unless the user explicitly cleared history.
- Clearing is destructive, so the default read path needed to filter old dirty
  rows without deleting Flash data.

Fix:

- Added `minuteRecordHasUsableValues`, `computeMinuteExportStats`, and
  `minuteRecordExportable`.
- `/api/history?range=...` and `/api/log.csv` now skip old persisted rows with
  zero core values, zero lux, invalid numeric ranges, or stale uptime-minute
  keys when NTP epoch-minute rows exist.
- `/api/status.storage.last_filtered_minute_rows` exposes how many raw rows were
  hidden by the read-side filter in the latest history/CSV export.
- `tools/analyze_csv_log.py` now fails non-monotonic minute keys but only reports
  gaps, because gaps are expected when old dirty rows are filtered instead of
  deleted.

Verification:

- `python .\tools\test_dashboard_contract.py` -> `[PASS] 8 dashboard contract tests`
- `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
- `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`
- Arduino CLI compile from `$env:TEMP\esp32mini-arduino-build-filterfix` -> pass:
  sketch `943917` bytes (`72%`), globals `78420` bytes (`23%`)
- Upload to `COM20` -> pass, all flash writes hash verified.
- `.\tools\http_perf_check.ps1 -Port COM20 -SerialSeconds 18 -WithBoost` -> PASS:
  `/api/history?range=all` `1633 ms` for `376` filtered rows,
  `/api/log.csv` `1486 ms`, `/` `142 ms`, protected clear `HTTP 400`.
- Downloaded `_verify_filtered_log.csv` first bytes: `EF BB BF`.
- `python .\tools\analyze_csv_log.py .\_verify_filtered_log.csv --require-bom`:
  - `rows=376`
  - `utf8_bom=True`
  - `zero_lux_rows=0`
  - `zero_core_rows=0`
  - `minute_backtracks=0`
  - Result: `[PASS] CSV log analysis passed`
- `/api/status.storage`: raw ring count `1147`,
  `last_filtered_minute_rows=771`, `dropped_invalid_samples=6`,
  `dropped_unsynced_samples=1`.

## 2026-05-03 HTTP Patrol Export Quality Gate (18:15 +08:00)

Purpose:

- Turn the CSV/dirty-history bug into a recurring read-only test so future
  patrols fail before the user sees Excel mojibake, `0 lux`, zero core sensor
  rows, or non-monotonic minute history again.

Change:

- `tools/http_perf_check.ps1` now checks `/api/history?range=60`,
  `/api/history?range=all`, and `/api/log.csv` for:
  - non-empty minute rows
  - positive CO2, humidity, and lux values
  - non-zero temperature
  - strictly increasing minute keys
  - UTF-8 BOM on CSV export for Excel compatibility
- The script remains read-only except for the temporary web performance boost.
  It still verifies that `POST /api/log/clear` without `confirm=1` returns HTTP
  400 and does not clear data.

Independent verification by subagent:

- `python .\tools\test_dashboard_contract.py` -> `[PASS] 8 dashboard contract tests`
- `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
- `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`
- `python -m py_compile .\tools\analyze_csv_log.py` -> exit 0
- PowerShell parse check for `tools/http_perf_check.ps1` -> pass
- `.\tools\http_perf_check.ps1 -Port COM20 -SerialSeconds 18 -WithBoost` -> PASS:
  - Device IP: `192.168.124.67`
  - `/api/status`: `649 ms`
  - `/api/health`: `44 ms`
  - `/api/history?range=60`: `1584 ms`
  - `/api/history?range=all`: `1923 ms` for `376` rows
  - `/api/log.csv`: `1748 ms`
  - `/`: `137 ms`
  - Protected clear: `68 ms`, HTTP 400 expected

No log clear was performed.

## 2026-05-03 Reboot Persistence Check (19:22 +08:00)

Purpose:

- Close the reboot persistence gap from issue #9 without clearing logs or
  mutating config by default.

Change:

- Added `tools/reboot_persistence_check.ps1`.
- Added `tools/test_reboot_persistence_contract.py`.
- `TESTING.md` now lists the reboot persistence contract test.

What the check verifies:

- Captures before-reboot `/api/status` config/storage/network/power fields.
- Captures before-reboot `/api/history?range=all`.
- Triggers a serial reset on `COM20` with DTR/RTS.
- Waits for `/api/status` to become ready again.
- Compares persisted config keys across reboot.
- Confirms LittleFS remains mounted and minute ring remains ready.
- Confirms history rows remain readable and do not unexpectedly shrink.
- Confirms `ring_count` and `ring_total_writes` do not regress.
- Confirms reboot returns to `low_power`, STA mode, AP off, and CPU `80 MHz`.
- Runs `health_verdict.py` and `time_continuity_check.py` after reboot.
- Does not call `/api/log/clear?confirm=1`.
- Does not POST `/api/config` by default.

Independent subagent verification:

- `python .\tools\test_reboot_persistence_contract.py` ->
  `[PASS] 7 reboot persistence contract tests`
- `python -m py_compile .\tools\test_reboot_persistence_contract.py` -> pass

Mainline verification:

- `.\tools\reboot_persistence_check.ps1 -Port COM20 -Ip 192.168.124.67 -TimeoutSeconds 100`
  -> PASS:
  - config persisted across reboot
  - LittleFS mounted after reboot
  - minute ring ready after reboot
  - power mode: `low_power`
  - network mode: `STA`
  - AP: disabled
  - CPU: `80 MHz`
  - health verdict: `21` checks, `0` warnings
  - time continuity: `8` checks
  - before rows: `414`
  - after rows: `414`
  - before ring_count: `1205`
  - after ring_count: `1205`
  - before ring_total_writes: `1205`
  - after ring_total_writes: `1205`
- Host regression tests also passed:
  - `python .\tools\test_dashboard_contract.py` -> `[PASS] 10 dashboard contract tests`
  - `python .\tools\test_power_regression_check.py` -> `[PASS] 3 power regression tests`
  - `python .\tools\test_time_continuity_check.py` -> `[PASS] 5 time continuity tests`
  - `python .\tools\test_health_verdict.py` -> `[PASS] 4 health verdict tests`
  - `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
  - `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`

No log clear was performed.

## 2026-05-03 Display Wake Endpoint And Backlight Power Evidence (19:15 +08:00)

Purpose:

- Address the remaining issue #6 gap identified by the curator: power gate had
  idle and web boost evidence, but no true LCD/backlight-awake evidence.

Finding:

- Trying to use `/api/config/reset` as a wake method was not valid evidence:
  it called `setBacklight(true)` but did not refresh `gLastUserActivityMs`, so
  the normal timeout logic immediately turned the backlight off.

Change:

- Added `POST /api/display/wake`.
- The endpoint is non-persistent and does not change config or logs.
- It calls `noteUserActivity()` and returns:
  - `woke`
  - `backlight_on`
  - `lcd_brightness_pct`
  - `backlight_timeout_ms`
- Reduced `kDefaultWebBoostMs` from `180000 ms` to `60000 ms`. The old 3-minute
  default caused idle power checks to remain in web-boost mode long after the
  user stopped interacting. Explicit boost requests can still pass a duration.
- `tools/test_dashboard_contract.py` now checks the display wake route and JSON
  fields.
- `TESTING.md` documents the display wake + backlight power gate command.

Build/upload:

- Arduino CLI compile passed from `$env:TEMP\esp32mini-arduino-build-displaywake`:
  - Sketch: `944389 bytes`, `72%`
  - Globals: `78420 bytes`, `23%`
- Upload to `COM20` passed; all flash writes were hash verified.

Verification:

- `python .\tools\test_dashboard_contract.py` -> `[PASS] 10 dashboard contract tests`
- `python .\tools\test_power_regression_check.py` -> `[PASS] 3 power regression tests`
- `python .\tools\test_time_continuity_check.py` -> `[PASS] 5 time continuity tests`
- `python .\tools\test_health_verdict.py` -> `[PASS] 4 health verdict tests`
- `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
- `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`

Backlight awake power evidence:

- Device ready after upload:
  - `time_source=ntp_rtc`
  - `power=low_power`
  - `backlight=false`
- `POST /api/display/wake`:
  - `woke=True`
  - `backlight=True`
  - `brightness=15`
  - `timeout=5000`
- `python .\tools\power_regression_check.py --mode backlight --port COM9 --channel 1 --samples 40 --interval 0.25`
  -> PASS:
  - `avg_mA=53.0`
  - `min_mA=41.0`
  - `max_mA=188.0`
  - `avg_voltage_mV=5142`
  - target `180 mA`
- After timeout:
  - `backlight=False`

Idle power evidence after shorter boost expiry:

- After waiting past the new 60-second default web boost window:
  `python .\tools\power_regression_check.py --mode idle --port COM9 --channel 1 --samples 40 --interval 0.25`
  -> PASS:
  - `avg_mA=60.5`
  - `min_mA=41.0`
  - `max_mA=195.0`
  - `avg_voltage_mV=5140`
  - target `75 mA`

Hourly patrol after upload:

- `.\tools\hourly_qa_patrol.ps1 -Ip 192.168.124.67 -WithBoost -SkipPower`
  -> PASS:
  - health verdict: `21` checks, `0` warnings
  - time continuity: `8` checks
  - `/api/history?range=all`: `2796 ms` for `414` rows
  - `/api/log.csv`: `2610 ms`
  - protected clear: `48 ms`, HTTP 400 expected
  - browser UX: PASS
  - range clicks: `10.5-34.0 ms`
  - `/api/history?range=all` browser requests: `1`
  - all four chart canvases nonblank

No log clear was performed.

## 2026-05-03 Backlog Curator Protocol (18:56 +08:00)

Purpose:

- Convert issue #10 into a durable AI-readable protocol so the main agent does
  not silently close its own work without independent evidence.

Change:

- Added `docs/AI_BACKLOG_PROTOCOL.md`.
- Updated `docs/AI_DOC_INDEX.md` and `README.md` to point future agents to the
  protocol.

Protocol summary:

- Main agent may create issues, implement fixes, and comment evidence.
- Main agent must not close an issue it implemented unless an independent
  curator/verifier has reviewed the evidence.
- Curator may close issues only after comparing acceptance criteria against
  committed code, docs, tests, and hardware evidence.
- Every issue should include priority, context, acceptance criteria,
  reproduction or verification commands, and residual risk.
- Every closure comment should include commit hash, commands run, hardware
  target when relevant, destructive-endpoint status, and residual risk.
- Destructive confirmed log clear, 24h/72h soak, and physical BOOT/LCD
  verification stay open until their specific safe test window exists.

Verification:

- Markdown/docs-only change reviewed by diff.
- Protocol was immediately used in this session: issues #1, #2, and #3 were
  closed by curator subagents rather than by the main implementing agent.

## 2026-05-03 NTP And Minute Continuity Gate (18:53 +08:00)

Purpose:

- Convert issue #7 from a manual observation into a read-only gate for NTP/RTC
  synchronization and monotonic minute history.

Change:

- Added `tools/time_continuity_check.py`.
- Added `tools/test_time_continuity_check.py` with offline fixtures for:
  - synced `ntp_rtc` PASS
  - uptime/unsynced FAIL
  - repeated history minute FAIL
  - CSV minute rollback FAIL
- `tools/hourly_qa_patrol.ps1` now runs the time continuity check after
  `health_verdict.py` and before the HTTP perf sweep.
- `TESTING.md` lists the new host test.

Verification:

- `python .\tools\test_time_continuity_check.py` -> `[PASS] 5 time continuity tests`
- `python .\tools\test_power_regression_check.py` -> `[PASS] 3 power regression tests`
- `python .\tools\test_health_verdict.py` -> `[PASS] 4 health verdict tests`
- `python .\tools\test_dashboard_contract.py` -> `[PASS] 9 dashboard contract tests`
- `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
- `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`
- `python -m py_compile .\tools\time_continuity_check.py .\tools\test_time_continuity_check.py .\tools\power_regression_check.py .\tools\health_verdict.py .\tools\analyze_csv_log.py .\tools\measure_ch1_current.py`
  -> pass

Direct hardware time check:

- `python .\tools\time_continuity_check.py --ip 192.168.124.67` -> PASS:
  - `sync_time=True`
  - `time_source=ntp_rtc`
  - `epoch_s=1777805360`
  - `epoch_minute=29630089`
  - `local_time=2026-05-03 18:49:20`
  - history rows: `400`, monotonic
  - CSV rows: `400`, monotonic
  - latest minute: `29630089`

Hourly runner verification:

- `.\tools\hourly_qa_patrol.ps1 -Ip 192.168.124.67 -WithBoost -SkipBrowser -SkipPower`
  -> PASS:
  - host tests passed, including time continuity tests
  - health verdict: `21` checks, `0` warnings
  - time continuity: `8` checks
  - `time_source=ntp_rtc`
  - `epoch_minute=29630092`
  - history rows: `403`, monotonic
  - CSV rows: `403`, monotonic
  - `/api/history?range=all`: `2785 ms` for `403` rows
  - `/api/log.csv`: `2588 ms`
  - protected clear: `48 ms`, HTTP 400 expected

No log clear was performed.

## 2026-05-03 SmartUSBHub Power Regression Gate (18:43 +08:00)

Purpose:

- Convert CH1 current sampling into a threshold-based regression gate for issue
  #6, instead of relying on manual interpretation of one-off current numbers.

Change:

- Added `tools/power_regression_check.py`.
- Added `tools/test_power_regression_check.py` with offline JSON fixtures.
- `tools/hourly_qa_patrol.ps1` now uses `power_regression_check.py --mode boost`
  for the active browser/boost patrol window.
- `TESTING.md` documents idle and boost power gate commands.

Thresholds:

- `idle`: default target `75 mA`; over target returns non-zero.
- `boost`: default target `160 mA`; intended for active web viewing and
  temporary WiFi boost windows.
- `backlight`: default target `180 mA`; reserved for LCD/backlight-awake windows.

Independent subagent verification:

- `python .\tools\test_power_regression_check.py` -> `[PASS] 3 power regression tests`
- `python -m py_compile .\tools\test_power_regression_check.py` -> pass

Mainline verification:

- `python .\tools\power_regression_check.py --mode boost --port COM9 --channel 1 --samples 30 --interval 0.25`
  -> PASS:
  - `avg_mA=140.7`
  - `min_mA=107.0`
  - `max_mA=224.0`
  - `avg_voltage_mV=5096`
  - target `160 mA`
- Immediate idle run after boost hit `COM9` access denied. This was treated as
  a port-availability condition, not a current regression.
- After waiting 70 seconds for the boost/viewing window to expire:
  `python .\tools\power_regression_check.py --mode idle --port COM9 --channel 1 --samples 40 --interval 0.25`
  -> PASS:
  - `avg_mA=65.9`
  - `min_mA=40.0`
  - `max_mA=142.0`
  - `avg_voltage_mV=5139`
  - target `75 mA`

No log clear was performed.

## 2026-05-03 Read-Only Hourly QA Patrol Runner (18:36 +08:00)

Purpose:

- Close the P0 gap where hourly QA behavior existed in automation text but not
  as a repository-owned runner that future agents can execute consistently.

Change:

- Added `tools/hourly_qa_patrol.ps1`.
- The runner is read-only for device state:
  - runs `git status` and recent git log
  - runs host contract tests
  - compiles Python tools
  - parses `http_perf_check.ps1`
  - discovers IP from serial `[ENV]` when possible
  - runs `health_verdict.py`
  - runs `http_perf_check.ps1`
  - runs `browser_ux_check.mjs` unless skipped
  - optionally samples SmartUSBHub CH1 current
- It does not call `/api/config`.
- It does not call confirmed log clear. The HTTP patrol only verifies
  unconfirmed `/api/log/clear` returns HTTP 400.
- It distinguishes serial discovery failure, device IP failure, HTTP/API
  failure, browser UX failure, and power measurement warnings.

Verification:

- PowerShell parse check for `tools/hourly_qa_patrol.ps1` -> pass
- First run without `-Ip` exposed a real serial-port availability condition:
  `COM20` access denied, no IP discovered, runner returned FAIL with explicit
  serial/IP messages.
- Full run with known IP:
  `.\tools\hourly_qa_patrol.ps1 -Ip 192.168.124.67 -WithBoost -PowerSamples 20`
  -> PASS:
  - host tests passed
  - health verdict: `21` checks, `0` warnings
  - HTTP history all: `2048 ms` for `386` rows
  - CSV: `1866 ms`
  - browser UX preload: `2789 ms`
  - browser range clicks: `5.0-33.2 ms`
  - `/api/history?range=all` browser requests: `1`
  - CH1 current sample: `125.0 mA` average during active/boosted viewing
- Boost propagation retest:
  `.\tools\hourly_qa_patrol.ps1 -Ip 192.168.124.67 -WithBoost -SkipBrowser -SkipPower`
  -> PASS:
  - `POST /api/performance/boost`: `57 ms`
  - `/api/history?range=all`: `2387 ms` for `387` rows
  - `/api/log.csv`: `2156 ms`
  - protected clear: `51 ms`
  - final metric reported `boost=True`

No log clear was performed.

## 2026-05-03 Independent Health Verdict And Patrol Hardening (18:32 +08:00)

Purpose:

- Close the P0 gap where the main agent and `http_perf_check.ps1` were the only
  health judges.
- Add a read-only script with objective exit codes for `/api/status` and
  `/api/health`.

Change:

- Added `tools/health_verdict.py`.
- Added `tools/test_health_verdict.py` with offline PASS/FAIL/WARN fixtures.
- `tools/http_perf_check.ps1` now calls `health_verdict.py` during hardware
  patrol.
- Fixed a patrol-script bug where PowerShell treated a single serial `[ENV]`
  line as a scalar string, so `$envLines[-1]` returned the final character
  instead of the line. The script now wraps serial matches in an array before
  selecting the newest telemetry line.
- `TESTING.md` now lists both the host-side health verdict tests and direct
  hardware verdict command.

Independent subagent verification:

- `python .\tools\test_health_verdict.py` -> `[PASS] 4 health verdict tests`
- `python -m py_compile .\tools\test_health_verdict.py .\tools\health_verdict.py`
  -> pass

Mainline verification:

- `python .\tools\test_health_verdict.py` -> `[PASS] 4 health verdict tests`
- `python .\tools\test_dashboard_contract.py` -> `[PASS] 9 dashboard contract tests`
- `python .\tools\test_ring_log.py` -> `[PASS] 5 ring log tests`
- `python .\tools\test_power_config.py` -> `[PASS] 6 power contract tests`
- `python -m py_compile .\tools\health_verdict.py .\tools\test_health_verdict.py .\tools\analyze_csv_log.py`
  -> pass
- `python .\tools\health_verdict.py --ip 192.168.124.67` -> PASS:
  - `21` checks
  - `0` warnings
  - all four sensors online
  - I2C `400 kHz`
  - LittleFS mounted
  - minute ring ready
  - `power_ok=true`
  - time fields present
  - storage failures `0`
  - loop stalls `0`
  - WiFi reconnects `0`
- `.\tools\http_perf_check.ps1 -Port COM20 -SerialSeconds 18 -WithBoost` -> PASS:
  - Serial line parsed correctly:
    `[ENV] sht=on ... scd=on ... sgp=on ... bh=on ... ip=192.168.124.67`
  - embedded health verdict: PASS
  - `/api/status`: `332 ms`
  - `/api/health`: `75 ms`
  - `/api/history?range=60`: `1536 ms`
  - `/api/history?range=all`: `1885 ms` for `382` rows
  - `/api/log.csv`: `1735 ms`
  - `/`: `191 ms`
  - protected clear: `64 ms`, HTTP 400 expected

No log clear was performed.

## 2026-05-03 Browser UX Range Click Patrol (18:24 +08:00)

Purpose:

- Convert the user's slow dashboard range-switching complaint into a repeatable
  browser test that actually opens Chrome, clicks the dashboard buttons, and
  checks rendered chart canvases.

Change:

- Added `tools/browser_ux_check.mjs`.
- The script uses only Node.js standard libraries and a headless Chrome DevTools
  Protocol connection. It does not require Playwright or npm packages.
- The script starts Chrome with an isolated temporary profile, no proxy, and a
  temporary remote-debugging port.
- It waits for the dashboard minute cache preload, clicks:
  - realtime
  - 1 hour
  - 6 hours
  - 1 day
  - 3 days
  - all minutes
- It fails if `/api/history?range=all` is requested more than once, if a click
  takes more than 2 seconds, or if any of `chartCo2`, `chartVoc`, `chartNox`,
  and `chartLux` are blank.
- `tools/test_dashboard_contract.py` now includes a source contract for the
  browser UX patrol script.
- `TESTING.md` documents the new command.

Verification:

- `node --check .\tools\browser_ux_check.mjs` -> pass
- `python .\tools\test_dashboard_contract.py` -> `[PASS] 9 dashboard contract tests`
- `node .\tools\browser_ux_check.mjs --url http://192.168.124.67/` -> PASS:
  - preload: `2871 ms`
  - `/api/history?range=all` requests: `1`
  - realtime click: `16.5 ms`
  - 1 hour click: `32.1 ms`
  - 6 hour click: `33.1 ms`
  - 1 day click: `30.3 ms`
  - 3 day click: `32.5 ms`
  - all minutes click: `32.3 ms`
  - `chartCo2`, `chartVoc`, `chartNox`, and `chartLux`: nonblank

Same-session hardware baseline:

- `.\tools\http_perf_check.ps1 -Port COM20 -SerialSeconds 18 -WithBoost` -> PASS:
  - `/api/status`: `690 ms`
  - `/api/health`: `109 ms`
  - `/api/history?range=60`: `1858 ms`
  - `/api/history?range=all`: `2207 ms` for `376` rows
  - `/api/log.csv`: `2009 ms`
  - `/`: `176 ms`
  - protected clear: `65 ms`, HTTP 400 expected
- `python .\tools\measure_ch1_current.py --port COM9 --channel 1 --samples 40 --interval 0.25`:
  - `avg_mA=67.0`
  - `min_mA=38.0`
  - `max_mA=135.0`

No log clear was performed.
