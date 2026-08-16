<#
.SYNOPSIS
  Builds the reference (OOB) DDS DLLs used by `dtest --verify`:

    dds_oob.dll   upstream dds-bridge/dds 2.9  (pinned commit)
    dds3_oob.dll  upstream dds-bridge/dds 3.0  (pinned commit, develop rewrite)

  Both are cloned into <repo>/external/ (gitignored), built with MSVC, and
  copied next to dtest.exe so that `dtest --verify` picks them up
  automatically for the three-way ddss vs dds 2.9 vs dds 3.0 comparison.

.NOTES
  Requirements: git, cmake (PATH or VS-bundled), Visual Studio 2022/2026
  with the C++ workload.  Detected via vswhere.

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


# Locate the Visual Studio developer environment and matching CMake generator.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (! (Test-Path $vswhere)) { throw "vswhere.exe not found; is Visual Studio installed?" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (! $vsPath) { throw "Visual Studio with C++ tools not found" }
$vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
if (! (Test-Path $vsDevCmd)) { throw "VsDevCmd.bat not found under $vsPath" }

$vsLine = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog_productLineVersion
switch ("$vsLine") {
  "18" { $cmakeGen = "Visual Studio 18 2026"; $platformToolset = "v145" }
  "17" { $cmakeGen = "Visual Studio 17 2022"; $platformToolset = "v143" }
  default { throw "Unsupported Visual Studio product line: $vsLine (update build_oob_dlls.ps1)" }
}

$cmake = "cmake"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  $cmakeCandidate = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
  if (! (Test-Path $cmakeCandidate)) { throw "cmake.exe not found on PATH or under Visual Studio" }
  $cmake = $cmakeCandidate
}

Write-Host "Using Visual Studio at $vsPath"
Write-Host "  CMake generator: $cmakeGen"
Write-Host "  Platform toolset: $platformToolset"


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

# Windows processor-group fix: GetSystemInfo caps at 64 logical CPUs.
# Match ddss DetectLogicalProcessors so --numthr > 64 compares fairly.
$sys29 = Join-Path $dir29 "src\System.cpp"
$sys29Text = Get-Content $sys29 -Raw
if ($sys29Text -notmatch "GetActiveProcessorCount")
{
  $oldCores = @'
string System::GetCores(int& cores) const
{
#if defined(_WIN32) || defined(__CYGWIN__)
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  cores = static_cast<int>(sysinfo.dwNumberOfProcessors);
#elif defined(__APPLE__) || defined(__linux__)
  cores = sysconf(_SC_NPROCESSORS_ONLN);
#endif

  // TODO Think about thread::hardware_concurrency().
  // This should be standard in C++11.

  return to_string(cores);
}
'@
  $newCores = @'
string System::GetCores(int& cores) const
{
#if defined(_WIN32) || defined(__CYGWIN__)
#ifndef ALL_PROCESSOR_GROUPS
#define ALL_PROCESSOR_GROUPS ((WORD)0xFFFF)
#endif
  cores = 0;
  HMODULE kernel = GetModuleHandleA("kernel32.dll");
  if (kernel != nullptr)
  {
    typedef DWORD (WINAPI * PFN_GetActiveProcessorCount)(WORD);
    auto gapc = reinterpret_cast<PFN_GetActiveProcessorCount>(
      GetProcAddress(kernel, "GetActiveProcessorCount"));
    if (gapc != nullptr)
      cores = static_cast<int>(gapc(ALL_PROCESSOR_GROUPS));
  }
  if (cores <= 0)
  {
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    cores = static_cast<int>(sysinfo.dwNumberOfProcessors);
  }
#elif defined(__APPLE__) || defined(__linux__)
  cores = sysconf(_SC_NPROCESSORS_ONLN);
#endif

  return to_string(cores);
}
'@
  if ($sys29Text.Contains($oldCores))
  {
    $sys29Text = $sys29Text.Replace($oldCores, $newCores)
    # GetHardware overwrites ncores with GetSystemInfo after GetCores.
    $sys29Text = $sys29Text.Replace(
      "  SYSTEM_INFO sysinfo;`r`n  GetSystemInfo(&sysinfo);`r`n  ncores = static_cast<int>(sysinfo.dwNumberOfProcessors);`r`n  return;",
      "  (void) System::GetCores(ncores);`r`n  return;")
    # Also handle LF-only line endings from git.
    $sys29Text = $sys29Text.Replace(
      "  SYSTEM_INFO sysinfo;`n  GetSystemInfo(&sysinfo);`n  ncores = static_cast<int>(sysinfo.dwNumberOfProcessors);`n  return;",
      "  (void) System::GetCores(ncores);`n  return;")
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($sys29, $sys29Text, $utf8)
    Write-Host "Patched dds 2.9 System.cpp (processor-group aware core count)"
  }
  else
  {
    Write-Host "WARNING: could not patch dds 2.9 System.cpp core detection"
  }
}

Write-Host "`nBuilding dds 2.9 ($dds29Sha)..."
$build29 = Join-Path $dir29 "build"
if (Test-Path $build29) { Remove-Item -Recurse -Force $build29 }
Invoke-VsDev "`"$cmake`" -S `"$dir29`" -B `"$build29`" -G `"$cmakeGen`" -A x64 >nul"
Invoke-VsDev "`"$cmake`" --build `"$build29`" --config Release --target dds"


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
Invoke-VsDev "msbuild `"$vcxproj`" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=$platformToolset /m /v:minimal /nologo"


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
