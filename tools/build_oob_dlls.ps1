<#
.SYNOPSIS
  Builds the reference (OOB) DDS DLLs used by `dtest --verify`:

    dds_oob.dll   upstream dds-bridge/dds 2.9  (pinned commit)
    dds3_oob.dll  upstream dds-bridge/dds 3.0  (pinned commit, develop rewrite)

  Both are cloned into <repo>/external/ (gitignored), built with MSVC, and
  copied next to dtest.exe so that `dtest --verify` picks them up
  automatically for the three-way ddss vs dds 2.9 vs dds 3.0 comparison.

.NOTES
  Requirements: git, cmake, Visual Studio 2022 with the C++ workload.

  The DDS 3.0 checkout needs a one-line fix: upstream's
  solution/dds_native.vcxproj omits library/src/system/parallel_boards.cpp,
  which leaves resolve_worker_count / parallel_all_boards_n unresolved at
  link time.  This script patches the project file after checkout.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\build_oob_dlls.ps1
  powershell -ExecutionPolicy Bypass -File tools\build_oob_dlls.ps1 -DtestDir build-cmake\Release
#>

param(
  # Directory (relative to the repo root) containing dtest.exe, where the
  # OOB DLLs are installed.
  [string]$DtestDir = "build-cmake\Release"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$external = Join-Path $repoRoot "external"
New-Item -ItemType Directory -Force -Path $external | Out-Null

$upstreamUrl = "https://github.com/dds-bridge/dds.git"

# Pinned upstream commits so results are reproducible.
# dds 2.9: develop as of 2026-02 (reports version 2.9.0).
$dds29Sha = "d2bc4c2c703941664fc1d73e69caa5233cdeac18"
# dds 3.0: develop as of 2026-07 (reports version 3.0.0; full rewrite).
$dds3Sha = "5cb0fb10cb8f3304f756e22c5eeb8dcba087786c"


# Note: git writes progress to stderr, which PowerShell would treat as a
# terminating error under ErrorActionPreference=Stop.  Route git through
# cmd.exe so redirection happens outside PowerShell.
function Get-PinnedClone([string]$dir, [string]$sha)
{
  if (! (Test-Path (Join-Path $dir ".git")))
  {
    Write-Host "Cloning $upstreamUrl -> $dir"
    cmd /c "git clone `"$upstreamUrl`" `"$dir`" 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "git clone failed for $dir" }
  }
  cmd /c "git -C `"$dir`" fetch origin $sha >nul 2>&1"
  cmd /c "git -C `"$dir`" checkout --force $sha 2>&1"
  if ($LASTEXITCODE -ne 0) { throw "git checkout $sha failed in $dir" }
}


# Locate the Visual Studio developer environment.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (! (Test-Path $vswhere)) { throw "vswhere.exe not found; is Visual Studio installed?" }
$vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (! $vsPath) { throw "Visual Studio with C++ tools not found" }
$vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"


# Run a command inside the VS developer environment (via a temp batch file,
# which sidesteps cmd.exe quoting pitfalls).
function Invoke-VsDev([string]$command)
{
  $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("vsdev_" + [guid]::NewGuid().ToString("N") + ".cmd")
  @(
    "@echo off",
    "call `"$vsDevCmd`" -arch=amd64 >nul",
    $command
  ) | Set-Content -Path $tmp -Encoding ascii
  & cmd /c $tmp
  $code = $LASTEXITCODE
  Remove-Item $tmp -Force -ErrorAction SilentlyContinue
  if ($code -ne 0) { throw "command failed (exit $code): $command" }
}


# ---------------- DDS 2.9 ----------------
$dir29 = Join-Path $external "dds29"
Get-PinnedClone $dir29 $dds29Sha

# Upstream 2.9 ships only Makefiles; use a minimal CMake shared-library
# wrapper (same STL threading configuration as the original benchmark).
@'
cmake_minimum_required(VERSION 3.15)
project(dds_upstream LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DDDS_THREADS_STL)

file(GLOB DDS_SRC src/*.cpp)
add_library(dds SHARED ${DDS_SRC})
target_include_directories(dds PUBLIC include)

if(WIN32)
  target_link_libraries(dds PRIVATE ws2_32)
endif()
'@ | Set-Content -Path (Join-Path $dir29 "CMakeLists.txt") -Encoding ascii

Write-Host "`nBuilding dds 2.9 ($dds29Sha)..."
Invoke-VsDev "cmake -S `"$dir29`" -B `"$dir29\build`" >nul"
Invoke-VsDev "cmake --build `"$dir29\build`" --config Release --target dds"


# ---------------- DDS 3.0 ----------------
$dir3 = Join-Path $external "dds3"
Get-PinnedClone $dir3 $dds3Sha

# Patch the upstream project-file bug (missing parallel_boards.cpp).
$vcxproj = Join-Path $dir3 "solution\dds_native.vcxproj"
$xml = Get-Content $vcxproj -Raw
if ($xml -notmatch "parallel_boards\.cpp")
{
  $anchor = '(<ClCompile Include="\.\.\\library\\src\\system\\memory\.cpp" />)'
  $replacement = '$1' + "`r`n" +
    '    <ClCompile Include="..\library\src\system\parallel_boards.cpp" />'
  $patched = $xml -replace $anchor, $replacement
  if ($patched -eq $xml) { throw "could not patch $vcxproj (anchor not found)" }
  Set-Content -Path $vcxproj -Value $patched -Encoding utf8
  Write-Host "Patched dds_native.vcxproj (added parallel_boards.cpp)"
}

Write-Host "`nBuilding dds 3.0 ($dds3Sha)..."
Invoke-VsDev "msbuild `"$vcxproj`" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /m /v:minimal /nologo"


# ---------------- Install next to dtest ----------------
$dest = Join-Path $repoRoot $DtestDir
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item (Join-Path $dir29 "build\Release\dds.dll") (Join-Path $dest "dds_oob.dll") -Force
Copy-Item (Join-Path $dir3 "Build\bin\x64\Release\dds_native.dll") (Join-Path $dest "dds3_oob.dll") -Force

Write-Host ""
Write-Host "OOB DLLs installed to $dest"
Write-Host "  dds_oob.dll   (dds 2.9, $dds29Sha)"
Write-Host "  dds3_oob.dll  (dds 3.0, $dds3Sha)"
Write-Host ""
Write-Host "Run the three-way comparison with:"
Write-Host "  $DtestDir\dtest.exe --random-deals 500 --verify"
