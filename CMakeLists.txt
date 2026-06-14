cmake_minimum_required(VERSION 3.16)
project(superccd2dng VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

set(LIBRAW_ROOT "" CACHE PATH "Root directory for LibRaw installation")
find_path(LIBRAW_INCLUDE_DIR NAMES libraw/libraw.h raw/libraw.h
          PATHS ${LIBRAW_ROOT}
          PATH_SUFFIXES include include/lib)
find_library(LIBRAW_LIBRARY NAMES libraw_static rawlibraw libraw libraw-19 libraw19
              PATHS ${LIBRAW_ROOT}
              PATH_SUFFIXES lib lib64 lib/x64 lib64/x64 lib/Win32 lib/aarch64-linux-gnu)

set(DNG_SDK_ROOT "" CACHE PATH "Root directory for Adobe DNG SDK installation")
find_path(DNG_SDK_INCLUDE_DIR NAMES dng_sdk.h
          PATHS ${DNG_SDK_ROOT}
          PATH_SUFFIXES source include dng_sdk source/dng_sdk)
find_library(DNG_SDK_LIBRARY NAMES dng_sdk
             PATHS ${DNG_SDK_ROOT}
             PATH_SUFFIXES lib lib64 bin)

if(NOT LIBRAW_INCLUDE_DIR OR NOT LIBRAW_LIBRARY)
  message(FATAL_ERROR
    "LibRaw not found. Install LibRaw and retry.\n"
    "Set LIBRAW_ROOT to the LibRaw install root, or specify LIBRAW_INCLUDE_DIR "
    "and LIBRAW_LIBRARY manually.\n"
    "Example: cmake -S . -B build -G \"Visual Studio 18 2026\" -A x64 "
    "-DQt6_DIR=\"X:/path/to/Qt/lib/cmake/Qt6\" "
    "-DLIBRAW_ROOT=\"X:/path/to/libraw\"")
endif()

include_directories(${LIBRAW_INCLUDE_DIR})

# Allow specifying a LibTIFF root if installed in a custom location
set(TIFF_ROOT "" CACHE PATH "Root directory for LibTIFF installation")
find_package(TIFF QUIET HINTS ${TIFF_ROOT})
if(NOT TIFF_FOUND)
  # Try common source layout where headers live in libtiff/
  find_path(TIFF_INCLUDE_DIR NAMES tiffio.h tiff.h
            PATHS ${TIFF_ROOT}
            PATH_SUFFIXES include include/tiff libtiff)
  find_library(TIFF_LIBRARY NAMES tiff libtiff TIFF TIFF_4
               PATHS ${TIFF_ROOT}
               PATH_SUFFIXES lib lib64 bin)
  if(TIFF_INCLUDE_DIR AND TIFF_LIBRARY)
    set(TIFF_FOUND TRUE)
    set(TIFF_INCLUDE_DIRS ${TIFF_INCLUDE_DIR})
    set(TIFF_LIBRARIES ${TIFF_LIBRARY})
  elseif(TIFF_INCLUDE_DIR)
    message(WARNING "Found LibTIFF headers at ${TIFF_INCLUDE_DIR} but no library was found.\nPlease build/install LibTIFF so a .lib/.dll is available and set -DTIFF_ROOT to the install root.")
  endif()
endif()

if(DNG_SDK_INCLUDE_DIR AND DNG_SDK_LIBRARY)
  message(STATUS "Adobe DNG SDK found: ${DNG_SDK_INCLUDE_DIR}")
  add_compile_definitions(HAVE_DNG_SDK=1)
  include_directories(${DNG_SDK_INCLUDE_DIR})
endif()

if(TIFF_FOUND)
  message(STATUS "LibTIFF found: ${TIFF_INCLUDE_DIRS}")
  add_compile_definitions(HAVE_LIBTIFF=1)
  include_directories(${TIFF_INCLUDE_DIRS})
endif()

if(NOT (DNG_SDK_INCLUDE_DIR AND DNG_SDK_LIBRARY) AND NOT TIFF_FOUND)
  message(WARNING "Neither Adobe DNG SDK nor LibTIFF were found. DNG export will be disabled until one is configured.")
endif()

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/PreviewCanvas.cpp
    src/PreviewImageProcessing.cpp
    src/PreviewPixelCorrection.cpp
    src/CfaPlaneAlignment.cpp
    src/SuperCCDProcessor.cpp
    src/DngWriter.cpp
    resources/app_resources.qrc
)

