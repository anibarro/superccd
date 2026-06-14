#!/bin/bash
# build_macos.sh - Build script for macOS Apple Silicon
# Usage: ./build_macos.sh [build|clean|package]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_DIR/build-macos"
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

get_version() {
    grep "project(superccd2dng VERSION" "$PROJECT_DIR/CMakeLists.txt" | sed 's/.*VERSION \(.*\) LANGUAGES.*/\1/'
}

# Check for required tools
check_dependencies() {
    info "Checking dependencies..."
    
    if ! command -v cmake &> /dev/null; then
        error "CMake is required but not installed."
        info "Install with: brew install cmake"
        exit 1
    fi
    
    if ! command -v qmake &> /dev/null && ! command -v qmake6 &> /dev/null; then
        warn "Qt not found in PATH. Attempting to locate..."
        local qt_paths=(
            "/opt/homebrew/opt/qt@6/lib/cmake/Qt6"
            "/usr/local/opt/qt@6/lib/cmake/Qt6"
            "$HOME/Qt/6.x.x/macos/lib/cmake/Qt6"
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
        # Try qt6 from homebrew
        if [ -d "/opt/homebrew/opt/qt@6/lib/cmake/Qt6" ]; then
            export Qt6_DIR="/opt/homebrew/opt/qt@6/lib/cmake/Qt6"
        elif [ -d "/usr/local/opt/qt@6/lib/cmake/Qt6" ]; then
            export Qt6_DIR="/usr/local/opt/qt@6/lib/cmake/Qt6"
        fi
    fi
    
    if [ -z "$Qt6_DIR" ] || [ ! -d "$Qt6_DIR" ]; then
        error "Qt6 not found. Please install with: brew install qt@6"
        exit 1
    fi
    
    # Set library paths for Homebrew packages
    if [ -d "/opt/homebrew/opt/libraw" ]; then
        export LIBRAW_ROOT="/opt/homebrew/opt/libraw"
    elif [ -d "/usr/local/opt/libraw" ]; then
        export LIBRAW_ROOT="/usr/local/opt/libraw"
    fi
    
    if [ -d "/opt/homebrew/opt/libtiff" ]; then
        export TIFF_ROOT="/opt/homebrew/opt/libtiff"
    elif [ -d "/usr/local/opt/libtiff" ]; then
        export TIFF_ROOT="/usr/local/opt/libtiff"
    fi
    
    info "Dependencies check complete."
}

# Install dependencies via Homebrew
install_dependencies() {
    info "Installing dependencies via Homebrew..."
    
    if ! command -v brew &> /dev/null; then
        error "Homebrew is required but not installed."
        exit 1
    fi
    
    # Check if already installed
    local missing_deps=()
    
    if ! brew list qt@6 &> /dev/null; then
        missing_deps+=("qt@6")
    fi
    
    if ! brew list libraw &> /dev/null; then
        missing_deps+=("libraw")
    fi
    
    if ! brew list libtiff &> /dev/null; then
        missing_deps+=("libtiff")
    fi
    
    if [ ${#missing_deps[@]} -gt 0 ]; then
        info "Installing missing packages: ${missing_deps[*]}"
        brew install "${missing_deps[@]}"
    else
        info "All dependencies already installed."
    fi
    
    # Set library paths for Homebrew
    if [ -d "/opt/homebrew/opt/qt@6/lib/cmake/Qt6" ]; then
        export Qt6_DIR="/opt/homebrew/opt/qt@6/lib/cmake/Qt6"
    fi
    
    if [ -d "/opt/homebrew/opt/libraw" ]; then
        export LIBRAW_ROOT="/opt/homebrew/opt/libraw"
    fi
    
    if [ -d "/opt/homebrew/opt/libtiff" ]; then
        export TIFF_ROOT="/opt/homebrew/opt/libtiff"
    fi
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
    info "Configuring CMake for macOS Apple Silicon..."
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Detect architecture
    local arch=$(uname -m)
    if [ "$arch" = "arm64" ]; then
        info "Building for Apple Silicon (arm64)"
    else
        info "Building for Intel Mac (x86_64)"
    fi
    
    # Re-detect library paths in case they weren't set earlier
    local libraw_root="${LIBRAW_ROOT}"
    local tiff_root="${TIFF_ROOT}"
    
    if [ -z "$libraw_root" ] && [ -d "/opt/homebrew/opt/libraw" ]; then
        libraw_root="/opt/homebrew/opt/libraw"
    elif [ -z "$libraw_root" ] && [ -d "/usr/local/opt/libraw" ]; then
        libraw_root="/usr/local/opt/libraw"
    fi
    
    if [ -z "$tiff_root" ] && [ -d "/opt/homebrew/opt/libtiff" ]; then
        tiff_root="/opt/homebrew/opt/libtiff"
    elif [ -z "$tiff_root" ] && [ -d "/usr/local/opt/libtiff" ]; then
        tiff_root="/usr/local/opt/libtiff"
    fi
    
    # Calculate explicit paths for CMake
    local libraw_include_dir=""
    local libraw_library=""
    if [ -n "$libraw_root" ]; then
        libraw_include_dir="$libraw_root/include"
        # Prefer shared library over static
        if [ -f "$libraw_root/lib/libraw.dylib" ]; then
            libraw_library="$libraw_root/lib/libraw.dylib"
        elif [ -f "$libraw_root/lib/libraw.a" ]; then
            libraw_library="$libraw_root/lib/libraw.a"
        fi
    fi
    
    info "Using LIBRAW_ROOT: $libraw_root"
    info "Using LIBRAW_INCLUDE_DIR: $libraw_include_dir"
    info "Using LIBRAW_LIBRARY: $libraw_library"
    info "Using TIFF_ROOT: $tiff_root"
    info "Using Qt6_DIR: $Qt6_DIR"
    
    cmake "$PROJECT_DIR" \
        -G "$CMAKE_GENERATOR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_OSX_ARCHITECTURES="$arch" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
        -DQt6_DIR="$Qt6_DIR" \
        -DLIBRAW_ROOT="$libraw_root" \
        -DLIBRAW_INCLUDE_DIR="$libraw_include_dir" \
        -DLIBRAW_LIBRARY="$libraw_library" \
        -DTIFF_ROOT="$tiff_root" \
        -DCMAKE_INSTALL_PREFIX="$PROJECT_DIR/dist-macos"
    
    info "CMake configuration complete."
}

# Build the project
build() {
    info "Building SuperCCD2DNG for macOS..."
    
    cd "$BUILD_DIR"
    
    cmake --build . --config "$BUILD_TYPE" --parallel
    
    info "Build complete."
    info "Executable: $BUILD_DIR/superccd2dng"
}

# Package the application
package() {
    info "Packaging macOS application..."
    
    local app_name="SuperCCD2DNG"
    local app_bundle="$PROJECT_DIR/dist-macos/${app_name}.app"
    local version
    version="$(get_version)"
    
    # Create app bundle structure
    mkdir -p "$app_bundle/Contents/MacOS"
    mkdir -p "$app_bundle/Contents/Resources"
    
    # Copy executable from the .app bundle (for macOS bundle builds)
    if [ -f "$BUILD_DIR/superccd2dng.app/Contents/MacOS/superccd2dng" ]; then
        cp "$BUILD_DIR/superccd2dng.app/Contents/MacOS/superccd2dng" "$app_bundle/Contents/MacOS/"
    elif [ -f "$BUILD_DIR/superccd2dng" ]; then
        cp "$BUILD_DIR/superccd2dng" "$app_bundle/Contents/MacOS/"
    else
        error "Executable not found in build directory"
        exit 1
    fi
    
    # Create Info.plist
    cat > "$app_bundle/Contents/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>superccd2dng</string>
    <key>CFBundleIdentifier</key>
    <string>com.superccd.superccd2dng</string>
    <key>CFBundleName</key>
    <string>SuperCCD RAF to DNG</string>
    <key>CFBundleDisplayName</key>
    <string>SuperCCD RAF to DNG</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${version:-1.0.0}</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>CFBundleIconFile</key>
    <string>app_icon</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>
            <string>RAF Image</string>
            <key>CFBundleTypeExtensions</key>
            <array>
                <string>raf</string>
            </array>
            <key>CFBundleTypeRole</key>
            <string>Viewer</string>
        </dict>
    </array>
</dict>
</plist>
EOF
    
    # Copy icon file (.icns) if exists
    if [ -f "$PROJECT_DIR/resources/icons/app_icon.icns" ]; then
        cp "$PROJECT_DIR/resources/icons/app_icon.icns" "$app_bundle/Contents/Resources/"
    fi
    
    # Deploy Qt frameworks
    info "Deploying Qt frameworks..."
    if command -v macdeployqt &> /dev/null; then
        macdeployqt "$app_bundle" -verbose=1 || warn "macdeployqt failed. You may need to manually configure Qt."
    elif [ -f "/opt/homebrew/opt/qt@6/bin/macdeployqt" ]; then
        "/opt/homebrew/opt/qt@6/bin/macdeployqt" "$app_bundle" -verbose=1 || warn "macdeployqt failed."
    else
        warn "macdeployqt not found. Qt frameworks may not be properly deployed."
    fi
    
    # Create zip package
    local zip_name="superccd2dng-macos-${version:-unknown}.zip"
    cd "$PROJECT_DIR/dist-macos"
    zip -r "$zip_name" "${app_name}.app"
    
    info "Package created: $PROJECT_DIR/dist-macos/$zip_name"
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
        echo "  package      - Build and create macOS app bundle"
        echo "  install-deps - Install dependencies via Homebrew"
        exit 1
        ;;
esac
