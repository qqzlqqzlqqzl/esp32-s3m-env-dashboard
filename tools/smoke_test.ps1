param(
  [string]$Port = "COM20",
  [string]$Ip = "",
  [string]$Fqbn = "esp32:esp32:esp32s3:PSRAM=opi",
  [int]$SerialSeconds = 12
)

$ErrorActionPreference = "Stop"

function Assert-True {
  param(
    [bool]$Condition,
    [string]$Message
  )
  if (-not $Condition) {
    throw $Message
  }
}

function Get-Json {
  param([string]$Url)
  $raw = & curl.exe --noproxy "*" --silent --show-error --fail --max-time 8 $Url
  Assert-True ($LASTEXITCODE -eq 0) "curl $Url failed"
  return (($raw -join "`n") | ConvertFrom-Json)
}

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $ProjectRoot

Write-Host "[TEST] Host ring-buffer logic"
python .\tools\test_ring_log.py
Assert-True ($LASTEXITCODE -eq 0) "Host ring-buffer tests failed"

Write-Host "[TEST] Host power-management contract"
python .\tools\test_power_config.py
Assert-True ($LASTEXITCODE -eq 0) "Host power-management contract tests failed"

Write-Host "[TEST] Host dashboard/API contract"
python .\tools\test_dashboard_contract.py
Assert-True ($LASTEXITCODE -eq 0) "Host dashboard/API contract tests failed"

$extraFlags = ""
if ($env:WIFI_STA_SSID) {
  $extraFlags += " -DWIFI_STA_SSID=`"$($env:WIFI_STA_SSID)`""
}
if ($env:WIFI_STA_PASS) {
  $extraFlags += " -DWIFI_STA_PASS=`"$($env:WIFI_STA_PASS)`""
}

Write-Host "[TEST] Compile with Arduino CLI"
if ($extraFlags.Length -gt 0) {
  arduino-cli compile --fqbn $Fqbn --build-property "build.extra_flags=$extraFlags" $ProjectRoot
} else {
  arduino-cli compile --fqbn $Fqbn $ProjectRoot
}
Assert-True ($LASTEXITCODE -eq 0) "Arduino CLI compile failed"

Write-Host "[TEST] Serial telemetry on $Port"
$serialText = ""
$serial = New-Object System.IO.Ports.SerialPort $Port,115200,'None',8,'One'
$serial.ReadTimeout = 500
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.Open()
try {
  $deadline = (Get-Date).AddSeconds($SerialSeconds)
  while ((Get-Date) -lt $deadline) {
    $chunk = $serial.ReadExisting()
    if ($chunk) {
      $serialText += $chunk
    }
    Start-Sleep -Milliseconds 200
  }
} finally {
  $serial.Close()
}

$envLines = $serialText -split "(`r`n|`n|`r)" | Where-Object { $_ -match "^\[ENV\]" }
Assert-True (($envLines | Measure-Object).Count -gt 0) "No [ENV] serial telemetry received"
$validEnvLines = $envLines | Where-Object {
  $_ -match "sht=on" -and $_ -match "scd=on" -and $_ -match "sgp=on" -and $_ -match "bh=on" -and $_ -match "co2=([1-9][0-9]*)"
}
Assert-True (($validEnvLines | Measure-Object).Count -gt 0) "No valid all-sensors-online [ENV] serial telemetry received"
$lastEnv = $validEnvLines[-1]
Write-Host "[TEST] Last serial line: $lastEnv"
Assert-True ($lastEnv -match "sht=on") "SHT41 is not online in serial telemetry"
Assert-True ($lastEnv -match "scd=on") "SCD41 is not online in serial telemetry"
Assert-True ($lastEnv -match "sgp=on") "SGP41 is not online in serial telemetry"
Assert-True ($lastEnv -match "bh=on") "BH1750 is not online in serial telemetry"
Assert-True ($lastEnv -match "co2=([1-9][0-9]*)") "SCD41 CO2 value missing or zero in serial telemetry"

if (-not $Ip) {
  $match = [regex]::Match($lastEnv, "ip=([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)")
  Assert-True $match.Success "No IP supplied and no IP found in serial telemetry"
  $Ip = $match.Groups[1].Value
}

Write-Host "[TEST] HTTP API http://$Ip/api/status"
$status = Get-Json "http://$Ip/api/status"
Assert-True ($status.ok -eq $true) "API ok is false"
Assert-True ([int]$status.i2c.clock_hz -eq 400000) "I2C clock is not 400 kHz"
Assert-True ($status.sht41.online -eq $true) "API SHT41 offline"
Assert-True ($status.scd41.online -eq $true) "API SCD41 offline"
Assert-True ($status.sgp41.online -eq $true) "API SGP41 offline"
Assert-True ($status.bh1750.online -eq $true) "API BH1750 offline"
Assert-True ($status.storage.mounted -eq $true) "LittleFS is not mounted"
Assert-True ($null -ne $status.interpretation.co2.text) "CO2 interpretation missing"
Assert-True ($null -ne $status.interpretation.voc.text) "VOC interpretation missing"
Assert-True ($null -ne $status.config.log_interval_ms) "Config block missing"
Assert-True ($null -ne $status.time.time_source) "Time sync block missing"
Assert-True ($null -ne $status.time.epoch_s) "Time epoch missing"
Assert-True ($null -ne $status.time.local_time) "Local time field missing"
Assert-True ($null -ne $status.power.web_boost_active) "Web boost status missing"
Assert-True ($null -ne $status.sensor_options.sht41.available) "Sensor options block missing"
Assert-True ($status.storage.ring_ready -eq $true) "Minute ring is not ready"
Assert-True ([int]$status.storage.ring_capacity -eq 10080) "Minute ring capacity is not 10080"
Assert-True ($null -ne $status.storage.ring_path) "Minute ring path missing"
Assert-True ($null -ne $status.storage.ring_record_bytes) "Minute ring record size missing"
Assert-True ([double]$status.sht41.temp_c -gt -20 -and [double]$status.sht41.temp_c -lt 80) "SHT41 temperature out of plausible range"
Assert-True ([double]$status.sht41.humidity -ge 0 -and [double]$status.sht41.humidity -le 100) "SHT41 humidity out of plausible range"
Assert-True ([int]$status.scd41.co2_ppm -gt 0) "SCD41 CO2 is zero"
Assert-True ([double]$status.bh1750.lux -ge 0) "BH1750 lux is negative"

