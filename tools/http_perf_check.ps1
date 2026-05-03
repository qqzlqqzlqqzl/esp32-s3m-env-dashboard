param(
  [string]$Port = "COM20",
  [string]$Ip = "",
  [int]$SerialSeconds = 8,
  [switch]$WithBoost,
  [int]$CurlMaxSeconds = 30,
  [int]$WarnHistoryAllMs = 8000,
  [int]$WarnCsvMs = 6000,
  [int]$WarnRootMs = 1500
)

$ErrorActionPreference = "Stop"
$script:Metrics = @()

trap {
  Write-Host "[FAIL] $($_.Exception.Message)"
  if ($script:Metrics.Count -gt 0) {
    Write-Host "[METRICS]"
    $script:Metrics | Format-Table -AutoSize | Out-String | Write-Host
  }
  exit 1
}

function Add-Metric {
  param(
    [string]$Method,
    [string]$Url,
    [int]$Ms,
    [string]$Result,
    [int]$Bytes = 0
  )
  $script:Metrics += [pscustomobject]@{
    Method = $Method
    Url = $Url
    Ms = $Ms
    Result = $Result
    Bytes = $Bytes
  }
}

function Assert-True {
  param(
    [bool]$Condition,
    [string]$Message
  )
  if (-not $Condition) {
    throw $Message
  }
}

function Invoke-TimedCurl {
  param(
    [string]$Url,
    [string]$Method = "GET"
  )
  $timer = [System.Diagnostics.Stopwatch]::StartNew()
  if ($Method -eq "POST") {
    $raw = & curl.exe --noproxy "*" --silent --show-error --fail --max-time $CurlMaxSeconds -X POST $Url
  } else {
    $raw = & curl.exe --noproxy "*" --silent --show-error --fail --max-time $CurlMaxSeconds $Url
  }
  $exit = $LASTEXITCODE
  $timer.Stop()
  $text = ($raw -join "`n")
  if ($exit -ne 0) {
    Add-Metric -Method $Method -Url $Url -Ms ([int]$timer.ElapsedMilliseconds) -Result "FAIL curl=$exit" -Bytes $text.Length
  } else {
    Add-Metric -Method $Method -Url $Url -Ms ([int]$timer.ElapsedMilliseconds) -Result "OK" -Bytes $text.Length
  }
  Assert-True ($exit -eq 0) "curl $Method $Url failed"
  return [pscustomobject]@{
    Url = $Url
    Method = $Method
    Ms = [int]$timer.ElapsedMilliseconds
    Text = $text
  }
}

function Invoke-TimedCurlExpectFailure {
  param(
    [string]$Url,
    [string]$Method = "POST"
  )
  $timer = [System.Diagnostics.Stopwatch]::StartNew()
  $raw = & curl.exe --noproxy "*" --silent --show-error --max-time $CurlMaxSeconds -X $Method -w "`nHTTP_CODE:%{http_code}" $Url
  $timer.Stop()
  $text = $raw -join "`n"
  Add-Metric -Method $Method -Url $Url -Ms ([int]$timer.ElapsedMilliseconds) -Result "PROTECTED" -Bytes $text.Length
  Assert-True ($text -match "HTTP_CODE:400") "$Method $Url should be protected by HTTP 400"
  return [pscustomobject]@{
    Url = $Url
    Method = $Method
    Ms = [int]$timer.ElapsedMilliseconds
    Text = $text
  }
}

function Read-SerialTelemetry {
  param(
    [string]$Name,
    [int]$Seconds
  )
  $text = ""
  try {
    $serial = New-Object System.IO.Ports.SerialPort $Name,115200,'None',8,'One'
    $serial.ReadTimeout = 500
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.Open()
    try {
      $deadline = (Get-Date).AddSeconds($Seconds)
      while ((Get-Date) -lt $deadline) {
        $chunk = $serial.ReadExisting()
        if ($chunk) { $text += $chunk }
        Start-Sleep -Milliseconds 200
      }
    } finally {
      $serial.Close()
    }
  } catch {
    Write-Host "[WARN] Serial read skipped on $Name`: $($_.Exception.Message)"
  }
  return $text
}

function Convert-JsonText {
  param([string]$Text)
  return ($Text | ConvertFrom-Json)
}

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $ProjectRoot

if (-not $Ip) {
  Write-Host "[CHECK] Reading serial telemetry from $Port for IP discovery"
  $serialText = Read-SerialTelemetry -Name $Port -Seconds $SerialSeconds
  $envLines = $serialText -split "(`r`n|`n|`r)" | Where-Object { $_ -match "^\[ENV\]" }
  if (($envLines | Measure-Object).Count -gt 0) {
    $lastEnv = $envLines[-1]
    Write-Host "[CHECK] Last serial line: $lastEnv"
    Assert-True ($lastEnv -match "sht=on") "Serial SHT41 is not online"
    Assert-True ($lastEnv -match "scd=on") "Serial SCD41 is not online"
    Assert-True ($lastEnv -match "sgp=on") "Serial SGP41 is not online"
    Assert-True ($lastEnv -match "bh=on") "Serial BH1750 is not online"
    $match = [regex]::Match($lastEnv, "ip=([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)")
    if ($match.Success) { $Ip = $match.Groups[1].Value }
  }
}

