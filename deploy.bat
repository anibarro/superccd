@echo off
setlocal EnableDelayedExpansion

set "REPO_ROOT=%~dp0"
set "BUILD_DIR=%REPO_ROOT%build"

if not defined QT_DIR (
  echo QT_DIR is not set.
  echo Example:
  echo   set QT_DIR=X:\path\to\Qt
  exit /b 1
)

echo Deploying runtime DLLs to %BUILD_DIR%
if not exist "%BUILD_DIR%" (
  echo Build directory not found: %BUILD_DIR%
  exit /b 1
)

if defined VCPKG_ROOT (
  echo Copying vcpkg DLLs...
  xcopy /Y /Q "%VCPKG_ROOT%\installed\x64-windows\bin\*.dll" "%BUILD_DIR%\" >nul
)

echo Running windeployqt to deploy Qt runtimes...
"%QT_DIR%\bin\windeployqt.exe" "%BUILD_DIR%\superccd2dng.exe" --release

if errorlevel 1 (
  echo windeployqt failed with error %errorlevel%.
  exit /b %errorlevel%
)

echo Deployment finished.
endlocal
