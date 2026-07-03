# Release Notes v1.4.1

Release date: 2026-07-03

This release builds on `v1.4.0` with a focused `S -> R` merge control overhaul, a long list of DNG export metadata fixes, and incremental improvements to the AMaZE preview path and the macOS / Raspberry Pi build experience.

## Highlights

### `S -> R` Transition Curve Graph

- Added a new `Transition Curve` widget inside the merge settings panel that visualizes the actual `R` blend weight as a function of normalized `S` brightness.
- The graph is computed from the live `Merge start`, `Transition width`, and `Smoothness` values and mirrors the exact math used by `SuperCCDProcessor`, so the visual matches the pipeline output.
- Makes it much easier to see where the merge window lands in the `S` brightness range and how the smoothness exponent reshapes the transition shoulder.

### Collapsible Transition Settings Group

- The `Transition Settings` group is now a checkable, collapsible group box.
- Collapsing it hides the curve graph and the merge sliders so the preview panel can stay compact, while expanding it brings every merge control back into view.
- The collapsed/expanded state is persisted with `QSettings` and restored on the next launch.

### Improved Merge Controls

- Refined the merge slider behavior at the top of the `S` brightness range so the non-linear mapping into the meaningful 99-100% region is more accurate and easier to fine-tune.
- Added a new `MergeStartTest` covering the slider/value mapping for the `R` handoff delay path, including the non-linear end of the slider range and the smoothstep curve used at the top.
- The transition curve math itself is now covered by a dedicated widget and tied back to the live controls, so the on-screen graph cannot drift from the actual merge.

### DNG Export Metadata Fixes

- Reworked the DNG writer to populate a proper EXIF IFD so exported DNG files carry the expected EXIF tags (ExifVersion, FlashPixVersion, ColorSpace, DateTimeOriginal, DateTimeDigitized, ISO, shutter, aperture, focal length, etc.).
- Fixed the ISO speed ratings tag identifier (`34855` is the standard EXIF ISO tag) so raw editors and other tools pick up the value instead of ignoring it.
- Replaced the previous 1/seconds-only exposure encoding with a true rational fraction: exposure times are now written as a proper `numerator/denominator` pair (e.g. 1/250 as `1/250`, 2.5s as `5/2`), reduced to lowest terms.
- EXIF/DateTime handling now consistently writes a single `DateTimeOriginal` and `DateTimeDigitized` from the RAF metadata so timestamps survive across editors.
- Fixed aperture (`FNumber`), focal length and other rational fields so they are written as proper TIFF RATIONALs instead of the previous best-effort encoding.
- Fixed ColorSpace handling so exported DNGs report a sensible value (`sRGB` / `Uncalibrated`) rather than leaving the tag undefined.

### macOS DNG Metadata

- macOS now uses the same DNG metadata path as Windows/Linux instead of the older ad-hoc path, so exported DNG files from the macOS build carry the corrected EXIF IFD.
- Refactored the DNG writer so the EXIF IFD logic lives in one place and is shared across platforms.

### AMaZE Preview Improvements

- Parallelized the AMaZE demosaic on the AMaZE preview rendering path so the second preview method benefits from the available CPU cores.
- Fixed the AMaZE preview export size so the `12 MP` / `6 MP` labels in the export dialog now reflect the actual exported resolution on this route.
- Added a dedicated `AmazeDemosaic` test that exercises the multi-threaded path.

### macOS Build

- Resolved a number of build warnings on the macOS toolchain so the build log is cleaner and the project builds without spurious diagnostics.
- Updated the macOS deployment metadata to match the current `CMakeLists.txt` configuration.

### Raspberry Pi Build

- Updated the Raspberry Pi install instructions to reflect the current packaging flow and clarified the RAM requirement for compilation.
- Realigned the generated `.deb` package metadata with the current build script so the package dependency story is consistent with the latest toolchain.

## Bug Fixes

- Fixed `Amaze debayer` preview export writing the wrong output size on certain code paths.
- Fixed the DNG EXIF IFD being effectively empty on the Windows build, which caused raw editors to fall back to defaults.
- Fixed a small UI issue where the collapsible transition group retained a tall minimum height when collapsed.
- General code health cleanups around the DNG writer, the transition curve widget, and the AMaZE demosaic harness.

## Notes

- `v1.4.1` is a quality-focused follow-up to `v1.4.0`. The new Transition Curve graph, collapsible merge group, and DNG metadata fixes are the headline improvements; the rest is incremental cleanup.
- The DNG metadata overhaul is largely transparent: existing workflows continue to work, but the exported files now expose the camera capture metadata to tools that read it.
- The macOS and Raspberry Pi changes are build/packaging and EXIF metadata; no user-facing behavior changed on those platforms beyond the metadata correctness.
- The AMaZE parallelization change only affects the `Amaze debayer` preview method and does not change the reconstruction preview path.
