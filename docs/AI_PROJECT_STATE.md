# AI Project State

This file is for future AI agents. It records the project state that should be
assumed before making changes.

## Identity

- Project: ESP32-S3M environment monitoring station.
- Board: 正点原子 DNESP32S3M minimal system board.
- This is not the older `esp32_sensor_hub` development-board project.
- Current project root: `C:\Users\lyl\Desktop\ESP32Mini\esp32_s3m_env_dashboard`
- Old project to avoid unless explicitly requested:
  `C:\Users\lyl\Desktop\ESP32\esp32_sensor_hub`
- Common SmartUSBHub tooling is intentionally duplicated per workspace:
  `C:\Users\lyl\Desktop\ESP32Mini\smartusbhub` for this project and
  `C:\Users\lyl\Desktop\ESP32\smartusbhub` for the old workspace.

## Hardware Facts

- I2C:
  - SDA `IO4`
  - SCL `IO5`
  - Clock `400 kHz`
- Sensors:
  - SGP41
  - SCD41
  - SHT41
  - BH1750
- No DHT, QMA, camera, ES8388, AP3216C, or old development-board LCD in this
  project.
- Onboard LCD:
  - 160x80 SPI TFT
  - MOSI `IO11`
  - SCLK `IO12`
  - MISO `IO13`
  - DC `IO40`
  - CS `IO39`
  - RST `IO38`
  - BL `IO41`
- Button:
  - BOOT on GPIO0, active low.

## Lab Ports

- ESP32 firmware upload port: `COM20`
- SmartUSBHub control port: `COM9`
- SmartUSBHub measured channel: `CH1`
- Do not upload firmware to `COM9`.

## Current Capability Snapshot

- Four sensors read over hardware I2C.
- LCD dashboard with manual BOOT page switch.
- HTML dashboard over WiFi.
- LittleFS persisted config.
- LittleFS segmented minute ring log.
- RAM-only realtime samples.
- Human-readable data interpretation.
- JSON APIs for status, live, history, health, config, and CSV export.
- Power optimization modes and diagnostics.

## Key Metrics

- Balanced current: about `128 mA` average.
- Low-power current: about `66 mA` average.
- Practical current target while preserving WiFi HTML, LCD-on-demand, sensors,
  and minute logging: `65-75 mA` average.

## Prohibited Regressions

- Do not add old-board hardware drivers back into this minimal-board project.
- Do not write 1-second realtime samples to Flash.
- Do not revert to single large LittleFS random-write ring.
- Do not remove host-side contract tests because they are "only text tests".
- Do not put real WiFi passwords in committed docs.
