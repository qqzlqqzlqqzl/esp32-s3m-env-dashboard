# ESP32-S3M Environment Dashboard

Clean Arduino project for the DNESP32S3M minimal system board.

Features:

- Reads SGP41, SCD41, SHT41, and BH1750 on I2C `SDA IO4 / SCL IO5`
- Uses 400 kHz hardware I2C
- Shows CO2, temperature, humidity, VOC, NOx, lux, raw SGP41, and storage status on the LCD
- Serves a live HTML dashboard over WiFi with human-readable data interpretation
- Draws history curves for CO2, VOC, NOx, and lux
- Persists configuration and 1-minute aggregate history in LittleFS
- Uses a segmented Flash ring buffer: 96 segments x 105 minute records = 10080 minute points
- Exposes JSON at `/api/status`, `/api/live`, `/api/history`, `/api/health`, `/api/config`
- Exports CSV at `/api/log.csv`
- Uses a browser-side history cache so chart range switching does not re-read
  LittleFS from the ESP32 for every click
- Provides a 网页加速 button and automatic web interaction boost. While active,
  the firmware keeps the low-power CPU setting but temporarily disables WiFi
  sleep so dashboard operations respond faster.
- Supports explicit `POST /api/log/clear` 数据清零 for history/log reset
- Uses NTP time sync when STA WiFi is connected and exposes `time_source`,
  `epoch_s`, and `local_time` in `/api/status`
- Supports a one-button LCD settings menu on the BOOT key: 短按 wakes or
  switches pages; 长按 enters settings; in settings, 短按 moves or changes
  values and 长按 edits/saves/exits.
- Uses Arduino CLI build and upload flow
- Tracks the Power optimization contract in `tools/test_power_config.py`

AI documentation:

- Future AI agents should start at `docs/AI_DOC_INDEX.md`.
- Current state: `docs/AI_PROJECT_STATE.md`
- Handoff: `docs/AI_HANDOFF.md`
- API contract: `docs/AI_API_CONTRACT.md`
- Power/long-run contract: `docs/AI_POWER_LONGRUN_CONTRACT.md`
- Test closure: `docs/AI_TEST_CLOSURE_RUNBOOK.md`
- Storage notes: `docs/AI_STORAGE_RING_NOTES.md`
- Power measurements: `docs/AI_POWER_MEASUREMENT_LOG.md`
- Backlog/issue closure protocol: `docs/AI_BACKLOG_PROTOCOL.md`
- Cross-project lessons: `docs/AI_CROSS_PROJECT_LESSONS.md`
- Known mistakes: `docs/AI_ERROR_LESSONS.md`

Normal operating defaults:

- Fast SHT41/SGP41/BH1750 read interval: `1000 ms`
- SCD41 CO2 poll interval: `5000 ms`
- Realtime RAM sample interval: `60000 ms` by default, usually set to `1000 ms` during testing
- Flash history interval: fixed 1 minute aggregate
- Browser live refresh: `1000 ms`
- CO2 warning/bad: `1000 / 1500 ppm`
- VOC warning/bad index: `150 / 250`
- NOx warning/bad index: `10 / 50`
- Temperature comfort band: `18-28 C`
- Humidity comfort band: `40-70 %`
- Light band: `50-1500 lx`

Power optimization contract:

- `/api/config` should persist `power_mode`, `lcd_brightness_pct`,
  `backlight_timeout_ms`, `wifi_sta_sleep`, and
  `low_power_sensor_interval_ms`.
- `/api/status` should expose a `power` block containing the active
  `power_mode`, LCD brightness, backlight state/timeout, WiFi STA sleep state,
  and effective sensor interval.
- `/api/health` should expose power health fields, including `power_mode`,
  `backlight_on`, `wifi_sta_sleep`, `sleep_eligible`, and `power_ok`.
- Defaults should favor balanced operation: LCD brightness around 60-80%,
  finite backlight timeout, WiFi STA sleep enabled, and a low-power sensor
  interval no faster than 30 seconds.
- The root HTML should expose controls for power mode, LCD brightness,
  backlight timeout, WiFi power-save, and low-power sensor interval.

Measured CH1 current on SmartUSBHub:

- Balanced mode (`AP on`, CPU `160 MHz`, backlight off after timeout,
  1-second sensor loop): about `128 mA` average, `105-207 mA` observed.
- Low-power mode (`STA only`, AP off, CPU `80 MHz`, backlight timeout `5 s`,
  30-second SHT41/SGP41/BH1750 loop): about `66 mA` average, `41-218 mA`
  observed.
- The practical target for the current hardware while keeping WiFi HTML,
  LCD-on-demand, sensors, and minute logging is about `65-75 mA` average.

Current measurement:

```powershell
python .\tools\measure_ch1_current.py --port COM9 --channel 1 --samples 80
```

For test runs, `/api/config` can temporarily set `fast_interval_ms=1000`,
`log_interval_ms=1000`, and `live_refresh_ms=1000`. This increases realtime RAM
updates; Flash still writes only one aggregate per minute.

Build:

```powershell
$env:WIFI_STA_SSID="your-ssid"
$env:WIFI_STA_PASS="your-password"
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi" --build-property "build.extra_flags=-DWIFI_STA_SSID=`"$env:WIFI_STA_SSID`" -DWIFI_STA_PASS=`"$env:WIFI_STA_PASS`"" "C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard"
```

Upload example:

```powershell
arduino-cli upload -p COM20 --fqbn "esp32:esp32:esp32s3:PSRAM=opi" "C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard"
```

If the board is behind SmartUSBHub, do not upload to the hub control port
`VID_1A86 PID_FE0C`.

Data interpretation:

- SCD41 CO2 indicates ventilation. Around 400-800 ppm is normally fine;
  1000 ppm means ventilation should be improved; 1500 ppm is poor air.
- SHT41 temperature and humidity are checked against the configured comfort band.
- SGP41 VOC index is relative. Around 100 is a typical baseline; higher values
  usually mean VOC events such as cooking, alcohol, solvent, smoke, or occupancy.
- SGP41 NOx index starts near clean air and rises with combustion or outdoor
  pollution intrusion. SRAW values are raw algorithm inputs, not direct scores.
- BH1750 lux is visible light intensity. Low values mean dim light; high values
  mean bright light or direct lamp/sun exposure.

Storage model:

- `/api/history` without `range` returns recent RAM realtime samples.
- `/api/history?range=60`, `1440`, `4320`, or `all` returns minute aggregates
  from the Flash ring plus the current in-progress minute.
- The HTML dashboard fetches `range=all` into a browser history cache once, then
  switches 1h/6h/1d/3d/all locally and decimates large ranges before drawing.
- `/api/log.csv` exports minute aggregate CSV, not raw 1-second rows.
- `POST /api/log/clear?confirm=1` clears the LittleFS minute ring and RAM
  realtime history, but leaves configuration intact.
- `/api/status` and `/api/health` expose ring capacity, write count, write
  duration, loop stall count, and WiFi reconnect count for long-run checks.
- When NTP has synced, new minute records use real epoch minutes; before sync,
  records use uptime minutes and `/api/status.time.time_source` reports
  `uptime`.

Local LCD controls:

- Normal mode: BOOT 短按 wakes the LCD and switches dashboard pages.
- Normal mode: BOOT 长按 enters the settings menu.
- Settings menu: 短按 moves to the next item; 长按 enters editing.
- Editing mode: 短按 cycles values, 长按 saves the value.
- The menu exposes core field controls: power mode, LCD brightness, backlight
  timeout, WiFi sleep, SHT41 precision, BH1750 mode, and web boost.
