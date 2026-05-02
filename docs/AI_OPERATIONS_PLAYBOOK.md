# AI Operations Playbook

This is a command-oriented playbook for future AI agents.

## Paths

```powershell
$Project = "C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard"
$OldProject = "C:\Users\lyl\Desktop\ESP32\esp32_sensor_hub"
$HubRepo = "C:\Users\lyl\Desktop\ESP32Mini\smartusbhub"
```

## Preflight

```powershell
git -C $Project status -sb
git -C $HubRepo status --short
```

Expected:

- Current project may have intentional changes.
- Mini SmartUSBHub copy should remain clean unless updating that tool.
- Check `$OldProject` only for explicit old-board work or isolation audits.

## Host Tests

```powershell
cd $Project
python .\tools\test_ring_log.py
python .\tools\test_power_config.py
python -m py_compile .\tools\measure_ch1_current.py
```

## Build

For local testing with real credentials, do not commit credentials:

```powershell
$env:WIFI_STA_SSID="<ssid>"
$env:WIFI_STA_PASS="<redacted>"
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi" --build-path "$Project\.arduino-build" --build-property 'build.extra_flags=-DWIFI_STA_SSID="<ssid>" -DWIFI_STA_PASS="<redacted>"' $Project
```

Use the actual password only in the shell, never in committed docs.

## Upload

```powershell
arduino-cli upload -p COM20 --fqbn "esp32:esp32:esp32s3:PSRAM=opi" --input-dir "$Project\.arduino-build" $Project
```

Do not upload to `COM9`; that is SmartUSBHub control.

## Smoke Test

```powershell
cd $Project
$env:WIFI_STA_SSID="<ssid>"
$env:WIFI_STA_PASS="<redacted>"
.\tools\smoke_test.ps1 -Port COM20 -Ip 192.168.124.67 -SerialSeconds 20
```

## Query Current Device State

```powershell
curl.exe --noproxy "*" --silent --show-error --fail --max-time 8 http://192.168.124.67/api/status | ConvertFrom-Json
curl.exe --noproxy "*" --silent --show-error --fail --max-time 8 http://192.168.124.67/api/health | ConvertFrom-Json
```

## Set Low-Power Mode

```powershell
$url = "http://192.168.124.67/api/config?power_mode=low_power&lcd_brightness_pct=15&backlight_timeout_ms=5000&wifi_sta_sleep=1&low_power_sensor_interval_ms=30000&fast_interval_ms=1000&scd_interval_ms=5000&log_interval_ms=5000&live_refresh_ms=5000&co2_warn_ppm=1000&co2_bad_ppm=1500&voc_warn=150&voc_bad=250&nox_warn=10&nox_bad=50&temp_low_c=18&temp_high_c=28&humidity_low=40&humidity_high=70&lux_low=50&lux_high=1500&sht_precision=0&bh1750_mode=0"
curl.exe --noproxy "*" --silent --show-error --fail --max-time 8 -X POST $url
```

Expected `/api/status.power`:

- `power_mode=low_power`
- `ap_enabled=false`
- `cpu_mhz=80`
- `backlight_timeout_ms=5000`
- `sensor_interval_ms=30000`

## Set Balanced Mode

```powershell
$url = "http://192.168.124.67/api/config?power_mode=balanced&lcd_brightness_pct=70&backlight_timeout_ms=60000&wifi_sta_sleep=1&low_power_sensor_interval_ms=30000&fast_interval_ms=1000&scd_interval_ms=5000&log_interval_ms=1000&live_refresh_ms=1000&co2_warn_ppm=1000&co2_bad_ppm=1500&voc_warn=150&voc_bad=250&nox_warn=10&nox_bad=50&temp_low_c=18&temp_high_c=28&humidity_low=40&humidity_high=70&lux_low=50&lux_high=1500&sht_precision=0&bh1750_mode=0"
curl.exe --noproxy "*" --silent --show-error --fail --max-time 8 -X POST $url
```

## Measure CH1 Current

```powershell
cd $Project
python .\tools\measure_ch1_current.py --port COM9 --channel 1 --samples 80
```

If `COM9` is busy:

```powershell
Get-Process python -ErrorAction SilentlyContinue | Stop-Process -Force
```

Then rerun the measurement.

## Commit And Push

```powershell
cd $Project
git status -sb
git add <specific-files>
git commit -m "Update AI handoff documentation"
git push
```

Before pushing, ensure:

```powershell
rg -n "known-real-password|known-real-ssid" .
```

There should be no real password in committed docs.
