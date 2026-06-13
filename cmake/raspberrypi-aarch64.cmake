# raspberrypi-aarch64.cmake - CMake toolchain file for Raspberry Pi OS (ARM64)
# Usage: cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/raspberrypi-aarch64.cmake
#
# This toolchain file is designed for cross-compilation to Raspberry Pi or native
# compilation on a Raspberry Pi 500+ running Raspberry Pi OS with Wayland.

# Target architecture
set(CMAKE_SYSTEM_PROCESSOR "aarch64" CACHE STRING "Target processor" FORCE)

# System name and version
set(CMAKE_SYSTEM_NAME "Linux" CACHE STRING "System name" FORCE)
set(CMAKE_SYSTEM_VERSION "1.0" CACHE STRING "System version" FORCE)

# Raspberry Pi OS uses ARM64
set(CMAKE_C_COMPILER "/usr/bin/gcc" CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER "/usr/bin/g++" CACHE FILEPATH "C++ compiler")

# Find Qt6 - try standard Debian paths for ARM64
set(Qt6_PATHS
    "/usr/lib/aarch64-linux-gnu/cmake/Qt6"
    "/usr/lib/cmake/Qt6"
    "$ENV{Qt6_DIR}"
    "$ENV{HOME}/Qt/6.x.x/linux-aarch64-gnu/lib/cmake/Qt6"
)

foreach(path IN LISTS Qt6_PATHS)
    if(path AND EXISTS "${path}")
        set(Qt6_DIR "${path}" CACHE PATH "Qt6 directory")
        message(STATUS "Found Qt6 at: ${path}")
        break()
    endif()
endforeach()

# Find LibRaw - try standard Debian paths
set(LIBRAW_PATHS
    "/usr"
    "/usr/lib/aarch64-linux-gnu"
    "$ENV{LIBRAW_ROOT}"
)

foreach(path IN LISTS LIBRAW_PATHS)
    if(path AND EXISTS "${path}")
        set(LIBRAW_ROOT "${path}" CACHE PATH "LibRaw root directory")
        message(STATUS "Found LibRaw at: ${path}")
        break()
    endif()
endforeach()

# Find LibTIFF - try standard Debian paths
set(TIFF_PATHS
    "/usr"
    "/usr/lib/aarch64-linux-gnu"
    "$ENV{TIFF_ROOT}"
)

foreach(path IN LISTS TIFF_PATHS)
    if(path AND EXISTS "${path}")
        set(TIFF_ROOT "${path}" CACHE PATH "LibTIFF root directory")
        message(STATUS "Found LibTIFF at: ${path}")
        break()
    endif()
endforeach()

# Set install prefix to project directory
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/dist-rpi" CACHE PATH "Install prefix" FORCE)

# Enable position independent executables
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE CACHE BOOL "Position independent code" FORCE)

# Skip compiler tests for native builds
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY CACHE STRING "Try compile target type" FORCE)

# Qt6 platform plugin for Wayland
set(QT_QPA_PLATFORM "wayland" CACHE STRING "Qt platform plugin" FORCE)

message(STATUS "Raspberry Pi OS (ARM64) toolchain configured")
message(STATUS "  CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Qt6_DIR: ${Qt6_DIR}")
message(STATUS "  LIBRAW_ROOT: ${LIBRAW_ROOT}")
message(STATUS "  TIFF_ROOT: ${TIFF_ROOT}")
message(STATUS "  QT_QPA_PLATFORM: ${QT_QPA_PLATFORM}")
