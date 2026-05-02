# ESP32-S3M Environment Dashboard

Target board: ALIENTEK / 正点原子 DNESP32S3M minimal system board.

This is a clean Arduino CLI project for the minimal board. It intentionally does
not reuse the older `esp32_sensor_hub` development-board project.

## Hardware

- I2C sensors:
  - SGP41 at `0x59`
  - SCD41 at `0x62`
  - SHT41/SHT4x at `0x44`
  - BH1750 at `0x23`, fallback `0x5C`
- I2C wiring:
  - SDA -> IO4
  - SCL -> IO5
  - Clock -> 400 kHz
- Board LCD from S3M factory program:
  - 0.96 inch 160x80 SPI TFT
  - MOSI IO11
  - SCLK IO12
  - MISO IO13
  - DC/WR IO40
  - CS IO39
  - RST IO38
  - BL IO41

## Arduino Libraries

Required libraries already installed:

- Sensirion I2C SGP41
- Sensirion Gas Index Algorithm
- Sensirion I2C SCD4x
- Sensirion I2C SHT4x
- Sensirion Core
- BH1750

## Product Direction

Treat this as a small environment monitoring station, not a throwaway sensor
demo. Reuse the engineering ideas from `esp32_sensor_hub`:

- LittleFS persisted segmented ring logging: 1-second realtime samples stay in RAM;
  Flash stores 1-minute aggregates only.
- LittleFS persisted configuration
- Live/status/history/health/config/log APIs
- Browser dashboard with curves
- Human-readable interpretation next to raw sensor values
- Zero-trust smoke test before calling work complete

Do not copy unused old-board hardware into this project. The new minimal board
does not use DHT, QMA, camera, ES8388, AP3216C, or the old development-board LCD.

## Data Meaning

- CO2 from SCD41: ventilation indicator. Default warn/bad thresholds are
  `1000 / 1500 ppm`.
- Temperature and humidity from SHT41: comfort and condensation/mold risk band.
  Defaults are `18-28 C` and `40-70 %RH`.
- VOC index from SGP41: relative air-quality index. Around `100` is baseline;
  sustained values above `150` deserve attention.
- NOx index from SGP41: combustion/outdoor-pollution indicator. Clean air is
  usually near the low end; sustained increases above `10` are warnings.
- SGP41 SRAW VOC/NOx: raw algorithm inputs. Show them for diagnostics, but do
  not present them as direct human air-quality scores.
- Lux from BH1750: visible light level. Default normal band is `50-1500 lx`.

## Long Run Storage

- Flash history uses a segmented ring:
  - header: `/env_min.hdr`
  - segments: `/env000.bin` through `/env095.bin`
  - 105 minute records per segment, 96 segments total, 10080 minute points.
- This is about 7 days at one point per minute. Realtime 1-second rows are RAM
  only and capped by `kHistoryCapacity`.
- Do not go back to per-sample Flash writes. LittleFS random writes into one
  large fixed file were measured at about 8.6 seconds per minute write on this
  board; segmented append writes are about 30-60 ms in the current test.
- Health/status expose `ring_count`, `ring_total_writes`,
  `last_persist_duration_ms`, `max_persist_duration_ms`, `loop_stalls`, and
  `max_loop_gap_ms` for long-run diagnostics.

## Power Optimization

- CH1 current can be read through SmartUSBHub control port `COM9` with
  `tools/measure_ch1_current.py`.
- SmartUSBHub current raw values are milliamps. The GUI divides by 1000 to show
  amps, so `current_raw=120` means about `120 mA`.
- Measured after optimization:
  - Balanced: about `128 mA` average, AP on, CPU 160 MHz.
  - Low power: about `66 mA` average, STA only, AP off, CPU 80 MHz, backlight
    off after 5 seconds, low-power sensor interval 30 seconds.
- Low-power mode keeps core function: STA HTML dashboard, LCD on BOOT press,
  all four sensors, LittleFS minute logging, and HTTP APIs. It disables fallback
  SoftAP when STA credentials are compiled in.

## Build

Use Arduino CLI only:

```powershell
$env:WIFI_STA_SSID="your-ssid"
$env:WIFI_STA_PASS="your-password"
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi" --build-property "build.extra_flags=-DWIFI_STA_SSID=`"$env:WIFI_STA_SSID`" -DWIFI_STA_PASS=`"$env:WIFI_STA_PASS`"" "C:\Users\lyl\Desktop\ESP32\esp32_s3m_env_dashboard"
```

Upload through the ESP32 serial port, not the SmartUSBHub control port.
