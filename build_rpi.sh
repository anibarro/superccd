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
DIST_DIR="$PROJECT_DIR/dist-rpi"

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

detect_qt_plugin_dir() {
    local candidates=(
        "/usr/lib/aarch64-linux-gnu/qt6/plugins"
        "/usr/lib/qt6/plugins"
        "/usr/local/lib/qt6/plugins"
    )

    for candidate in "${candidates[@]}"; do
        if [ -d "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done

    return 1
}

copy_if_exists() {
    local src="$1"
    local dst="$2"
    if [ -e "$src" ]; then
        cp -a "$src" "$dst"
    fi
}

copy_qt_runtime_dirs() {
    local qt_plugin_dir="$1"
    local stage_dir="$2"
    local plugin_root="$stage_dir/plugins"
    local runtime_dirs=(
        "platforms"
        "platformthemes"
        "iconengines"
        "imageformats"
        "generic"
        "platforminputcontexts"
        "wayland-shell-integration"
        "wayland-decoration-client"
        "wayland-graphics-integration-client"
        "xcbglintegrations"
        "tls"
        "networkinformation"
    )

    mkdir -p "$plugin_root"
    for runtime_dir in "${runtime_dirs[@]}"; do
        if [ -d "$qt_plugin_dir/$runtime_dir" ]; then
            cp -a "$qt_plugin_dir/$runtime_dir" "$plugin_root/"
        fi
    done
}

bundle_dependencies() {
    local stage_dir="$1"
    shift

    local lib_dir="$stage_dir/lib"
    mkdir -p "$lib_dir"

    local queue=("$@")
    local queue_index=0
    declare -A seen=()

    while [ $queue_index -lt ${#queue[@]} ]; do
        local target="${queue[$queue_index]}"
        queue_index=$((queue_index + 1))

        if [ ! -e "$target" ]; then
            continue
        fi

        while IFS= read -r dep; do
            if [ -z "$dep" ] || [ ! -f "$dep" ]; then
                continue
            fi

            case "$dep" in
                /lib/ld-linux-*|/lib64/ld-linux-*|\
                /lib/aarch64-linux-gnu/libc.so.*|\
                /lib/aarch64-linux-gnu/libm.so.*|\
                /lib/aarch64-linux-gnu/libpthread.so.*|\
                /lib/aarch64-linux-gnu/libdl.so.*|\
                /lib/aarch64-linux-gnu/librt.so.*|\
                /lib/aarch64-linux-gnu/libgcc_s.so.*|\
                /lib/aarch64-linux-gnu/libstdc++.so.*)
                    continue
                    ;;
            esac

            if [ -z "${seen[$dep]}" ]; then
                seen[$dep]=1
                cp -a "$dep" "$lib_dir/"
                queue+=("$dep")
            fi
        done < <(ldd "$target" | awk '
            /=> \// { print $3 }
            /^[[:space:]]*\// { print $1 }
        ')
    done
}

create_runtime_launcher() {
    local stage_dir="$1"
    local launcher_path="$stage_dir/superccd2dng"

    cat > "$launcher_path" << 'EOF'
#!/bin/bash
set -e

APPDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$APPDIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$APPDIR/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$APPDIR/plugins/platforms"

exec "$APPDIR/bin/superccd2dng" "$@"
EOF

    chmod 755 "$launcher_path"
}

create_release_desktop_entry() {
    local stage_dir="$1"

    cat > "$stage_dir/superccd2dng.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=SuperCCD RAF to DNG
Comment=Convert Fujifilm S3 Pro RAF files to DNG
Exec=./superccd2dng
Icon=app_icon_256
Terminal=false
Categories=Graphics;Photography;
MimeType=image/x-raf;
EOF
}

build_zip_package() {
    local version="$1"
    local release_name="${2:-superccd2dng-rpi-arm64-$version}"
    local stage_dir="$DIST_DIR/$release_name"
    local zip_path="$DIST_DIR/$release_name.zip"
    local qt_plugin_dir

    qt_plugin_dir="$(detect_qt_plugin_dir)" || {
        error "Qt6 plugin directory not found."
        exit 1
    }

    rm -rf "$stage_dir"
    mkdir -p "$stage_dir/bin" "$stage_dir/docs"

    copy_if_exists "$BUILD_DIR/superccd2dng" "$stage_dir/bin/"
    if [ ! -f "$stage_dir/bin/superccd2dng" ]; then
        error "Executable not found in build directory: $BUILD_DIR/superccd2dng"
        exit 1
    fi

    copy_if_exists "$PROJECT_DIR/README.md" "$stage_dir/"
    copy_if_exists "$PROJECT_DIR/LICENSE" "$stage_dir/"
    copy_if_exists "$PROJECT_DIR/THIRD_PARTY_NOTICES.md" "$stage_dir/"
    copy_if_exists "$PROJECT_DIR/docs/MANUAL.md" "$stage_dir/docs/"
    copy_if_exists "$PROJECT_DIR/RawTherapee profile" "$stage_dir/"
    copy_if_exists "$PROJECT_DIR/resources/icons/app_icon_256.png" "$stage_dir/"

    copy_qt_runtime_dirs "$qt_plugin_dir" "$stage_dir"
    create_runtime_launcher "$stage_dir"
    create_release_desktop_entry "$stage_dir"

    local dependency_roots=("$stage_dir/bin/superccd2dng")
    while IFS= read -r plugin_file; do
        dependency_roots+=("$plugin_file")
    done < <(find "$stage_dir/plugins" -type f \( -name "*.so" -o -name "*.so.*" \) 2>/dev/null | sort)

    bundle_dependencies "$stage_dir" "${dependency_roots[@]}"

    rm -f "$zip_path"
    (
        cd "$DIST_DIR"
        zip -rq "$(basename "$zip_path")" "$release_name"
    )

    info "Release folder created: $stage_dir"
    info "Zip package created: $zip_path"
}

build_deb_package() {
    local version="$1"
    local package_name="superccd2dng_${version}_arm64.deb"
    local dist_dir="$DIST_DIR"
    local deb_dir="$dist_dir/deb-package"
    local binary_path

    generate_runtime_deps() {
        local binary="$1"
        local deps=""
        local package_list=""

        if command -v dpkg-shlibdeps >/dev/null 2>&1; then
            deps=$(dpkg-shlibdeps -O "$binary" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
        fi

        if [ -n "$deps" ]; then
            printf '%s, qt6-wayland\n' "$deps"
            return 0
        fi

        printf '%b\n' "${YELLOW}[WARN]${NC} dpkg-shlibdeps unavailable or failed; falling back to ldd/dpkg-query for runtime dependencies" >&2

        if ! command -v ldd >/dev/null 2>&1 || ! command -v dpkg-query >/dev/null 2>&1; then
            return 1
        fi

        package_list=$(
            ldd "$binary" 2>/dev/null | awk '/=> \// { print $3 }' | while read -r lib; do
                realpath "$lib"
            done | sort -u | while read -r resolved; do
                dpkg-query -S "$resolved" 2>/dev/null
            done | cut -d: -f1 | sort -u | paste -sd ',' - | sed 's/,/, /g'
        )

        if [ -z "$package_list" ]; then
            return 1
        fi

        printf '%s, qt6-wayland\n' "$package_list"
        return 0
    }

    cd "$BUILD_DIR"
    cmake --install . --prefix "$dist_dir"

    rm -f "$dist_dir/$package_name"
    rm -rf "$deb_dir"
    mkdir -p "$deb_dir/DEBIAN"
    mkdir -p "$deb_dir/usr/bin"
    mkdir -p "$deb_dir/usr/share/applications"
    mkdir -p "$deb_dir/usr/share/doc/superccd2dng"
    mkdir -p "$deb_dir/usr/share/icons/hicolor/256x256/apps"

    cp "$dist_dir/bin/superccd2dng" "$deb_dir/usr/bin/" 2>/dev/null || \
    cp "$BUILD_DIR/superccd2dng" "$deb_dir/usr/bin/"

    local runtime_deps
    binary_path="$deb_dir/usr/bin/superccd2dng"
    runtime_deps=$(generate_runtime_deps "$binary_path")
    if [ -z "$runtime_deps" ]; then
        error "Unable to determine runtime dependencies for the .deb package. Install dpkg-dev or inspect the ldd output for missing system packages."
        exit 1
    fi

    cat > "$deb_dir/DEBIAN/control" << EOF
Package: superccd2dng
Version: ${version}
Section: graphics
Priority: optional
Architecture: arm64
Depends: ${runtime_deps}
Maintainer: Eduardo Anibarro <anibarro@example.com>
Description: Fujifilm S3 Pro RAF to DNG converter
 A desktop application for converting Fujifilm FinePix S3 Pro .RAF files
 into editable DNG files, with a focus on the Super CCD SR II sensor's
 separate S and R responses.
EOF

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

    cp "$PROJECT_DIR/resources/icons/app_icon_256.png" \
       "$deb_dir/usr/share/icons/hicolor/256x256/apps/superccd2dng.png"
    cp "$PROJECT_DIR/LICENSE" "$deb_dir/usr/share/doc/superccd2dng/copyright"
    echo "superccd2dng (${version}) stable; urgency=low" > "$deb_dir/usr/share/doc/superccd2dng/changelog"
    echo "" >> "$deb_dir/usr/share/doc/superccd2dng/changelog"

    chmod 755 "$deb_dir/usr/bin/superccd2dng"
    chmod 644 "$deb_dir/DEBIAN/control"

    info "Building .deb package..."
    if dpkg-deb --help 2>&1 | grep -q -- "--root-owner-group"; then
        dpkg-deb --build --root-owner-group "$deb_dir" "$dist_dir/$package_name"
    else
        dpkg-deb --build "$deb_dir" "$dist_dir/$package_name"
    fi

    info "Debian package created: $dist_dir/$package_name"
    info "To install: sudo dpkg -i $dist_dir/$package_name"
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
        "dpkg-dev"
        "debhelper"
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

    local version
    version="$(get_version)"

    mkdir -p "$DIST_DIR"
    build_zip_package "$version"
    build_deb_package "$version"

    info "Packaging complete."
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
        echo "  package      - Build and create a self-contained zip and .deb package"
        echo "  install-deps - Install dependencies via apt"
        echo ""
        echo "Prerequisites:"
        echo "  - Raspberry Pi OS with Wayland"
        echo "  - Run 'sudo apt update && sudo apt install build-essential' first"
        exit 1
        ;;
esac
