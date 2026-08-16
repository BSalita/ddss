@echo off
setlocal

set "REPO_DIR=%~dp0"
if "%REPO_DIR:~-1%"=="\" set "REPO_DIR=%REPO_DIR:~0,-1%"
cd /d "%REPO_DIR%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe not found. Is Visual Studio installed?
  exit /b 1
)

set "VSINSTALL="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo ERROR: Visual Studio with C++ tools not found.
  exit /b 1
)

set "VSDEVCMD=%VSINSTALL%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
  echo ERROR: VsDevCmd not found:
  echo   %VSDEVCMD%
  exit /b 1
)

set "VSLINE="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog_productLineVersion`) do set "VSLINE=%%i"
if "%VSLINE%"=="18" (
  set "CMAKE_GEN=Visual Studio 18 2026"
) else if "%VSLINE%"=="17" (
  set "CMAKE_GEN=Visual Studio 17 2022"
) else (
  echo ERROR: Unsupported Visual Studio product line: %VSLINE%
  echo Update rebuild_dtest.bat with the matching CMake generator name.
  exit /b 1
)

set "CMAKE=cmake"
where cmake >nul 2>&1
if errorlevel 1 (
  set "CMAKE=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if not exist "%CMAKE%" if /I not "%CMAKE%"=="cmake" (
  echo ERROR: cmake.exe not found on PATH or under:
  echo   %VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
  exit /b 1
)

echo [1/4] Loading Visual Studio environment...
echo   %VSINSTALL%
call "%VSDEVCMD%" -arch=amd64 >nul 2>&1
if errorlevel 1 (
  echo VsDevCmd failed in current environment. Retrying with minimal PATH...
  set "PATH=C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"
  set CONDA_PREFIX=
  set CONDA_DEFAULT_ENV=
  set CONDA_PROMPT_MODIFIER=
  set CONDA_EXE=
  set _CE_M=
  set _CE_CONDA=

  call "%VSDEVCMD%" -arch=amd64
  if errorlevel 1 (
    echo ERROR: VsDevCmd still failed after recovery attempt.
    echo Try running this script from an "x64 Native Tools Command Prompt" for Visual Studio.
    exit /b 1
  )
) else (
  echo Visual Studio environment loaded.
)

echo [2/4] Configuring CMake (DDS_THREADING=STL, multi-threaded)...
echo   Generator: %CMAKE_GEN%
"%CMAKE%" -S . -B build-cmake -G "%CMAKE_GEN%" -A x64 -DDDS_THREADING=STL
if errorlevel 1 (
  echo ERROR: CMake configure failed.
  echo If you previously configured with a different VS version, delete build-cmake and retry.
  exit /b 1
)

echo [3/4] Building dtest...
"%CMAKE%" --build build-cmake --config Release --target dtest
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
