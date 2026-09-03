<#
.SYNOPSIS
  Build libdds.so in a wslc python:3.12-slim container (matches Dockerfile.postmortem).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\build_linux_so.ps1
#>
param(
  [string]$Image = "python:3.12-slim"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$wslc = "C:\Program Files\WSL\wslc.exe"
if (! (Test-Path $wslc)) { $wslc = "wslc" }

Write-Host "Building libdds.so in $Image via wslc..."
# Mount the repo and run the Linux script. sed strips CR so a Windows-checked
# script still runs under bash.
& $wslc run --rm `
  -v "${repoRoot}:/src/ddss" `
  -w /src/ddss `
  $Image `
  bash -lc "sed -i 's/\r$//' tools/build_linux_so.sh tools/smoke_libdds.py; bash tools/build_linux_so.sh; python3 tools/smoke_libdds.py dist/linux-x86_64/libdds.so; LD_LIBRARY_PATH=/src/ddss/build-linux ./build-linux/dtest --random-deals 5 --seed 42 --numthr 2 --report-dir /tmp/dtest_linux_smoke"

if ($LASTEXITCODE -ne 0) { throw "Linux libdds.so build/smoke failed (exit $LASTEXITCODE)" }

Write-Host ""
Write-Host "Artifact: $repoRoot\dist\linux-x86_64\libdds.so"
Get-Item (Join-Path $repoRoot "dist\linux-x86_64\libdds.so") | Format-List FullName, Length, LastWriteTime
