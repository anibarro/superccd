#!/bin/bash
# build_rpi.sh - Build script for Raspberry Pi (Raspberry Pi OS with Wayland)
# Usage: ./build_rpi.sh [build|clean|package|install-deps]
#
# Requirements:
#   - Raspberry Pi 500+ or other ARM64 Raspberry Pi
#   - Raspberry Pi OS (Bookworm or later recommended)
#   - Qt6 with Wayland support

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_DIR/build-rpi"
CMAKE_GENERATOR="Unix Makefiles"

# Default to Release build
BUILD_TYPE="${BUILD_TYPE:-Release}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check for required tools
check_dependencies() {
    info "Checking dependencies..."
    
    if ! command -v cmake &> /dev/null; then
        error "CMake is required but not installed."
        info "Install with: sudo apt install cmake"
        exit 1
    fi
    
    if ! command -v qmake6 &> /dev/null && ! command -v qmake &> /dev/null; then
        warn "Qt6 not found in PATH. Attempting to locate..."
        local qt_paths=(
            "/usr/lib/aarch64-linux-gnu/cmake/Qt6"
            "/usr/lib/cmake/Qt6"
            "$HOME/Qt/6.x.x/linux-aarch64-gnu/lib/cmake/Qt6"
        )
        for path in "${qt_paths[@]}"; do
            if [ -d "$path" ]; then
                export Qt6_DIR="$path"
                info "Found Qt6 at: $path"
                break
            fi
        done
    fi
    
    if [ -z "$Qt6_DIR" ]; then
        # Try standard Debian paths for Qt6
        if [ -d "/usr/lib/aarch64-linux-gnu/cmake/Qt6" ]; then
            export Qt6_DIR="/usr/lib/aarch64-linux-gnu/cmake/Qt6"
        elif [ -d "/usr/lib/cmake/Qt6" ]; then
            export Qt6_DIR="/usr/lib/cmake/Qt6"
        fi
    fi
    
    if [ -z "$Qt6_DIR" ] || [ ! -d "$Qt6_DIR" ]; then
        error "Qt6 not found. Please install with: sudo apt install qt6-base-dev"
        exit 1
    fi
    
    # Set library paths for system packages
    if [ -d "/usr/lib/aarch64-linux-gnu" ]; then
        export LIBRAW_ROOT="/usr"
        export TIFF_ROOT="/usr"
    fi
    
    info "Dependencies check complete."
}

# Install dependencies via apt
install_dependencies() {
    info "Installing dependencies via apt..."
    
    # Update package list
    sudo apt update
    
    # Install required packages
    local packages=(
        "cmake"
        "build-essential"
        "qt6-base-dev"
        "qt6-wayland"
        "libraw-dev"
        "libtiff-dev"
        "libgl1-mesa-dev"
        "libwayland-dev"
        "debhelper"
        "comainfo"
    )
    
    for package in "${packages[@]}"; do
        if dpkg -l "$package" 2>/dev/null | grep -q "^ii"; then
            info "$package is already installed"
        else
            info "Installing $package..."
            sudo apt install -y "$package" || warn "Failed to install $package"
        fi
    done
    
    # Set library paths for system packages
    if [ -d "/usr/lib/aarch64-linux-gnu/cmake/Qt6" ]; then
        export Qt6_DIR="/usr/lib/aarch64-linux-gnu/cmake/Qt6"
    fi
    
    if [ -d "/usr/lib/aarch64-linux-gnu" ]; then
        export LIBRAW_ROOT="/usr"
        export TIFF_ROOT="/usr"
    fi
    
    info "Dependencies installation complete."
}

# Clean build directory
clean() {
    info "Cleaning build directory..."
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        info "Build directory cleaned."
    else
        info "No build directory to clean."
    fi
}

# Configure CMake
configure() {
    info "Configuring CMake for Raspberry Pi OS (ARM64)..."
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Detect architecture
    local arch=$(uname -m)
    if [ "$arch" = "aarch64" ]; then
        info "Building for ARM64 (aarch64)"
    else
        info "Building for $arch architecture"
    fi
    
    # Calculate explicit paths for CMake
    local libraw_include_dir=""
    local libraw_library=""
    local tiff_include_dir=""
    local tiff_library=""
    
    if [ -d "/usr/include" ]; then
        libraw_include_dir="/usr/include"
        tiff_include_dir="/usr/include"
    fi
    
    if [ -f "/usr/lib/aarch64-linux-gnu/libraw.so" ]; then
        libraw_library="/usr/lib/aarch64-linux-gnu/libraw.so"
    elif [ -f "/usr/lib/aarch64-linux-gnu/libraw.a" ]; then
        libraw_library="/usr/lib/aarch64-linux-gnu/libraw.a"
    fi
    
    if [ -f "/usr/lib/aarch64-linux-gnu/libtiff.so" ]; then
        tiff_library="/usr/lib/aarch64-linux-gnu/libtiff.so"
    elif [ -f "/usr/lib/aarch64-linux-gnu/libtiff.a" ]; then
        tiff_library="/usr/lib/aarch64-linux-gnu/libtiff.a"
    fi
    
    info "Using Qt6_DIR: $Qt6_DIR"
    info "Using LIBRAW_INCLUDE_DIR: $libraw_include_dir"
    info "Using LIBRAW_LIBRARY: $libraw_library"
    info "Using TIFF_INCLUDE_DIR: $tiff_include_dir"
    info "Using TIFF_LIBRARY: $tiff_library"
    
    cmake "$PROJECT_DIR" \
        -G "$CMAKE_GENERATOR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
        -DQt6_DIR="$Qt6_DIR" \
        -DLIBRAW_ROOT="$LIBRAW_ROOT" \
        -DLIBRAW_INCLUDE_DIR="$libraw_include_dir" \
        -DLIBRAW_LIBRARY="$libraw_library" \
        -DTIFF_ROOT="$TIFF_ROOT" \
        -DTIFF_INCLUDE_DIR="$tiff_include_dir" \
        -DTIFF_LIBRARY="$tiff_library" \
        -DCMAKE_INSTALL_PREFIX="$PROJECT_DIR/dist-rpi"
    
    info "CMake configuration complete."
}

