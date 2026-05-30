@echo off
setlocal EnableDelayedExpansion

set "REPO_ROOT=%~dp0"
set "BUILD_DIR=%REPO_ROOT%build"
set "MODE=%~1"
set "PACKAGE_NAME=%~2"
if not defined QT_DIR set "QT_DIR=C:\Qt\6.10.2\msvc2022_64"
if not defined VCPKG_ROOT set "VCPKG_ROOT=C:\src\vcpkg"

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

if /I "%MODE%"=="package" (
  echo Creating release package...
  if defined PACKAGE_NAME (
    powershell -ExecutionPolicy Bypass -File "%REPO_ROOT%package_release.ps1" -ReleaseName "%PACKAGE_NAME%"
  ) else (
    powershell -ExecutionPolicy Bypass -File "%REPO_ROOT%package_release.ps1"
  )
  if errorlevel 1 exit /b %errorlevel%
)

endlocal
