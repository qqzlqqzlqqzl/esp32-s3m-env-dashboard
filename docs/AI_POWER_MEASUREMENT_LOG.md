# AI Power Measurement Log

Use this log to keep power claims tied to measured CH1 data.

## Measurement Tool

Script:

```powershell
python .\tools\measure_ch1_current.py --port COM9 --channel 1 --samples 80
```

Dependencies:

- Imports `SmartUSBHub` from sibling repo:
  `C:\Users\lyl\Desktop\ESP32\smartusbhub`
- Uses SmartUSBHub control port `COM9`
- Measures channel `CH1`

Unit rule:

- `get_channel_current(1)` raw value is milliamps.
- SmartUSBHub GUI divides by 1000 to display amps.
- `current_raw=120` means about `120 mA`.

## Measurement Template

For future measurements, append:

```text
Date:
Firmware commit:
Build flags:
Device IP:
Power mode:
CPU MHz:
AP enabled:
STA connected:
WiFi sleep:
Backlight state:
LCD brightness:
Sensor interval:
Samples:
Average mA:
Min mA:
Max mA:
Voltage mV:
Notes:
```

## Recorded Measurements

### 2026-05-02 Balanced

- Firmware state: low-power implementation compiled and uploaded.
- Mode: `balanced`
- CPU: `160 MHz`
- AP: enabled
- WiFi STA sleep: enabled
- Backlight: off after timeout during measurement
- Sensor loop: 1-second fast loop
- Samples: 60
- Average: `128.0 mA`
- Min/max: `105-207 mA`
- Notes: used for comparison after AP/CPU optimization.

### 2026-05-02 Low Power

- Firmware state: low-power implementation compiled and uploaded.
- Mode: `low_power`
- CPU: `80 MHz`
- AP: disabled
- STA: enabled and dashboard reachable
- WiFi STA sleep: enabled
- Backlight: off after 5-second timeout
- LCD brightness setting: `15%`
- SHT41/SGP41/BH1750 interval: `30000 ms`
- SCD41 poll interval: `5000 ms`
- Samples: 80
- Average: `66.1 mA`
- Min/max: `41-218 mA`
- Notes: current target for this feature-preserving build is `65-75 mA`.

## Measurement Traps

- Measure after the backlight timeout has actually elapsed if comparing low
  power.
- Confirm `/api/status.power.ap_enabled=false` for low-power STA-only mode.
- Startup and SGP41 conditioning can cause transient differences.
- Web requests can add WiFi peaks; keep measurement window consistent.
- If COM9 is busy, terminate stale Python processes.
