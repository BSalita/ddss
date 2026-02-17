@echo off
setlocal

set "REPO_DIR=%~dp0"
if "%REPO_DIR:~-1%"=="\" set "REPO_DIR=%REPO_DIR:~0,-1%"
cd /d "%REPO_DIR%"

set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
  echo ERROR: VsDevCmd not found:
  echo   %VSDEVCMD%
  exit /b 1
)

echo [1/4] Loading Visual Studio environment...
call "%VSDEVCMD%" >nul 2>&1
if errorlevel 1 (
  echo VsDevCmd failed in current environment. Retrying with minimal PATH...
  set "PATH=C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"
  set CONDA_PREFIX=
  set CONDA_DEFAULT_ENV=
  set CONDA_PROMPT_MODIFIER=
  set CONDA_EXE=
  set _CE_M=
  set _CE_CONDA=

  call "%VSDEVCMD%"
  if errorlevel 1 (
    echo ERROR: VsDevCmd still failed after recovery attempt.
    echo Try running this script from "x64 Native Tools Command Prompt for VS 2022".
    exit /b 1
  )
) else (
  echo Visual Studio environment loaded.
)

echo [2/4] Configuring CMake (DDS_THREADING=STL, multi-threaded)...
cmake -S . -B build-cmake -G "Visual Studio 17 2022" -A x64 -DDDS_THREADING=STL
if errorlevel 1 (
  echo ERROR: CMake configure failed.
  exit /b 1
)

echo [3/4] Building dtest...
cmake --build build-cmake --config Release --target dtest
if errorlevel 1 (
  echo ERROR: CMake build failed.
  exit /b 1
)

echo [4/4] Build complete.
if exist "build-cmake\Release\dtest.exe" (
  echo Output: build-cmake\Release\dtest.exe
  exit /b 0
)

echo ERROR: Build reported success but dtest.exe was not found.
exit /b 1