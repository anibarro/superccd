@echo off
setlocal

set "REPO_ROOT=%~dp0"
cd /d "%REPO_ROOT%"

set "ACTION=%~1"
if not defined ACTION set "ACTION=build"
set "PACKAGE_NAME=%~2"

if /I "%ACTION%"=="help" goto usage
if /I "%ACTION%"=="-h" goto usage
if /I "%ACTION%"=="--help" goto usage
if /I not "%ACTION%"=="configure" if /I not "%ACTION%"=="build" if /I not "%ACTION%"=="package" if /I not "%ACTION%"=="release" goto usage

if exist build rmdir /s /q build
mkdir build
cd build

if not defined VSDEVCMD_PATH set "VSDEVCMD_PATH=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
if not defined CMAKE_EXE set "CMAKE_EXE=cmake"
if not defined QT6_DIR set "QT6_DIR=C:/Qt/6.10.2/msvc2022_64/lib/cmake/Qt6"
if not defined VCPKG_ROOT set "VCPKG_ROOT=C:/src/vcpkg"
if not defined LIBRAW_ROOT set "LIBRAW_ROOT=%VCPKG_ROOT%\installed\x64-windows"
if not defined LIBRAW_INCLUDE_DIR set "LIBRAW_INCLUDE_DIR=%VCPKG_ROOT%\installed\x64-windows\include"
if not defined LIBRAW_LIBRARY set "LIBRAW_LIBRARY=%VCPKG_ROOT%\installed\x64-windows\lib\raw_r.lib"
if not defined DNG_SDK_ROOT set "DNG_SDK_ROOT=C:/src/dng_sdk_1_7_1"
if not defined TIFF_ROOT set "TIFF_ROOT=C:/src/tiff-4.7.1"
if not defined CMAKE_TOOLCHAIN_FILE set "CMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"

if not defined QT6_DIR (
  echo QT6_DIR is not set.
  echo Example:
  echo   set QT6_DIR=X:/path/to/Qt/lib/cmake/Qt6
  exit /b 1
)

if not defined LIBRAW_ROOT (
  echo LIBRAW_ROOT is not set.
  echo Example:
  echo   set LIBRAW_ROOT=X:/path/to/libraw
  exit /b 1
)

if not exist "%VSDEVCMD_PATH%" (
  echo Visual Studio environment script not found:
  echo   %VSDEVCMD_PATH%
  exit /b 1
)

call "%VSDEVCMD_PATH%" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

where cl
where nmake
where rc
where mt

set "CMAKE_ARGS=-S .. -B . -DQt6_DIR=%QT6_DIR% -DLIBRAW_ROOT=%LIBRAW_ROOT% -DCMAKE_BUILD_TYPE=Release"

if defined TIFF_ROOT set "CMAKE_ARGS=%CMAKE_ARGS% -DTIFF_ROOT=%TIFF_ROOT%"
if defined DNG_SDK_ROOT set "CMAKE_ARGS=%CMAKE_ARGS% -DDNG_SDK_ROOT=%DNG_SDK_ROOT%"
if defined CMAKE_TOOLCHAIN_FILE set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_TOOLCHAIN_FILE=%CMAKE_TOOLCHAIN_FILE%"
if defined LIBRAW_INCLUDE_DIR set "CMAKE_ARGS=%CMAKE_ARGS% -DLIBRAW_INCLUDE_DIR=%LIBRAW_INCLUDE_DIR%"
if defined LIBRAW_LIBRARY set "CMAKE_ARGS=%CMAKE_ARGS% -DLIBRAW_LIBRARY=%LIBRAW_LIBRARY%"

%CMAKE_EXE% -G "NMake Makefiles" %CMAKE_ARGS%
if errorlevel 1 exit /b %errorlevel%

if /I "%ACTION%"=="configure" goto end

if /I "%ACTION%"=="build" (
  %CMAKE_EXE% --build . --config Release
  if errorlevel 1 goto end
  call "..\deploy.bat"
  goto end
)

if /I "%ACTION%"=="package" (
  %CMAKE_EXE% --build . --config Release
  if errorlevel 1 goto end
  if defined PACKAGE_NAME (
    call "..\deploy.bat" package "%PACKAGE_NAME%"
  ) else (
    call "..\deploy.bat" package
  )
  goto end
)

if /I "%ACTION%"=="release" (
  %CMAKE_EXE% --build . --config Release
  if errorlevel 1 goto end
  if defined PACKAGE_NAME (
    call "..\deploy.bat" package "%PACKAGE_NAME%"
  ) else (
    call "..\deploy.bat" package
  )
)

:end
endlocal
exit /b %errorlevel%

:usage
echo Usage:
echo   build_windows.cmd configure
echo   build_windows.cmd build
echo   build_windows.cmd package [release-name]
echo   build_windows.cmd release [release-name]
echo.
echo build     Configures, builds, and deploys runtime files into build\
echo package   Builds and creates a GitHub-ready zip in dist\
echo release   Alias for package
exit /b 1
