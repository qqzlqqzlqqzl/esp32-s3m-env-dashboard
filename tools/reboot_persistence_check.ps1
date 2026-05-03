param(
  [string]$Port = "COM20",
  [string]$Ip = "192.168.124.67",
  [int]$TimeoutSeconds = 90
)

$ErrorActionPreference = "Stop"

function Assert-True {
  param(
    [bool]$Condition,
    [string]$Message
  )
  if (-not $Condition) { throw $Message }
}

function Get-Json {
  param([string]$Url)
  $raw = & curl.exe --noproxy "*" --silent --show-error --fail --max-time 10 $Url
  Assert-True ($LASTEXITCODE -eq 0) "curl $Url failed"
  return (($raw -join "`n") | ConvertFrom-Json)
}

function Get-HistoryAll {
  param([string]$Base)
  $history = Get-Json "$Base/api/history?range=all"
  Assert-True ($history.source -eq "minute") "history range=all source is not minute"
  return $history
}

function Reset-Board {
  param([string]$Name)
  $serial = New-Object System.IO.Ports.SerialPort $Name,115200,'None',8,'One'
  $serial.ReadTimeout = 500
  $serial.DtrEnable = $false
  $serial.RtsEnable = $false
  $serial.Open()
  try {
    $serial.RtsEnable = $true
    $serial.DtrEnable = $true
    Start-Sleep -Milliseconds 150
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
  } finally {
    $serial.Close()
  }
}

function Wait-Ready {
  param(
    [string]$Base,
    [int]$Seconds
  )
  $deadline = (Get-Date).AddSeconds($Seconds)
  $lastError = ""
  while ((Get-Date) -lt $deadline) {
    try {
      $status = Get-Json "$Base/api/status"
      if ($status.ok -eq $true) { return $status }
    } catch {
      $lastError = $_.Exception.Message
    }
    Start-Sleep -Seconds 2
  }
  throw "Device did not become ready after reboot: $lastError"
}

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $ProjectRoot
$base = "http://$Ip"

Write-Host "[CHECK] Before reboot status/history"
$beforeStatus = Get-Json "$base/api/status"
$beforeHistory = Get-HistoryAll $base
$beforeConfig = $beforeStatus.config
$beforeRows = @($beforeHistory.rows).Count
$beforeRingCount = [int]$beforeStatus.storage.ring_count
$beforeTotalWrites = [int]$beforeStatus.storage.ring_total_writes

Assert-True ($beforeStatus.storage.mounted -eq $true) "LittleFS not mounted before reboot"
Assert-True ($beforeStatus.storage.ring_ready -eq $true) "minute ring not ready before reboot"
Assert-True ($beforeStatus.power.power_mode -eq "low_power") "pre-reboot power mode is not low_power"
Assert-True ($beforeStatus.power.ap_enabled -eq $false) "pre-reboot AP unexpectedly enabled"
Assert-True ($beforeStatus.network.mode -eq "STA") "pre-reboot network mode is not STA"
Assert-True ($beforeRows -gt 0) "pre-reboot history is empty"

Write-Host "[CHECK] Trigger serial reset on $Port"
Reset-Board $Port

Write-Host "[CHECK] Wait for device after reboot"
$afterStatus = Wait-Ready $base $TimeoutSeconds
$afterHistory = Get-HistoryAll $base
$afterRows = @($afterHistory.rows).Count
$afterRingCount = [int]$afterStatus.storage.ring_count
$afterTotalWrites = [int]$afterStatus.storage.ring_total_writes

Write-Host "[CHECK] Verify persisted config"
$keys = @(
  "power_mode",
  "lcd_brightness_pct",
  "backlight_timeout_ms",
  "wifi_sta_sleep",
  "low_power_sensor_interval_ms",
  "co2_warn_ppm",
  "co2_bad_ppm",
  "voc_warn",
  "voc_bad",
  "nox_warn",
  "nox_bad",
  "temp_low_c",
  "temp_high_c",
  "humidity_low",
  "humidity_high",
  "lux_low",
  "lux_high",
  "sht_precision",
  "bh1750_mode"
)
foreach ($key in $keys) {
  $beforeValue = [string]$beforeConfig.$key
  $afterValue = [string]$afterStatus.config.$key
  Assert-True ($beforeValue -eq $afterValue) "config $key changed across reboot: $beforeValue -> $afterValue"
}

Write-Host "[CHECK] Verify storage/history survived reboot"
Assert-True ($afterStatus.storage.mounted -eq $true) "LittleFS not mounted after reboot"
Assert-True ($afterStatus.storage.ring_ready -eq $true) "minute ring not ready after reboot"
Assert-True ($afterRows -gt 0) "post-reboot history is empty"
Assert-True ($afterRows -ge [Math]::Min($beforeRows, $beforeRows - 1)) "post-reboot history rows unexpectedly dropped: $beforeRows -> $afterRows"
Assert-True ($afterRingCount -ge [Math]::Min($beforeRingCount, $beforeRingCount - 1)) "ring_count unexpectedly dropped: $beforeRingCount -> $afterRingCount"
Assert-True ($afterTotalWrites -ge $beforeTotalWrites) "ring_total_writes decreased: $beforeTotalWrites -> $afterTotalWrites"

Write-Host "[CHECK] Verify low-power STA behavior"
Assert-True ($afterStatus.power.power_mode -eq "low_power") "post-reboot power mode is not low_power"
Assert-True ($afterStatus.power.ap_enabled -eq $false) "post-reboot AP unexpectedly enabled"
Assert-True ($afterStatus.network.mode -eq "STA") "post-reboot network mode is not STA"
Assert-True ([int]$afterStatus.power.cpu_mhz -eq 80) "post-reboot CPU is not 80 MHz"

Write-Host "[CHECK] Health and time continuity after reboot"
python .\tools\health_verdict.py --base-url $base
Assert-True ($LASTEXITCODE -eq 0) "health verdict failed after reboot"
python .\tools\time_continuity_check.py --base-url $base
Assert-True ($LASTEXITCODE -eq 0) "time continuity failed after reboot"

Write-Host ("[METRIC] before_rows={0} after_rows={1} before_ring_count={2} after_ring_count={3} before_writes={4} after_writes={5}" -f `
  $beforeRows, $afterRows, $beforeRingCount, $afterRingCount, $beforeTotalWrites, $afterTotalWrites)
Write-Host "[PASS] reboot persistence check passed"