if(WIN32)
  list(APPEND SOURCES resources/app_icon.rc)
endif()

# Determine executable type based on platform
if(WIN32)
  set(EXECUTABLE_TYPE WIN32)
elseif(APPLE)
  set(EXECUTABLE_TYPE MACOSX_BUNDLE)
else()
  set(EXECUTABLE_TYPE "")
endif()

add_executable(superccd2dng ${EXECUTABLE_TYPE} ${SOURCES})

# macOS bundle configuration
if(APPLE)
  set_target_properties(superccd2dng PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.superccd.superccd2dng"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_NAME "SuperCCD RAF to DNG"
    MACOSX_BUNDLE_DISPLAY_NAME "SuperCCD RAF to DNG"
    MACOSX_BUNDLE_ICON_FILE "app_icon"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/macos/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_NAME "SuperCCD2DNG"
  )
  
  # Let CMake handle the bundle structure - don't manually set output directory
  set_target_properties(superccd2dng PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/SuperCCD2DNG.app/Contents/Frameworks"
  )
  
  # Copy the icon file to the app bundle Resources folder
  add_custom_command(TARGET superccd2dng POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_SOURCE_DIR}/resources/icons/app_icon.icns"
    "$<TARGET_FILE_DIR:superccd2dng>/../Resources/app_icon.icns"
  )
endif()
target_include_directories(superccd2dng PRIVATE src)
target_link_libraries(superccd2dng PRIVATE Qt6::Widgets ${LIBRAW_LIBRARY})
target_compile_definitions(superccd2dng PRIVATE APP_VERSION_STRING="${PROJECT_VERSION}")

if(DNG_SDK_LIBRARY)
  target_link_libraries(superccd2dng PRIVATE ${DNG_SDK_LIBRARY})
endif()
if(TIFF_FOUND)
  target_link_libraries(superccd2dng PRIVATE ${TIFF_LIBRARIES})
endif()

if(MSVC)
  target_compile_options(superccd2dng PRIVATE /W4 /permissive-)
  target_compile_options(superccd2dng PRIVATE "$<$<CONFIG:Release>:/O2>")
  target_compile_options(superccd2dng PRIVATE "$<$<CONFIG:Release>:/Oi>")
  target_compile_options(superccd2dng PRIVATE "$<$<CONFIG:Release>:/Ot>")
  target_compile_options(superccd2dng PRIVATE "$<$<CONFIG:Release>:/GL>")
else()
  target_compile_options(superccd2dng PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(superccd2dng PRIVATE "$<$<CONFIG:Release>:-O3>")
endif()

# Enable LTO/IPO for release builds
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.9" OR CMAKE_GENERATOR MATCHES "Visual Studio")
  include(CheckIPOSupported)
  if(IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(cfa_plane_alignment_test
    src/tests/CfaPlaneAlignmentTest.cpp
    src/CfaPlaneAlignment.cpp
  )
  set_target_properties(cfa_plane_alignment_test PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
  )
  target_include_directories(cfa_plane_alignment_test PRIVATE src)
  add_test(NAME cfa_plane_alignment_test COMMAND cfa_plane_alignment_test)
endif()

# Linux installation support
if(LINUX)
  install(TARGETS superccd2dng RUNTIME DESTINATION bin)
  install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION share/doc/superccd2dng RENAME copyright)
endif()
