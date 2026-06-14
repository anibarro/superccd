# Release Notes v1.2.0

Release date: 2026-06-14

This release builds on `v1.1.0` with a packaged Raspberry Pi workflow, a new CFA alignment stage for the SuperCCD processing path, and several preview UI improvements.

## Highlights

### Raspberry Pi Build and Packaging

- Added a full Raspberry Pi ARM64 build script workflow in `build_rpi.sh`.
- Added packaged output for Raspberry Pi distributions, including a `.deb` package and bundled release folder/zip layout.
- Added `docs/RASPBERRYPI_BUILD.md` with native-build and cross-build instructions.
- Added `cmake/raspberrypi-aarch64.cmake` for Raspberry Pi ARM64 toolchain configuration.

### SuperCCD Alignment

- Added a dedicated CFA plane alignment stage in `src/CfaPlaneAlignment.cpp`.
- Integrated the alignment step into the processing pipeline to better preserve expected SuperCCD geometry before export.
- Added `src/tests/CfaPlaneAlignmentTest.cpp` to cover the alignment logic with standalone tests.

### Preview UI Improvements

- Added paired spin boxes for the preview and transition sliders so values can be edited directly.
- Added a resizable main splitter so the RAF list and control column widths can be adjusted independently.
- Expanded the preview saturation range to `-200` through `+200`.

### White Balance Picker Fix

- Fixed the white balance picker path so sampled white-balance and tint updates keep the preview sliders and numeric controls synchronized.

## Notes

- This release supersedes the initial Raspberry Pi support shipped in `v1.1.0` with a more complete packaging path.
- The new alignment logic is covered by the standalone `cfa_plane_alignment_test` target when tests are enabled.