Assert-True (-not [string]::IsNullOrWhiteSpace($Ip)) "No IP supplied and no IP found from serial telemetry"
$base = "http://$Ip"

Write-Host "[CHECK] GET /api/status"
$statusResp = Invoke-TimedCurl "$base/api/status"
$status = Convert-JsonText $statusResp.Text
Assert-True ($status.ok -eq $true) "/api/status ok=false"
Assert-True ([int]$status.i2c.clock_hz -eq 400000) "I2C clock is not 400 kHz"
Assert-True ($status.sht41.online -eq $true) "SHT41 offline"
Assert-True ($status.scd41.online -eq $true) "SCD41 offline"
Assert-True ($status.sgp41.online -eq $true) "SGP41 offline"
Assert-True ($status.bh1750.online -eq $true) "BH1750 offline"
Assert-True ($status.storage.mounted -eq $true) "LittleFS not mounted"
Assert-True ($status.storage.ring_ready -eq $true) "Minute ring is not ready"
Assert-True ($null -ne $status.time.time_source) "time_source missing"
Assert-True ($null -ne $status.time.epoch_s) "epoch_s missing"
Assert-True ($null -ne $status.time.local_time) "local_time missing"
Assert-True ($null -ne $status.power.web_boost_active) "web_boost_active missing"

Write-Host "[CHECK] GET /api/health"
$healthResp = Invoke-TimedCurl "$base/api/health"
$health = Convert-JsonText $healthResp.Text
Assert-True ($health.ok -eq $true) "/api/health ok=false"
Assert-True ($health.ring_ready -eq $true) "/api/health ring_ready=false"
Assert-True ($health.power_ok -eq $true) "/api/health power_ok=false"

if ($WithBoost) {
  Write-Host "[CHECK] POST /api/performance/boost"
  $boostResp = Invoke-TimedCurl "$base/api/performance/boost?duration_ms=60000" "POST"
  $boost = Convert-JsonText $boostResp.Text
  Assert-True ($boost.boosted -eq $true) "boosted=false"
  Assert-True ($boost.web_boost_active -eq $true) "web_boost_active=false after boost"
}

Write-Host "[CHECK] GET /api/history?range=60"
$hist60Resp = Invoke-TimedCurl "$base/api/history?range=60"
$hist60 = Convert-JsonText $hist60Resp.Text
Assert-True ($hist60.source -eq "minute") "range=60 should return minute source"
Assert-True (@($hist60.rows).Count -gt 0) "range=60 has no rows"

Write-Host "[CHECK] GET /api/history?range=all"
$histAllResp = Invoke-TimedCurl "$base/api/history?range=all"
$histAll = Convert-JsonText $histAllResp.Text
Assert-True ($histAll.source -eq "minute") "range=all should return minute source"
Assert-True (@($histAll.rows).Count -ge @($hist60.rows).Count) "range=all returned fewer rows than range=60"
if ($histAllResp.Ms -gt $WarnHistoryAllMs) {
  Write-Host "[WARN] /api/history?range=all took $($histAllResp.Ms) ms; warn threshold $WarnHistoryAllMs ms"
}

Write-Host "[CHECK] GET /api/log.csv"
$csvResp = Invoke-TimedCurl "$base/api/log.csv"
Assert-True ($csvResp.Text -match "minute,count,co2_ppm,temp_c,humidity_pct,voc_index,nox_index,lux,ok_ratio") "CSV header missing"
if ($csvResp.Ms -gt $WarnCsvMs) {
  Write-Host "[WARN] /api/log.csv took $($csvResp.Ms) ms; warn threshold $WarnCsvMs ms"
}

Write-Host "[CHECK] GET /"
$rootResp = Invoke-TimedCurl "$base/"
$html = $rootResp.Text
foreach ($token in @("ESP32-S3M 环境监测站", "minuteCache", "ensureMinuteCache", "数据清零", "加速查看", "趋势曲线", "1小时", "6小时", "1天", "全部分钟")) {
  Assert-True ($html.Contains($token)) "Root HTML missing $token"
}
if ($rootResp.Ms -gt $WarnRootMs) {
  Write-Host "[WARN] / took $($rootResp.Ms) ms; warn threshold $WarnRootMs ms"
}

Write-Host "[CHECK] POST /api/log/clear without confirm must fail"
$clearProtection = Invoke-TimedCurlExpectFailure "$base/api/log/clear" "POST"
Assert-True ($clearProtection.Text -match "confirm=1") "clear protection response should mention confirm=1"

Write-Host "[PASS] Read-only HTTP patrol passed for $base"
Write-Host "[METRICS]"
$script:Metrics | Format-Table -AutoSize | Out-String | Write-Host
Write-Host ("[METRIC] status={0}ms health={1}ms hist60={2}ms histAll={3}ms rowsAll={4} csv={5}ms root={6}ms clearProtect={7}ms boost={8}" -f `
  $statusResp.Ms, $healthResp.Ms, $hist60Resp.Ms, $histAllResp.Ms, @($histAll.rows).Count, $csvResp.Ms, $rootResp.Ms, $clearProtection.Ms, [bool]$WithBoost)
