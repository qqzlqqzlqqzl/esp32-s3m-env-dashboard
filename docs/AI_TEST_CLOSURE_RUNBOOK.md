# AI Test Closure Runbook

This runbook defines "done" for future AI changes. Do not call work complete
based on visual inspection.

## Required Closure Sequence

Run from:

```powershell
cd C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard
```

1. Host ring test:

```powershell
python .\tools\test_ring_log.py
```

Pass: `[PASS] 5 ring log tests`

2. Host power contract:

```powershell
python .\tools\test_power_config.py
```

Pass: `[PASS] 6 power contract tests`

3. Tool syntax:

```powershell
python -m py_compile .\tools\measure_ch1_current.py
```

Pass: no output and exit code 0.

4. Arduino compile:

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi" --build-path ".\.arduino-build" --build-property 'build.extra_flags=-DWIFI_STA_SSID="<ssid>" -DWIFI_STA_PASS="<redacted>"' .
```

Pass: compile exits 0. Never commit real password.

5. Upload:

```powershell
arduino-cli upload -p COM20 --fqbn "esp32:esp32:esp32s3:PSRAM=opi" --input-dir ".\.arduino-build" .
```

Pass: upload exits 0. Never upload to `COM9`.

6. Smoke test:

```powershell
$env:WIFI_STA_SSID="<ssid>"
$env:WIFI_STA_PASS="<redacted>"
.\tools\smoke_test.ps1 -Port COM20 -Ip 192.168.124.67 -SerialSeconds 20
```

Pass: `[PASS] Smoke test passed ...`

7. Manual API health:

```powershell
curl.exe --noproxy "*" --silent --show-error --fail --max-time 8 http://192.168.124.67/api/status | ConvertFrom-Json
curl.exe --noproxy "*" --silent --show-error --fail --max-time 8 http://192.168.124.67/api/health | ConvertFrom-Json
```

Pass: `ok=true`, sensors online, storage OK, power fields present.

8. Power measurement when power behavior changed:

```powershell
python .\tools\measure_ch1_current.py --port COM9 --channel 1 --samples 80
```

Pass: result is recorded in docs if materially changed.

9. Minute-boundary check when storage changed:

- Wait across at least two minute boundaries.
- Confirm `ring_count` or `ring_total_writes` increases.
- Confirm `/api/health.ok=true`.

## Smoke Test Side Effects

`tools/smoke_test.ps1` POSTs a test config. It can change persisted device
configuration. After smoke, restore desired low-power config if the device must
continue running in low-power mode.

Smoke test currently checks:

- Host ring-buffer logic.
- Host power contract.
- Arduino CLI compile.
- Serial telemetry for all four sensors.
- I2C `400000 Hz`.
- LittleFS mounted.
- Data interpretation/config blocks.
- `/api/config` POST.
- RAM history.
- Minute history.
- Health.
- Minute CSV.
- Root HTML Chinese/dashboard content.

Cross-project rule:

- Reuse the old `esp32_sensor_hub` closure pattern, not its old hardware list.
  The transferable parts are build/upload proof, serial URL discovery, health
  API checks, dashboard checks, config persistence, reboot persistence, soak
  checks, and explicit PASS evidence.
- Do not add DHT, AP3216C, QMA6100P, ES8388, OV5640, XL9555, camera, speaker,
  or old LCD requirements to this Mini project unless that hardware is actually
  connected.

## Failure Triage

No serial telemetry:

- Check upload port is `COM20`.
- Check board reset after upload.
- Check no other process holds `COM20`.

HTTP fails:

- Confirm IP from serial telemetry.
- In low-power mode SoftAP may be off; STA credentials must be valid.

Power contract fails:

- Inspect `tools/test_power_config.py` before changing firmware field names.
- Update tests only when intentionally changing the contract.

Current measurement fails:

- Check `COM9`.
- Kill stale Python process if needed.
- Confirm SmartUSBHub control cable is connected.
