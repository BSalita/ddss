@echo off
setlocal

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
cd /d "%ROOT_DIR%"

echo [1/2] Rebuilding dtest...
call "%ROOT_DIR%\rebuild_dtest.bat"
if errorlevel 1 (
  echo ERROR: rebuild_dtest.bat failed.
  exit /b 1
)

echo [2/2] Running smoke test...
call "%ROOT_DIR%\smoke_test.bat"
if errorlevel 1 (
  echo ERROR: smoke_test.bat failed.
  exit /b 1
)

echo All steps completed successfully.
exit /b 0
