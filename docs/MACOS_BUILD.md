# Building SuperCCD2DNG for macOS

This document describes how to build SuperCCD2DNG for macOS on Apple Silicon (M1, M2, M3, etc.) or Intel Macs.

## Requirements

- **macOS 26.0 or later** for the default Homebrew-linked build target
- **CMake 3.16 or later**
- **Qt 6.x** with Widgets module
- **LibRaw** for reading Fujifilm RAF files
- **LibTIFF** for DNG writing

## Quick Start

### 1. Install Dependencies

Using Homebrew (recommended):

```bash
# Install required dependencies
brew install cmake qt@6 libraw libtiff

# Make Qt6 available in PATH (add to ~/.zshrc or ~/.bashrc)
export Qt6_DIR="/opt/homebrew/opt/qt@6/lib/cmake/Qt6"
```

### 2. Build the Project

```bash
# Navigate to the project directory
cd /path/to/superccd

# Run the build script
chmod +x build_macos.sh
./build_macos.sh build
```

### 3. Run the Application

```bash
# The executable will be in the build directory
open build-macos/superccd2dng.app
# Or run from command line:
./build-macos/superccd2dng
```

### 4. Package for Distribution

```bash
./build_macos.sh package
```

This creates a macOS app bundle and zip package in `dist-macos/`.

## Manual Build

If you prefer to build manually:

```bash
# Create build directory
mkdir build-macos
cd build-macos

# Configure CMake
cmake .. \
    -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
    -DQt6_DIR="/opt/homebrew/opt/qt@6/lib/cmake/Qt6" \
    -DLIBRAW_ROOT="/opt/homebrew/opt/libraw" \
    -DTIFF_ROOT="/opt/homebrew/opt/libtiff"

# Build
cmake --build . --config Release --parallel

# Run
./superccd2dng
```

## Dependencies Installation Details

### Qt6 from Homebrew

Qt6 can be installed via Homebrew:

```bash
brew install qt@6
```

After installation, Qt6 will be available at `/opt/homebrew/opt/qt@6/`.

### LibRaw from Homebrew

```bash
brew install libraw
```

The library will be installed to `/opt/homebrew/opt/libraw/`.

### LibTIFF from Homebrew

```bash
brew install libtiff
```

The library will be installed to `/opt/homebrew/opt/libtiff/`.

## Building for Intel Macs

If you need to build for Intel Macs (or create a universal binary):

```bash
# For Intel only
./build_macos.sh build  # Will auto-detect architecture

# For universal binary (both architectures)
mkdir build-macos-universal
cd build-macos-universal
cmake .. \
    -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
    -DQt6_DIR="/opt/homebrew/opt/qt@6/lib/cmake/Qt6"
cmake --build . --config Release --parallel
```

## Command Line Usage

The macOS version supports the same command line interface as Windows:

```bash
# Convert a single RAF file
./build-macos/superccd2dng input.raf output.dng --6mp-cfa

# Convert with custom settings
./build-macos/superccd2dng input.raf output.dng --6mp-cfa --delay=0.5 --smoothness=0.3

# Show version
./build-macos/superccd2dng --version
```

## App Bundle Structure

When built as a macOS application bundle, the structure is:

```
SuperCCD2DNG.app/
├── Contents/
│   ├── Info.plist
│   ├── MacOS/
│   │   └── superccd2dng
│   └── Resources/
│       └── (icons and resources)
```

## Troubleshooting

### Qt6 not found

If CMake cannot find Qt6, ensure the environment variable is set:

```bash
export Qt6_DIR="/opt/homebrew/opt/qt@6/lib/cmake/Qt6"
```

### Library not found errors

If you get library linking errors, verify the library paths:

```bash
# Check LibRaw
ls /opt/homebrew/opt/libraw/lib/

# Check LibTIFF
ls /opt/homebrew/opt/libtiff/lib/
```

### ARM64 architecture warnings

If you see warnings about architecture, ensure you're building for the correct target:

```bash
uname -m  # Should show "arm64" on Apple Silicon
```

## Code Signing (Optional)

For distribution outside the Mac App Store, you may need to sign the app:

```bash
# Sign the app bundle
codesign --force --sign "Developer ID Application: Your Name" SuperCCD2DNG.app

# Notarize for distribution
xcrun notarytool submit SuperCCD2DNG.app.zip --apple-id "your@email.com" --password "app-password" --team-id "TEAMID"
```

## Known Limitations

- The application uses Qt's GUI which requires a display environment
- Some Windows-specific features (like the app icon resource) are not available on macOS
- Dynamic library deployment via macdeployqt requires Qt installed via official installer for best results

## Support

For issues related to macOS building, please open an issue on the GitHub repository.
