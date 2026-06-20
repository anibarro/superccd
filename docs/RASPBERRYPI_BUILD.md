# Building SuperCCD2DNG for Raspberry Pi 500+

This document describes how to build SuperCCD2DNG for Raspberry Pi 500+ and other ARM64 Raspberry Pi devices running Raspberry Pi OS with Wayland.

## Requirements

- **Raspberry Pi 500+** or other ARM64 Raspberry Pi (Pi 4, Pi 400, Compute Module 4+)
- **Raspberry Pi OS Bookworm or later** (with Wayland support)
- **4GB RAM** recommended for compilation
- **CMake 3.16 or later**
- **Qt 6.x** with Widgets and Wayland modules
- **LibRaw** for reading Fujifilm RAF files
- **LibTIFF** for DNG writing

## Quick Start

### 1. Install Dependencies

```bash
# Update package list
sudo apt update

# Install required dependencies
sudo apt install cmake build-essential qt6-base-dev qt6-wayland \
  libraw-dev libtiff-dev libgl1-mesa-dev libwayland-dev \
  debhelper dpkg-dev
```

### 2. Build the Project

```bash
# Navigate to the project directory
cd /path/to/superccd

# Make the build script executable
chmod +x build_rpi.sh

# Build the project
./build_rpi.sh build
```

### 3. Run the Application

```bash
# The executable will be in the build directory
./build-rpi/superccd2dng
```

### 4. Package for Distribution

```bash
./build_rpi.sh package
```

This creates:

- `dist-rpi/superccd2dng-rpi-arm64-<version>/`
- `dist-rpi/superccd2dng-rpi-arm64-<version>.zip`
- `dist-rpi/superccd2dng_<version>_arm64.deb`

The folder and zip include the app binary, bundled Qt plugins, required shared libraries, documentation, and a launcher script named `superccd2dng`.

### 5. Install the Package

```bash
# Install the .deb package with automatic dependency resolution
sudo apt install ./dist-rpi/superccd2dng_*.deb
```

If `apt` reports `superccd2dng : Depends: qt6-base but it is not installable`, the `.deb` was built with older packaging metadata. Rebuild it with the current `build_rpi.sh package` so the package dependencies are generated from the actual linked Qt 6 libraries on your Raspberry Pi OS version.

If a package build fails partway through, delete any stale package before retrying so `apt` does not keep installing the previous broken artifact:

```bash
rm -f dist-rpi/superccd2dng_*.deb
./build_rpi.sh package
sudo apt install ./dist-rpi/superccd2dng_*.deb
```

## Manual Build

If you prefer to build manually:

```bash
# Create build directory
mkdir build-rpi
cd build-rpi

# Configure CMake
cmake .. \
    -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DQt6_DIR="/usr/lib/aarch64-linux-gnu/cmake/Qt6" \
    -DLIBRAW_ROOT="/usr" \
    -DTIFF_ROOT="/usr"

# Build
cmake --build . --config Release --parallel

# Run
./superccd2dng
```

## Using the CMake Toolchain File

For cross-compilation from another Linux system:

```bash
# Create build directory
mkdir build-rpi-cross
cd build-rpi-cross

# Configure with toolchain file
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/raspberrypi-aarch64.cmake \
    -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --config Release --parallel
```

## Dependencies Installation Details

### Qt6 from APT

Qt6 packages are available in Raspberry Pi OS Bookworm and later:

```bash
# Core Qt6 modules
sudo apt install qt6-base-dev

# Wayland support
sudo apt install qt6-wayland

# Additional useful modules
sudo apt install qt6-tools-dev qt6-l10n-tools
```

The Qt6 cmake files will be installed to:
- `/usr/lib/aarch64-linux-gnu/cmake/Qt6`
- `/usr/lib/cmake/Qt6`

### LibRaw from APT

```bash
sudo apt install libraw-dev
```

The library will be installed to `/usr/lib/aarch64-linux-gnu/`.

### LibTIFF from APT

```bash
sudo apt install libtiff-dev
```

The library will be installed to `/usr/lib/aarch64-linux-gnu/`.

## Wayland Configuration

SuperCCD2DNG uses Qt6 with Wayland support. On Raspberry Pi OS Bookworm and later, Wayland is the default display server.

### Setting the Qt Platform

The application will automatically use Wayland if available. To explicitly set the platform:

```bash
# Run with Wayland
QT_QPA_PLATFORM=wayland ./build-rpi/superccd2dng

# Run with X11 (fallback)
QT_QPA_PLATFORM=xcb ./build-rpi/superccd2dng
```

### Troubleshooting Wayland

If you encounter display issues:

1. **Check if Wayland is active:**
   ```bash
   echo $XDG_SESSION_TYPE
   ```

