# AI Error Lessons

This file records mistakes and traps already encountered. Future AI agents
should consult this before proposing fixes.

## Project Isolation

Mistake risk:

- Accidentally editing the older `esp32_sensor_hub` old-board project.

Correct handling:

- Current clean project is `C:\Users\lyl\Desktop\ESP32\esp32_s3m_env_dashboard`.
- Old project is `C:\Users\lyl\Desktop\ESP32\esp32_sensor_hub`.
- Always check old project status if worried:

```powershell
git -C C:\Users\lyl\Desktop\ESP32\esp32_sensor_hub status --short
```

## SmartUSBHub Current Units

Mistake made:

- Interpreted `current_raw=120` as `0.120 mA`.

Correct interpretation:

- `SmartUSBHub.get_channel_current(ch)` returns raw milliamps.
- GUI divides by 1000 to show amps.
- `current_raw=120` means `120 mA`, or `0.120 A`.

## SmartUSBHub Port Handling

Mistake/risk:

- Uploading firmware to the SmartUSBHub control port.
- Letting a timed-out Python process hold `COM9`.

Correct handling:

- Firmware upload port: `COM20`.
- SmartUSBHub control port: `COM9`.
- If `COM9` is busy, find/kill stale Python processes before measuring.
- Prefer explicit `SmartUSBHub("COM9")` over broad scan when other serial
  devices are present.

## Flash Logging Design

Mistake made:

- Single large fixed LittleFS ring with random writes looked good logically but
  caused minute-write stalls around `8645 ms` on this board.

Correct handling:

- Use segmented append ring:
  - `/env_min.hdr`
  - `/env000.bin` to `/env095.bin`
  - 105 records per segment
- Measured segmented writes around `30-60 ms` initially, later single writes
  seen up to hundreds of ms but without loop stalls.
- Do not write 1-second samples to Flash.

## Watchdog

Mistake made:

- Enabling Arduino loop WDT caused unexpected resets during normal operation.

Correct handling:

- Keep soft monitoring fields:
  - `loop_stalls`
  - `max_loop_gap_ms`
- Do not re-enable hard WDT without a focused test proving it does not reset
  during sensor reads, LittleFS writes, HTTP transfers, and LCD updates.

## Power Optimization

Wrong assumption:

- LCD backlight timeout, WiFi STA sleep, and sensor interval changes alone
  would significantly reduce average current.

Measured reality:

- Balanced with AP on: about `128 mA`.
- Low-power before disabling AP/CPU reduction was still about `132 mA`.

Effective changes:

- Disable fallback SoftAP in low-power mode when STA credentials are available.
- Reduce CPU to `80 MHz`.
- Keep STA and HTTP dashboard available.
- Turn LCD backlight off after timeout and wake on BOOT press.

## Arduino CLI Quoting

Mistake made:

- PowerShell quoting around `build.extra_flags` caused Arduino CLI to see too
  many positional args.

Correct handling:

- In direct PowerShell commands, single-quote the build property:

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi" --build-property 'build.extra_flags=-DWIFI_STA_SSID="SSID" -DWIFI_STA_PASS="PASS"' "C:\Users\lyl\Desktop\ESP32\esp32_s3m_env_dashboard"
```

- In committed docs, avoid real credentials and prefer env var examples.

## Host Contract Tests

Potential trap:

- `tools/test_power_config.py` is text-based and may require exact strings or
  comments such as JSON contract markers.

Correct handling:

- Do not remove contract comments unless you update the tests intentionally.
- These tests exist to prevent silent removal of power API/HTML/config hooks.

## Low-Power Connectivity

Potential trap:

- In low-power mode AP is off. If STA credentials are wrong, the board will not
  provide fallback AP.

Correct handling:

- This is intentional for power savings.
- If field debugging without known WiFi is needed, switch to balanced mode or
  compile with correct STA credentials.
