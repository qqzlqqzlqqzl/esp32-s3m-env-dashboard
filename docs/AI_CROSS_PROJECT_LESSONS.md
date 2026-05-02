# AI Cross Project Lessons

Audience: future AI agents. This file distills reusable engineering lessons
from `C:\Users\lyl\Desktop\ESP32\esp32_sensor_hub` without importing its old
development-board hardware assumptions.

## Asset Boundary

- `C:\Users\lyl\Desktop\ESP32` and `C:\Users\lyl\Desktop\ESP32Mini` must both
  remain independently usable.
- Shared lab tools should be copied or installed per workspace. Do not make the
  Mini project depend on a file inside the old workspace, and do not make the
  old workspace depend on a file inside Mini.
- `smartusbhub` is a shared lab tool, not Mini-only hardware. This workspace
  uses `C:\Users\lyl\Desktop\ESP32Mini\smartusbhub`; the old workspace may use
  `C:\Users\lyl\Desktop\ESP32\smartusbhub`.
- Do not copy old board source code into Mini just because the old project has a
  working pattern. Reimplement only the pattern that matches SGP41, SCD41,
  SHT41, BH1750, the S3M onboard LCD, LittleFS, HTTP, and Arduino CLI.

## Reusable Architecture

- Treat the firmware as a small environment monitoring station, not as a sensor
  demo.
- Keep a fast RAM view for realtime display and browser updates.
- Persist slower aggregate history to LittleFS; for this project the durable
  history is one minute per point.
- Keep configuration in LittleFS and expose save/reset flows through HTTP.
- Keep `status`, `live`, `history`, `health`, `config`, and CSV endpoints
  stable enough that tests and dashboards can close the loop.
- Pair raw data with human-readable interpretation. For example, SGP41 SRAW is
  diagnostic input, while VOC/NOx indexes are the user-facing air-quality
  values.

## Reusable Verification Pattern

- A change is not done until commands or hardware probes prove it.
- Closure should include host tests, Arduino CLI compile, upload when firmware
  changed, serial readiness, HTTP API checks, dashboard checks, storage checks,
  config persistence, and a short soak.
- For long-run features, cross at least two minute boundaries and verify that
  ring counters advance without write failures, drops, loop stalls, or health
  failures.
- Reports should record command, endpoint, observed result, and key metric.
  A statement such as "looks OK" is not enough.

## What Not To Import

The old project uses board hardware that is not present on the S3M minimal
system board used here. Do not add requirements, tests, UI, or drivers for:

- DHT
- AP3216C
- QMA6100P
- ES8388 audio
- OV5640 or other camera workflows
- XL9555 old-board IO expansion
- Old development-board LCD paths

## SmartUSBHub Rules

- SmartUSBHub control is for power, data-line, voltage, and current automation.
- Firmware upload must use the ESP32 serial port, not the SmartUSBHub control
  serial port.
- On this bench, `COM9` is the SmartUSBHub control port and `CH1` is the
  measured channel. Confirm with device VID/PID when the bench changes.
- `SmartUSBHub.get_channel_current(ch)` returns milliamps. `120` means
  `120 mA`, not `0.120 mA`.
