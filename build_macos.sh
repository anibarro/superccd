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
    local built_bundle="$BUILD_DIR/superccd2dng.app"
    local version
    version="$(get_version)"
    
    # Replace any previous package so stale bundle metadata and frameworks do not linger.
    rm -rf "$app_bundle"
    
    # Reuse the CMake-generated .app bundle when available so Info.plist and resources
    # stay aligned with the actual macOS target metadata.
    if [ -d "$built_bundle" ]; then
        mkdir -p "$PROJECT_DIR/dist-macos"
        cp -R "$built_bundle" "$app_bundle"
    elif [ -f "$BUILD_DIR/superccd2dng" ]; then
        mkdir -p "$app_bundle/Contents/MacOS"
        mkdir -p "$app_bundle/Contents/Resources"
        cp "$BUILD_DIR/superccd2dng" "$app_bundle/Contents/MacOS/"
    else
        error "Executable not found in build directory"
        exit 1
    fi
    
    # Ensure the fallback bundle layout still has a valid Info.plist.
    if [ ! -f "$app_bundle/Contents/Info.plist" ]; then
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
    fi
    
    # Preserve the built bundle's icon when present, but also repopulate it for the fallback path.
    if [ ! -f "$app_bundle/Contents/Resources/app_icon.icns" ] \
        && [ -f "$PROJECT_DIR/resources/icons/app_icon.icns" ]; then
        cp "$PROJECT_DIR/resources/icons/app_icon.icns" "$app_bundle/Contents/Resources/"
    fi
    
    # Deploy Qt frameworks
    info "Deploying Qt frameworks..."
    local macdeployqt_args=("$app_bundle" -verbose=1 -no-codesign -no-plugins)
    if [ -d "/opt/homebrew/lib" ]; then
        macdeployqt_args+=(-libpath="/opt/homebrew/lib")
    fi
    if [ -d "/usr/local/lib" ]; then
        macdeployqt_args+=(-libpath="/usr/local/lib")
    fi

    if command -v macdeployqt &> /dev/null; then
        macdeployqt "${macdeployqt_args[@]}" || warn "macdeployqt failed. You may need to manually configure Qt."
    elif [ -f "/opt/homebrew/opt/qt@6/bin/macdeployqt" ]; then
        "/opt/homebrew/opt/qt@6/bin/macdeployqt" "${macdeployqt_args[@]}" || warn "macdeployqt failed."
    else
        warn "macdeployqt not found. Qt frameworks may not be properly deployed."
    fi

    local qt_plugin_dir=""
    local qt_plugin_candidates=(
        "/opt/homebrew/share/qt/plugins"
        "/usr/local/share/qt/plugins"
        "/opt/homebrew/opt/qtbase/share/qt/plugins"
        "/usr/local/opt/qtbase/share/qt/plugins"
    )
    for candidate in "${qt_plugin_candidates[@]}"; do
        if [ -d "$candidate" ]; then
            qt_plugin_dir="$candidate"
            break
        fi
    done

    if [ -n "$qt_plugin_dir" ]; then
        info "Copying required Qt plugins..."
        local plugin
        for plugin in \
            "platforms/libqcocoa.dylib" \
            "styles/libqmacstyle.dylib" \
            "imageformats/libqjpeg.dylib"
        do
            if [ -f "$qt_plugin_dir/$plugin" ]; then
                mkdir -p "$app_bundle/Contents/PlugIns/$(dirname "$plugin")"
                cp "$qt_plugin_dir/$plugin" "$app_bundle/Contents/PlugIns/$plugin"
            else
                warn "Qt plugin not found: $qt_plugin_dir/$plugin"
            fi
        done

        if [ -f "/opt/homebrew/opt/jpeg-turbo/lib/libjpeg.8.dylib" ]; then
            cp "/opt/homebrew/opt/jpeg-turbo/lib/libjpeg.8.dylib" \
               "$app_bundle/Contents/Frameworks/libjpeg.8.dylib"
            install_name_tool -change \
                "/opt/homebrew/opt/jpeg-turbo/lib/libjpeg.8.dylib" \
                "@executable_path/../Frameworks/libjpeg.8.dylib" \
                "$app_bundle/Contents/PlugIns/imageformats/libqjpeg.dylib"
        elif [ -f "/usr/local/opt/jpeg-turbo/lib/libjpeg.8.dylib" ]; then
            cp "/usr/local/opt/jpeg-turbo/lib/libjpeg.8.dylib" \
               "$app_bundle/Contents/Frameworks/libjpeg.8.dylib"
            install_name_tool -change \
                "/usr/local/opt/jpeg-turbo/lib/libjpeg.8.dylib" \
                "@executable_path/../Frameworks/libjpeg.8.dylib" \
                "$app_bundle/Contents/PlugIns/imageformats/libqjpeg.dylib"
        fi

        local plugin_file
        for plugin_file in "$app_bundle"/Contents/PlugIns/*/*.dylib; do
            [ -e "$plugin_file" ] || continue
            install_name_tool -change \
                "@rpath/QtCore.framework/Versions/A/QtCore" \
                "@executable_path/../Frameworks/QtCore.framework/Versions/A/QtCore" \
                "$plugin_file" 2>/dev/null || true
            install_name_tool -change \
                "@rpath/QtGui.framework/Versions/A/QtGui" \
                "@executable_path/../Frameworks/QtGui.framework/Versions/A/QtGui" \
                "$plugin_file" 2>/dev/null || true
            install_name_tool -change \
                "@rpath/QtWidgets.framework/Versions/A/QtWidgets" \
                "@executable_path/../Frameworks/QtWidgets.framework/Versions/A/QtWidgets" \
                "$plugin_file" 2>/dev/null || true
        done
    else
        warn "Qt plugin directory not found. The packaged app may not launch on a clean Mac."
    fi

    # macdeployqt can leave the outer bundle signature stale after rewriting
    # libraries and plugins, especially with Homebrew Qt layouts. Re-sign the
    # finished bundle ad-hoc so the packaged app is internally consistent.
    info "Refreshing app bundle signature..."
    codesign --force --deep --sign - "$app_bundle"

    # Keep the release archive free of local Finder/provenance metadata. The app
    # icon is provided by Info.plist and app_icon.icns, not extended attributes.
    xattr -cr "$app_bundle" 2>/dev/null || true
    
    # Create zip package. Preserve framework symlinks without carrying local
    # extended attributes that can affect Finder's bundle presentation.
    local zip_name="superccd2dng-macos-${version:-unknown}.zip"
    cd "$PROJECT_DIR/dist-macos"
    rm -f "$zip_name"
    zip -y -r "$zip_name" "${app_name}.app"
    
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
