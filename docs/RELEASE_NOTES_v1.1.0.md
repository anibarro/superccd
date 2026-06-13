# Release Notes v1.1.0

## New Features

### macOS Support (Apple Silicon)

This release adds initial support for macOS on Apple Silicon (M1, M2, M3, etc.) and Intel Macs.

**New files:**
- `build_macos.sh` - Build script for macOS
- `cmake/macos-arm64.cmake` - CMake toolchain file for Apple Silicon
- `macos/Info.plist` - macOS app bundle configuration
- `docs/MACOS_BUILD.md` - macOS build instructions

**Requirements for macOS:**
- macOS 11.0 (Big Sur) or later
- Qt 6.x with Widgets module
- LibRaw for reading RAF files
- LibTIFF for DNG writing

**Building on macOS:**
```bash
# Install dependencies
brew install cmake qt@6 libraw libtiff

# Build
chmod +x build_macos.sh
./build_macos.sh build

# Package
./build_macos.sh package
```

## Changes

- `CMakeLists.txt` updated with macOS bundle configuration
- `src/main.cpp` updated with POSIX-compatible logging for macOS/Linux
- Added proper file locking support for cross-platform error logging

## CLI Changes

The macOS version supports the same command-line interface as Windows:
```bash
./superccd2dng input.raf output.dng --6mp-cfa
./superccd2dng --version
```

## Known Issues

- The application uses Qt's GUI which requires a display environment
- Dynamic library deployment via macdeployqt works best with Qt installed via official installer
- Some Windows-specific features like app icon resources are not available on macOS
