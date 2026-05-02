# AI API Contract

This document fixes the HTTP API surface expected by tests and downstream AI
work.

## `/api/status`

Top-level object should include:

- `ok`
- `i2c`
- `network`
- `sht41`
- `scd41`
- `sgp41`
- `bh1750`
- `interpretation`
- `config`
- `power`
- `sensor_options`
- `storage`
- `system`

Important `network` fields:

- `mode`
- `ip`
- `reconnects`
- `last_reconnect_ms`

Important `config` fields:

- `power_mode`
- `fast_interval_ms`
- `scd_interval_ms`
- `log_interval_ms`
- `live_refresh_ms`
- `lcd_brightness_pct`
- `backlight_timeout_ms`
- `wifi_sta_sleep`
- `low_power_sensor_interval_ms`
- threshold fields
- `sht_precision`
- `bh1750_mode`

Important `power` fields:

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

Important `storage` fields:

- `mounted`
- `persisted_samples`
- `failures`
- `ring_ready`
- `ring_path`
- `ring_capacity`
- `ring_segments`
- `ring_segment_records`
- `ring_count`
- `ring_write_index`
- `ring_total_writes`
- `ring_record_bytes`
- `raw_ram_rows`
- `last_persist_duration_ms`
- `max_persist_duration_ms`
- `used_bytes`
- `total_bytes`

Important `system` fields:

- `uptime_s`
- `heap`
- `last_loop_ms`
- `loop_stalls`
- `max_loop_gap_ms`
- `watchdog`

## `/api/config`

Method:

- `GET` returns same JSON shape as `/api/status`.
- `POST` accepts query parameters.

Power parameters:

- `power_mode`: `low_power`, `balanced`, `performance`
- `lcd_brightness_pct`: `0..100`
- `backlight_timeout_ms`: `5000..600000`
- `wifi_sta_sleep`: `0|1|true|false|on|off`
- `low_power_sensor_interval_ms`: `30000..600000`

Other important parameters:

- `fast_interval_ms`: `500..10000`
- `scd_interval_ms`: `5000..60000`
- `log_interval_ms`: `500..600000`
- `live_refresh_ms`: `250..10000`
- threshold fields
- `sht_precision`: `0..2`
- `bh1750_mode`: `0..2`

Persistence:

- Config is written to `/env_config.txt`.

## `/api/health`

Used for closure checks. Must include:

- `ok`
- `sensors_ok`
- `storage_ok`
- `ring_ready`
- `persisted_samples`
- `minute_rows`
- `wifi_reconnects`
- `free_heap`
- `loop_stalls`
- `max_loop_gap_ms`
- `last_persist_duration_ms`
- `power_ok`
- `power_mode`
- `backlight_on`
- `wifi_sta_sleep`
- `ap_enabled`
- `cpu_mhz`
- `sleep_eligible`

## `/api/history`

Without `range`:

- Returns RAM realtime samples.
- Source should be `ram`.

With `range`:

- Example: `/api/history?range=60`
- Returns minute aggregate rows.
- Source should be `minute`.
- `range=all` returns all ring rows plus current in-progress minute.

## `/api/log.csv`

- Exports minute aggregate CSV.
- Not raw 1-second CSV.
- Header:

```text
minute,count,co2_ppm,temp_c,humidity_pct,voc_index,nox_index,lux,ok_ratio
```

## Compatibility Rule

Adding fields is allowed. Renaming or deleting fields requires synchronized
changes in:

- Firmware
- Root HTML JavaScript
- `tools/test_power_config.py`
- `tools/smoke_test.ps1`
- AI docs