Write-Host "[TEST] Config save, history, health, and CSV log"
$configUrl = "http://$Ip/api/config?fast_interval_ms=1000&scd_interval_ms=5000&log_interval_ms=1000&live_refresh_ms=1000&co2_warn_ppm=1000&co2_bad_ppm=1500&voc_warn=150&voc_bad=250&nox_warn=10&nox_bad=50&temp_low_c=18&temp_high_c=28&humidity_low=40&humidity_high=70&lux_low=50&lux_high=1500&sht_precision=0&bh1750_mode=0"
$configRaw = & curl.exe --noproxy "*" --silent --show-error --fail --max-time 8 -X POST $configUrl
Assert-True ($LASTEXITCODE -eq 0) "curl POST /api/config failed"
$configResult = (($configRaw -join "`n") | ConvertFrom-Json)
Assert-True ($configResult.saved -eq $true) "Config did not report saved=true"

$history = $null
for ($i = 0; $i -lt 20; $i++) {
  Start-Sleep -Milliseconds 500
  $history = Get-Json "http://$Ip/api/history"
  if (@($history.rows).Count -gt 0) { break }
}
Assert-True (@($history.rows).Count -gt 0) "History API did not collect rows"
$latest = @($history.rows)[-1]
Assert-True ($null -ne $latest.co2_ppm -and $null -ne $latest.voc_index -and $null -ne $latest.lux) "History row missing expected fields"

$minuteHistory = Get-Json "http://$Ip/api/history?range=60"
Assert-True ($minuteHistory.source -eq "minute") "Minute history did not report source=minute"
Assert-True (@($minuteHistory.rows).Count -gt 0) "Minute history has no rows"
$minuteLatest = @($minuteHistory.rows)[-1]
Assert-True ($null -ne $minuteLatest.count -and $null -ne $minuteLatest.co2_ppm) "Minute history row missing aggregate fields"

$health = Get-Json "http://$Ip/api/health"
Assert-True ($health.ok -eq $true) "Health API reports not ok"
Assert-True ($health.ring_ready -eq $true) "Health API reports ring not ready"
Assert-True ([int]$health.minute_rows -ge 1) "Health API minute_rows is empty"

$boostRaw = & curl.exe --noproxy "*" --silent --show-error --fail --max-time 8 -X POST "http://$Ip/api/performance/boost?duration_ms=60000"
Assert-True ($LASTEXITCODE -eq 0) "curl POST /api/performance/boost failed"
$boost = (($boostRaw -join "`n") | ConvertFrom-Json)
Assert-True ($boost.boosted -eq $true) "Performance boost did not report boosted=true"
Assert-True ($boost.web_boost_active -eq $true) "Performance boost did not become active"

$csv = & curl.exe --noproxy "*" --silent --show-error --fail --max-time 8 "http://$Ip/api/log.csv"
Assert-True ($LASTEXITCODE -eq 0) "curl /api/log.csv failed"
$csvText = $csv -join "`n"
Assert-True ([bool]($csvText -match "minute,count,co2_ppm,temp_c,humidity_pct,voc_index,nox_index,lux,ok_ratio")) "Minute CSV header missing"
Assert-True (($csvText -split "(`r`n|`n|`r)" | Where-Object { $_ -match "^[0-9]+," } | Measure-Object).Count -gt 0) "CSV has no data rows"

Write-Host "[TEST] HTTP root page"
$html = & curl.exe --noproxy "*" --silent --show-error --fail --max-time 8 "http://$Ip/"
Assert-True ($LASTEXITCODE -eq 0) "curl root page failed"
$htmlText = $html -join "`n"
Assert-True ([bool]($htmlText -match "ESP32-S3M 环境监测站")) "Root HTML did not contain expected Chinese title"
Assert-True ([bool]($htmlText -match "趋势曲线")) "Root HTML did not contain Chinese trend chart label"
Assert-True ([bool]($htmlText -match "数据解读")) "Root HTML did not contain Chinese data interpretation"
Assert-True ([bool]($htmlText -match "阈值依据")) "Root HTML did not contain threshold basis"
Assert-True ([bool]($htmlText -match "可暴露的传感器能力")) "Root HTML did not contain sensor capability section"
Assert-True ([bool]($htmlText -match "1天")) "Root HTML did not contain one-day history button"
Assert-True ([bool]($htmlText -match "重置配置")) "Root HTML did not contain config reset button"
Assert-True ([bool]($htmlText -match "数据清零")) "Root HTML did not contain log clear button"
Assert-True ([bool]($htmlText -match "加速查看")) "Root HTML did not contain performance boost button"
Assert-True ([bool]($htmlText -match "minuteCache")) "Root HTML did not contain browser history cache"

Write-Host "[PASS] Smoke test passed for $ProjectRoot at http://$Ip/"
