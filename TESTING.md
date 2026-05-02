# Testing

This project uses a zero-trust smoke test. A build that only "looks OK" is not
accepted.

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
```

Power measurement:

```powershell
python .\tools\measure_ch1_current.py --port COM9 --channel 1 --samples 80
```

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
- CO2, SHT41 temperature/humidity, and BH1750 lux values are in plausible ranges.
- Root HTML page is served and includes the trend chart and interpretation panel.

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