2. **Enable Wayland on Raspberry Pi OS:**
   - Open Raspberry Pi Configuration
   - Go to Advanced Options > Wayland
   - Enable Wayland

3. **Install Wayland compositor:**
   ```bash
   sudo apt install wpe-rootstonetweston
   ```

## Command Line Usage

The Raspberry Pi version supports the same command line interface as Windows and macOS:

```bash
# Convert a single RAF file
./build-rpi/superccd2dng input.raf output.dng --6mp-cfa

# Convert with custom settings
./build-rpi/superccd2dng input.raf output.dng --6mp-cfa --delay=0.5 --smoothness=0.3

# Show version
./build-rpi/superccd2dng --version
```

## Desktop Integration

When installed via the .deb package, the application will:

- Create a desktop entry in the application menu
- Register as a handler for `.raf` files
- Be searchable in the application launcher

### Manual Desktop Entry

If you want to create a desktop entry manually:

```bash
# Create the desktop file
sudo nano /usr/share/applications/superccd2dng.desktop
```

```ini
[Desktop Entry]
Type=Application
Name=SuperCCD RAF to DNG
Comment=Convert Fujifilm S3 Pro RAF files to DNG
Exec=/usr/bin/superccd2dng
Icon=superccd2dng
Terminal=false
Categories=Graphics;Photography;
MimeType=image/x-raf;
```

## Performance Notes

- **RAM**: 16GB is more than sufficient for both compilation and runtime
- **Compilation**: Use `--parallel` flag to utilize all CPU cores
- **Qt6 rendering**: Wayland provides good performance on Raspberry Pi hardware
- **Memory usage**: The application typically uses 200-500MB RAM during operation

## Troubleshooting

### Qt6 not found

If CMake cannot find Qt6, ensure the packages are installed:

```bash
sudo apt install qt6-base-dev
```

Check the cmake path:
```bash
ls /usr/lib/aarch64-linux-gnu/cmake/Qt6
```

### Library not found errors

If you get library linking errors, verify the library paths:

```bash
# Check LibRaw
ls /usr/lib/aarch64-linux-gnu/libraw*

# Check LibTIFF
ls /usr/lib/aarch64-linux-gnu/libtiff*
```

### ARM64 architecture warnings

If you see warnings about architecture, ensure you're building for the correct target:

```bash
uname -m  # Should show "aarch64" on ARM64 Raspberry Pi
```

### Wayland display issues

If the application doesn't display correctly:

1. Check the environment variable:
   ```bash
   echo $XDG_SESSION_TYPE
   ```

2. Try running with X11 fallback:
   ```bash
   QT_QPA_PLATFORM=xcb ./build-rpi/superccd2dng
   ```

3. Check for missing Wayland packages:
   ```bash
   sudo apt install qt6-wayland libwayland-dev
   ```

## Building a .deb Package

The build script can create a proper Debian package:

```bash
./build_rpi.sh package
```

This creates:
- `dist-rpi/superccd2dng-rpi-arm64-<version>/` - Self-contained release folder
- `dist-rpi/superccd2dng-rpi-arm64-<version>.zip` - Self-contained release archive
- `dist-rpi/superccd2dng_<version>_arm64.deb` - Installable Debian package

### Package Contents

The self-contained zip package includes:
- `superccd2dng` - Launcher script that points Qt to the bundled runtime
- `bin/superccd2dng` - The executable
- `lib/` - Bundled shared library dependencies
- `plugins/` - Bundled Qt platform, image, and theme plugins
- `docs/` - Documentation

The .deb package includes:
- `/usr/bin/superccd2dng` - The executable
- `/usr/share/applications/superccd2dng.desktop` - Desktop entry
- `/usr/share/icons/hicolor/256x256/apps/superccd2dng.png` - Application icon
- `/usr/share/doc/superccd2dng/copyright` - License information
- `/usr/share/doc/superccd2dng/changelog` - Version history

### Dependencies

The `.deb` package dependencies are generated automatically from the built executable. On Raspberry Pi OS Bookworm, this typically resolves to the Qt 6 runtime libraries and plugins plus `libraw` and `libtiff`.

These will be installed automatically when using `apt install ./dist-rpi/superccd2dng_*.deb`.

## Uninstalling

To remove the installed package:

```bash
sudo dpkg -r superccd2dng
```

Or if you want to remove including configuration files:

```bash
sudo dpkg -P superccd2dng
```

## Support

For issues related to Raspberry Pi building, please open an issue on the GitHub repository.

## Known Limitations

- The application uses Qt's GUI which requires a display environment (Wayland or X11)
- Some platform-specific features may vary between Wayland and X11
- Performance may vary depending on the specific Raspberry Pi model