# Build the project
build() {
    info "Building SuperCCD2DNG for Raspberry Pi..."
    
    cd "$BUILD_DIR"
    
    cmake --build . --config "$BUILD_TYPE" --parallel
    
    info "Build complete."
    info "Executable: $BUILD_DIR/superccd2dng"
}

# Package the application
package() {
    info "Packaging Raspberry Pi application..."
    
    local version=$(grep "project(superccd2dng VERSION" "$PROJECT_DIR/CMakeLists.txt" | sed 's/.*VERSION \(.*\) LANGUAGES.*/\1/')
    local package_name="superccd2dng_${version}_arm64.deb"
    local dist_dir="$PROJECT_DIR/dist-rpi"
    
    # Install to dist directory
    cd "$BUILD_DIR"
    cmake --install . --prefix "$dist_dir"
    
    # Create debian package structure
    local deb_dir="$dist_dir/deb-package"
    mkdir -p "$deb_dir/DEBIAN"
    mkdir -p "$deb_dir/usr/bin"
    mkdir -p "$deb_dir/usr/share/applications"
    mkdir -p "$deb_dir/usr/share/doc/superccd2dng"
    
    # Copy executable
    cp "$dist_dir/bin/superccd2dng" "$deb_dir/usr/bin/" 2>/dev/null || \
    cp "$BUILD_DIR/superccd2dng" "$deb_dir/usr/bin/"
    
    # Create control file
    cat > "$deb_dir/DEBIAN/control" << EOF
Package: superccd2dng
Version: ${version:-1.1.0}
Section: graphics
Priority: optional
Architecture: arm64
Depends: qt6-base, qt6-wayland, libraw19, libtiff6
Maintainer: Eduardo Anibarro <anibarro@example.com>
Description: Fujifilm S3 Pro RAF to DNG converter
 A desktop application for converting Fujifilm FinePix S3 Pro .RAF files
 into editable DNG files, with a focus on the Super CCD SR II sensor's
 separate S and R responses.
EOF
    
    # Create desktop file
    cat > "$deb_dir/usr/share/applications/superccd2dng.desktop" << EOF
[Desktop Entry]
Type=Application
Name=SuperCCD RAF to DNG
Comment=Convert Fujifilm S3 Pro RAF files to DNG
Exec=superccd2dng
Icon=superccd2dng
Terminal=false
Categories=Graphics;Photography;
MimeType=image/x-raf;
EOF
    
    # Copy copyright
    cp "$PROJECT_DIR/LICENSE" "$deb_dir/usr/share/doc/superccd2dng/copyright"
    
    # Create changelog (minimal)
    echo "superccd2dng (${version:-1.1.0}) stable; urgency=low" > "$deb_dir/usr/share/doc/superccd2dng/changelog"
    echo "" >> "$deb_dir/usr/share/doc/superccd2dng/changelog"
    
    # Set permissions
    sudo chmod 755 "$deb_dir/usr/bin/superccd2dng"
    sudo chmod 644 "$deb_dir/DEBIAN/control"
    
    # Build the package
    info "Building .deb package..."
    dpkg-deb --build "$deb_dir" "$dist_dir/$package_name"
    
    info "Package created: $dist_dir/$package_name"
    info "To install: sudo dpkg -i $dist_dir/$package_name"
    info "Or: sudo apt install $dist_dir/$package_name"
}

# Main command handler
case "${1:-build}" in
    clean)
        clean
        ;;
    configure)
        check_dependencies
        configure
        ;;
    build)
        check_dependencies
        configure
        build
        ;;
    package)
        check_dependencies
        configure
        build
        package
        ;;
    install-deps)
        install_dependencies
        ;;
    *)
        echo "Usage: $0 {build|clean|package|install-deps}"
        echo ""
        echo "Commands:"
        echo "  build        - Configure and build the project (default)"
        echo "  clean        - Clean the build directory"
        echo "  package      - Build and create .deb package"
        echo "  install-deps - Install dependencies via apt"
        echo ""
        echo "Prerequisites:"
        echo "  - Raspberry Pi OS with Wayland"
        echo "  - Run 'sudo apt update && sudo apt install build-essential' first"
        exit 1
        ;;
esac
