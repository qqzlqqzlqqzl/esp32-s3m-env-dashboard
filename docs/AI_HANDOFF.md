# AI Handoff

This is the current handoff state after sensor integration, long-run logging,
and power optimization.

## Current Firmware State

- Firmware file: `esp32_s3m_env_dashboard.ino`
- Sensors connected on I2C:
  - SGP41 `0x59`
  - SCD41 `0x62`
  - SHT41/SHT4x `0x44`
  - BH1750 `0x23`, fallback `0x5C`
- I2C pins:
  - SDA `IO4`
  - SCL `IO5`
  - Clock `400000 Hz`
- LCD:
  - Onboard 160x80 SPI TFT
  - MOSI `IO11`, SCLK `IO12`, MISO `IO13`, DC `IO40`, CS `IO39`, RST `IO38`,
    BL `IO41`
  - Full-frame framebuffer push is used; avoid per-pixel SPI flush.
- Button:
  - BOOT key on GPIO0, active low
  - Press cycles LCD pages and wakes backlight.

## Active Feature Set

- HTML dashboard in Chinese.
- LCD pages:
  - Main dashboard
  - Threshold margin
  - Sensor details
  - System status
- APIs:
  - `/api/status`
  - `/api/live`
  - `/api/history`
  - `/api/history?range=60|360|1440|4320|all`
  - `/api/health`
  - `/api/config`
  - `/api/config/reset`
  - `/api/log.csv`
- LittleFS persisted configuration.
- LittleFS segmented minute ring history.
- Human-readable interpretation for CO2, VOC, NOx, temperature, humidity, and
  lux.
- Power config exposed through API and root HTML:
  - `power_mode`
  - `lcd_brightness_pct`
  - `backlight_timeout_ms`
  - `wifi_sta_sleep`
  - `low_power_sensor_interval_ms`

## Storage Model

- Realtime rows are RAM-only.
- Flash stores only 1-minute aggregate records.
- Ring header: `/env_min.hdr`
- Segment files: `/env000.bin` through `/env095.bin`
- Capacity:
  - `96` segments
  - `105` minute records per segment
  - `10080` minute records total
  - About 7 days of minute history
- CSV export is minute aggregate CSV, not raw 1-second rows.

## Current Power State

Latest device config after optimization:

- `power_mode=low_power`
- CPU `80 MHz`
- AP disabled when STA credentials are compiled in
- STA HTML/dashboard remains available
- WiFi STA sleep enabled
- LCD brightness `15%`
- Backlight timeout `5000 ms`
- Backlight currently off after timeout
- SHT41/SGP41/BH1750 effective low-power interval `30000 ms`
- SCD41 CO2 poll remains `5000 ms`

Measured CH1 current:

- Balanced mode: about `128.0 mA` average, `105-207 mA`
- Low-power mode: about `66.1 mA` average, `41-218 mA`

Interpret SmartUSBHub current correctly:

- `get_channel_current(1)` returns raw milliamps.
- The SmartUSBHub GUI divides by 1000 to display amps.
- `current_raw=120` means about `120 mA`, not `0.120 mA`.

## Current Network/Port State

- Board upload serial: `COM20`
- SmartUSBHub control serial: `COM9`
- Last known STA IP: `192.168.124.67`
- Do not upload to `COM9`; it is SmartUSBHub control.

## Git State

- Repository was created for this new clean project.
- Remote: `https://github.com/qqzlqqzlqqzl/esp32-s3m-env-dashboard.git`
- Prior pushed commit: `9d287a2 Add ESP32-S3M environment dashboard power optimization`
- Current docs work should be committed and pushed separately after validation.

## Do Not Regress

- Do not restore raw per-sample Flash writes.
- Do not restore single large fixed LittleFS random-write ring; it caused
  minute-write stalls around 8.6 seconds.
- Do not enable fallback SoftAP in low-power mode when STA credentials exist.
- Do not use hardware loop WDT casually; it caused resets during earlier tests.
- Do not remove test hooks just because they are host-side string tests.
