# AI Power And Long-Run Contract

This is the firmware contract for power management and long-term operation.

## Power Config Keys

These keys must be persisted in `/env_config.txt`, accepted by `/api/config`,
shown in the HTML config UI, and surfaced through `/api/status`:

- `power_mode`
- `lcd_brightness_pct`
- `backlight_timeout_ms`
- `wifi_sta_sleep`
- `low_power_sensor_interval_ms`

## Power Mode Semantics

`low_power`:

- CPU target: `80 MHz`
- WiFi: STA only when STA credentials are compiled in.
- SoftAP: off when STA credentials exist.
- WiFi sleep: enabled when `wifi_sta_sleep=true`.
- LCD: PWM brightness controlled by `lcd_brightness_pct`; backlight turns off
  after `backlight_timeout_ms`; BOOT press wakes it.
- SHT41/SGP41/BH1750 effective interval: `low_power_sensor_interval_ms`.
- SCD41 remains periodic and is currently polled every `scd_interval_ms`.

`balanced`:

- CPU target: `160 MHz`
- WiFi: AP+STA
- Default LCD brightness around 60-80%.
- Intended for interactive testing and normal lab usage.

`performance`:

- CPU target: `240 MHz`
- Sensor fast interval may run faster than balanced.
- Use only when refresh responsiveness matters more than current.

## API Contract

`/api/status.power` must expose:

- `power_mode`
- `lcd_brightness_pct`
- `backlight_on`
- `backlight_timeout_ms`
- `wifi_sta_sleep`
- `sensor_interval_ms`
- `low_power_sensor_interval_ms`
- `ap_enabled`
- `cpu_mhz`
- `sleep_eligible`

`/api/health` must expose:

- `power_ok`
- `power_mode`
- `backlight_on`
- `wifi_sta_sleep`
- `ap_enabled`
- `cpu_mhz`
- `sleep_eligible`

Do not rename fields without updating:

- `tools/test_power_config.py`
- `tools/smoke_test.ps1`
- AI docs
- HTML dashboard JavaScript

## Long-Run Contract

The system should support months of continuous operation by avoiding frequent
Flash erase/write cycles and by exposing failure counters.

Required health/status fields:

- `ring_ready`
- `ring_count`
- `ring_total_writes`
- `last_persist_duration_ms`
- `max_persist_duration_ms`
- `loop_stalls`
- `max_loop_gap_ms`
- `wifi_reconnects`

Success standard:

- Ring writes increase after minute boundaries.
- `/api/health.ok` remains `true`.
- `storage_ok=true`.
- `sensors_ok=true`.
- `wifi_reconnects` does not climb continuously.
- `loop_stalls` should remain low or zero.
- Persist duration should stay low enough not to create visible service stalls.

## Current Evidence

- Low-power mode measured around `66.1 mA` average on CH1.
- Balanced mode measured around `128.0 mA` average on CH1.
- Segmented ring replaced a bad single-file random-write design.
