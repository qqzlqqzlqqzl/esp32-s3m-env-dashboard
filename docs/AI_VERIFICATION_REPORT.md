# AI Verification Report

This records the latest meaningful verification evidence. Future agents should
append new evidence instead of replacing facts unless they have re-run the same
checks.

## Last Verified Firmware Build

Command pattern:

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi" --build-path "C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard\.arduino-build" --build-property 'build.extra_flags=-DWIFI_STA_SSID="<ssid>" -DWIFI_STA_PASS="<redacted>"' "C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard"
```

Observed result after low-power implementation:

- Compile passed.
- Sketch size around `911005 bytes`, about `69%` of program storage.
- Global variables around `78252 bytes`, about `23%` of dynamic memory.

Do not copy the real WiFi password into committed docs. Use environment variable
examples in docs.

## Last Upload

Command:

```powershell
arduino-cli upload -p COM20 --fqbn "esp32:esp32:esp32s3:PSRAM=opi" --input-dir "C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard\.arduino-build" "C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard"
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
