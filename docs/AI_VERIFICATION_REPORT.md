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
