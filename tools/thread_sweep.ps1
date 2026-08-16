<#
.SYNOPSIS
  Thin wrapper around dtest --thread-sweep (kept for convenience).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\thread_sweep.ps1
  powershell -ExecutionPolicy Bypass -File tools\thread_sweep.ps1 -MinThreads 1 -MaxThreads 64 -Deals 200 -Verify
#>
param(
  [int]$MinThreads = 1,
  [int]$MaxThreads = 128,
  [int]$Step = 1,
  [int]$Deals = 200,
  [int]$Seed = 42,
  [string]$ReportDir = "thread_sweep",
  [switch]$Verify,
  [string]$Dtest = "build-cmake\Release\dtest.exe"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if (! (Test-Path $Dtest)) { throw "dtest not found: $Dtest" }

$spec = "${MinThreads}:${MaxThreads}:${Step}"
$args = @(
  "--random-deals", "$Deals",
  "--seed", "$Seed",
  "--thread-sweep", $spec,
  "--report-dir", $ReportDir
)
if ($Verify) { $args += "--verify" }

Write-Host "Running: $Dtest $($args -join ' ')"
& $Dtest @args
exit $LASTEXITCODE
