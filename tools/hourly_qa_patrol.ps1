param(
  [string]$Port = "COM20",
  [string]$Ip = "",
  [int]$SerialSeconds = 18,
  [switch]$WithBoost,
  [switch]$SkipBrowser,
  [switch]$SkipPower,
  [string]$SmartUsbPort = "COM9",
  [int]$PowerSamples = 40
)

$ErrorActionPreference = "Stop"
$script:Failures = @()
$script:Warnings = @()

function Add-Failure {
  param([string]$Message)
  $script:Failures += $Message
  Write-Host "[FAIL] $Message"
}

function Add-Warning {
  param([string]$Message)
  $script:Warnings += $Message
  Write-Host "[WARN] $Message"
}

function Invoke-Step {
  param(
    [string]$Name,
    [scriptblock]$Script,
    [switch]$WarnOnly
  )
  Write-Host "[STEP] $Name"
  try {
    & $Script
    if ($LASTEXITCODE -ne $null -and $LASTEXITCODE -ne 0) {
      throw "$Name exited with code $LASTEXITCODE"
    }
  } catch {
    if ($WarnOnly) {
      Add-Warning "$Name`: $($_.Exception.Message)"
    } else {
      Add-Failure "$Name`: $($_.Exception.Message)"
    }
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
    Add-Warning "Serial read skipped on $Name`: $($_.Exception.Message)"
  }
  return $text
}

function Discover-IpFromSerial {
  param(
    [string]$Name,
    [int]$Seconds
  )
  Write-Host "[STEP] Serial telemetry discovery"
  $serialText = Read-SerialTelemetry -Name $Name -Seconds $Seconds
  $envLines = @($serialText -split "(`r`n|`n|`r)" | Where-Object { $_ -match "^\[ENV\]" })
  if ($envLines.Count -eq 0) {
    Add-Warning "No [ENV] serial telemetry found on $Name"
    return ""
  }
  $lastEnv = $envLines[-1]
  Write-Host "[CHECK] Last serial line: $lastEnv"
  foreach ($token in @("sht=on", "scd=on", "sgp=on", "bh=on")) {
    if ($lastEnv -notmatch [regex]::Escape($token)) {
      Add-Failure "Serial telemetry missing $token"
    }
  }
  $match = [regex]::Match($lastEnv, "ip=([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)")
  if ($match.Success) { return $match.Groups[1].Value }
  Add-Warning "Serial telemetry did not expose IP"
  return ""
}

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $ProjectRoot

Write-Host "[INFO] ESP32Mini hourly QA patrol"
Write-Host "[INFO] cwd=$ProjectRoot"
Write-Host "[INFO] time=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"

Invoke-Step "Git status" {
  git status --short --branch
  git log --oneline -3
}

Invoke-Step "Host tests" {
  python .\tools\test_dashboard_contract.py
  if ($LASTEXITCODE -ne 0) { throw "test_dashboard_contract.py failed" }
  python .\tools\test_ring_log.py
  if ($LASTEXITCODE -ne 0) { throw "test_ring_log.py failed" }
  python .\tools\test_power_config.py
  if ($LASTEXITCODE -ne 0) { throw "test_power_config.py failed" }
  python .\tools\test_power_regression_check.py
  if ($LASTEXITCODE -ne 0) { throw "test_power_regression_check.py failed" }
  python .\tools\test_health_verdict.py
  if ($LASTEXITCODE -ne 0) { throw "test_health_verdict.py failed" }
  python -m py_compile .\tools\measure_ch1_current.py .\tools\analyze_csv_log.py .\tools\health_verdict.py .\tools\power_regression_check.py
  if ($LASTEXITCODE -ne 0) { throw "py_compile failed" }
  [scriptblock]::Create((Get-Content -Raw .\tools\http_perf_check.ps1)) | Out-Null
}

if (-not $Ip) {
  $Ip = Discover-IpFromSerial -Name $Port -Seconds $SerialSeconds
}

if ([string]::IsNullOrWhiteSpace($Ip)) {
  Add-Failure "No device IP supplied or discovered"
} else {
  $base = "http://$Ip"
  Invoke-Step "Independent health verdict" {
    python .\tools\health_verdict.py --base-url $base
    if ($LASTEXITCODE -ne 0) { throw "health verdict failed" }
  }

  Invoke-Step "Read-only HTTP perf check" {
    if ($WithBoost) {
      .\tools\http_perf_check.ps1 -Ip $Ip -WithBoost
    } else {
      .\tools\http_perf_check.ps1 -Ip $Ip
    }
    if ($LASTEXITCODE -ne 0) { throw "http perf check failed" }
  }

  if (-not $SkipBrowser) {
    Invoke-Step "Browser UX range-click check" {
      node .\tools\browser_ux_check.mjs --url "$base/"
      if ($LASTEXITCODE -ne 0) { throw "browser UX check failed" }
    }
  }
}

if (-not $SkipPower) {
  Invoke-Step "SmartUSBHub CH1 current sample" {
    python .\tools\power_regression_check.py --mode boost --port $SmartUsbPort --channel 1 --samples $PowerSamples --interval 0.25
    if ($LASTEXITCODE -ne 0) { throw "power measurement failed" }
  } -WarnOnly
}

Write-Host "[SUMMARY] failures=$($script:Failures.Count) warnings=$($script:Warnings.Count)"
if ($script:Warnings.Count -gt 0) {
  $script:Warnings | ForEach-Object { Write-Host "[SUMMARY][WARN] $_" }
}
if ($script:Failures.Count -gt 0) {
  $script:Failures | ForEach-Object { Write-Host "[SUMMARY][FAIL] $_" }
  exit 1
}

Write-Host "[PASS] hourly QA patrol passed"
