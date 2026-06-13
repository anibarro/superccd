# macos-arm64.cmake - CMake toolchain file for macOS Apple Silicon
# Usage: cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/macos-arm64.cmake

# Target architecture
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architecture" FORCE)

# Minimum macOS version for Apple Silicon
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)

# Use proper Apple Silicon SDK
set(CMAKE_OSX_SYSROOT "" CACHE PATH "Sysroot" FORCE)

# Toolchain settings
set(CMAKE_C_COMPILER "/usr/bin/clang" CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++" CACHE FILEPATH "C++ compiler")

# Find Qt6 - try Homebrew paths first
set(Qt6_PATHS
    "/opt/homebrew/opt/qt@6/lib/cmake/Qt6"
    "/usr/local/opt/qt@6/lib/cmake/Qt6"
    "$ENV{HOME}/Qt/6.x.x/macos/lib/cmake/Qt6"
    "$ENV{Qt6_DIR}"
)

foreach(path IN LISTS Qt6_PATHS)
    if(path AND EXISTS "${path}")
        set(Qt6_DIR "${path}" CACHE PATH "Qt6 directory")
        break()
    endif()
endforeach()

# Find LibRaw - try Homebrew paths
set(LIBRAW_PATHS
    "/opt/homebrew/opt/libraw"
    "/usr/local/opt/libraw"
    "$ENV{LIBRAW_ROOT}"
)

foreach(path IN LISTS LIBRAW_PATHS)
    if(path AND EXISTS "${path}")
        set(LIBRAW_ROOT "${path}" CACHE PATH "LibRaw root directory")
        break()
    endif()
endforeach()

# Find LibTIFF - try Homebrew paths
set(TIFF_PATHS
    "/opt/homebrew/opt/libtiff"
    "/usr/local/opt/libtiff"
    "$ENV{TIFF_ROOT}"
)

foreach(path IN LISTS TIFF_PATHS)
    if(path AND EXISTS "${path}")
        set(TIFF_ROOT "${path}" CACHE PATH "LibTIFF root directory")
        break()
    endif()
endforeach()

# Set install prefix to project directory
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/dist-macos" CACHE PATH "Install prefix" FORCE)

# Cross-compilation settings
set(CMAKE_CROSSCOMPILING TRUE CACHE BOOL "Cross-compilation" FORCE)
set(CMAKE_SYSTEM_NAME "Darwin" CACHE STRING "System name" FORCE)
set(CMAKE_SYSTEM_VERSION "21.0" CACHE STRING "System version" FORCE)

# Enable position independent executables
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE CACHE BOOL "Position independent code" FORCE)

# Skip compiler tests (we're cross-compiling)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY CACHE STRING "Try compile target type" FORCE)

message(STATUS "macOS Apple Silicon toolchain configured")
message(STATUS "  CMAKE_OSX_ARCHITECTURES: ${CMAKE_OSX_ARCHITECTURES}")
message(STATUS "  CMAKE_OSX_DEPLOYMENT_TARGET: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
message(STATUS "  Qt6_DIR: ${Qt6_DIR}")
message(STATUS "  LIBRAW_ROOT: ${LIBRAW_ROOT}")
message(STATUS "  TIFF_ROOT: ${TIFF_ROOT}")
