# SuperCCD S3 RAF to DNG

Windows desktop application for converting Fujifilm FinePix S3 Pro `.RAF` files into editable `DNG` files, with a focus on the Super CCD SR II sensor's separate `S` and `R` responses.

## Project Status

This repository is currently centered on one supported output path:

- `6MP Raw CFA DNG`

That path is the stable result of the current work and is the one intended for real use.

## AI-Origin Statement

This project was developed **100% with AI, with no manual intervention in the implementation**.

- Human author and maintainer: `Eduardo Anibarro`
- Code, refactors, UI changes, reverse-engineering iterations, and documentation were generated through AI-assisted sessions

That statement is included here deliberately so contributors understand the origin of the codebase before working on it.

## What The Application Does

- Loads one or more Fujifilm S3 Pro `RAF` files
- Extracts separate `S` and `R` shot data
- Merges both responses into a highlight-safe `6MP` CFA DNG
- Preserves the original embedded RAF preview for the GUI file list and DNG preview embedding
- Provides a preview workflow for tuning the `S/R` highlight handoff before export

## Current Scope

Supported and intended:

- Fujifilm FinePix S3 Pro RAF files
- Windows
- Qt 6 desktop GUI
- `6MP Raw CFA DNG` export

Present in source but not a supported workflow:

- legacy experimental `12MP` reconstruction code
- older diagnostic and reverse-engineering helpers inside `SuperCCDProcessor.cpp`

Those experimental paths are kept only as research history. They are not the recommended path for normal use.

## Why The Output Is A CFA DNG

The stable workflow keeps the merged data as a CFA DNG because:

- it preserves more editability than a baked RGB render
- RawTherapee currently gives better detail than the app's abandoned linear-DNG experiments
- the merged CFA path is the most reliable result reached so far

The intended post-processing workflow is:

1. Convert RAF to `6MP Raw CFA DNG`
2. Open the DNG in RawTherapee
3. Apply the included RawTherapee profile from `RawTherapee profile\s3pro_dng.pp3`
4. Demosaic and finish the image there

Important:

- the generated DNG is currently a rotated output
- final orientation and framing are expected to be corrected in RawTherapee

## Included RawTherapee Profile

This repository includes a starter RawTherapee processing profile:

- `RawTherapee profile\s3pro_dng.pp3`

It is meant as a basic correction preset for the generated S3 Pro DNG files. The profile applies a starting point for:

- exposure compensation and highlight compression
- white balance based on camera metadata
- tone equalizer adjustments
- a default crop and rotation
- AMaZE demosaicing
- post-demosaic sharpening

Use it as a baseline, not as a finished look. You should still expect to fine-tune exposure, crop, color, and sharpening per image.
The included crop and rotation are already part of the profile.

### Using The Profile Manually

1. Open a converted `*_sr_merged.dng` in RawTherapee.
2. Load `RawTherapee profile\s3pro_dng.pp3` as a processing profile.
3. Adjust the result as needed for the specific image.

### Setting Up A Dynamic Default Profile In RawTherapee

If you want RawTherapee to apply this profile automatically to these DNG files:

1. Make the profile available to RawTherapee.
2. In RawTherapee, open `Preferences`.
3. Set the default processing profile for raw files to `(Dynamic)`.
4. Open the `Dynamic Profile Rules` section.
5. Add a rule for the Fujifilm S3 Pro DNG workflow.
6. Set the `Camera` condition to match the camera metadata shown by RawTherapee for these files.
7. Attach `s3pro_dng.pp3` to that rule.
8. Keep the rule near the end of the rule list if you want it to override more general raw defaults.
9. For files already visible in the file browser, use `Processing Profile Operations > Reset to Default` so the dynamic chain is applied again.

RawPedia notes that dynamic rules are combined in list order, and later matching rules can override earlier ones. See: https://rawpedia.rawtherapee.com/Dynamic_processing_profiles

## Key Limitations

- The project is specialized for the S3 Pro Super CCD SR II sensor
- The output is intentionally highlight-safe by default, so images may open darker than a normal camera raw
- Some experimental code remains in the repository and should not be treated as stable API
- The codebase is usable, but it is still research-driven rather than polished as a general-purpose photo product

## GUI Features

- RAF file list with embedded thumbnails
- preview of the currently selected RAF file
- draggable and zoomable preview
- preview exposure, white-balance, and tint controls
- preview rotation options
- adjustable `S -> R` highlight handoff parameters
- optional export of individual S and R plane images
- convert current previewed RAF
- convert all listed RAF files
- drag-and-drop support for adding RAF files
- save and restore default parameter values

Performance note:

- the first preview or conversion of a RAF file is slower because the app must decode and cache the raw data
- repeated previews on the same file are faster

## Command Line

Convert a RAF file:

```powershell
superccd2dng.exe input.raf output.dng --6mp-cfa
```

Display the installed version without opening the GUI:

```powershell
superccd2dng.exe --version
```

The short version option is `-v`.

## Build Requirements

- Windows
- CMake `3.16+`
- Qt `6` with `Widgets`
- LibRaw
- LibTIFF
- Visual Studio C++ toolchain

Adobe DNG SDK is not required for the current stable path. LibTIFF is the practical writer backend used here.

## Build

This repository already includes `run_vs_setup.cmd`, which is the expected local build entrypoint in this project.

Typical build:

```powershell
cmd /c run_vs_setup.cmd build
```

GitHub release package:

```powershell
cmd /c run_vs_setup.cmd package
```

That creates a zip in `dist\` with a default name like `superccd2dng-windows-x64-20260529.zip`.
You can override the package name:

```powershell
cmd /c run_vs_setup.cmd package superccd2dng-windows-x64-v0.1.0
```

If you need to configure from scratch, the important CMake inputs are:

- `Qt6_DIR`
- `LIBRAW_ROOT`
- optionally `TIFF_ROOT`

Example:

```powershell
cmake -S . -B build ^
  -G "Visual Studio 18 2026" -A x64 ^
  -DQt6_DIR="X:/path/to/Qt/lib/cmake/Qt6" ^
  -DLIBRAW_ROOT="X:/path/to/libraw" ^
  -DTIFF_ROOT="X:/path/to/libtiff"
cmake --build build --config Release
```

## Command-Line Usage

Minimal usage:

```powershell
build\superccd2dng.exe input.raf output.dng --6mp-cfa
```

Important behavior:

- the app writes three files from that base output name:
  - `_s_pixels.dng`
  - `_r_pixels.dng`
  - `_sr_merged.dng`
- the merged file is the main result

Example:

```powershell
build\superccd2dng.exe samples\DSCF0125.RAF tests\DSCF0125.dng --6mp-cfa
```
## Repository Structure

- [src](src)
  - application code
- [resources](resources)
  - icons and Qt resources
- [docs/MANUAL.md](docs/MANUAL.md)
  - end-user application manual
- [CONTRIBUTING.md](CONTRIBUTING.md)
  - contributor notes
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
  - dependency and attribution notes

## Contributor Expectations

If you want to contribute, read:

- [docs/MANUAL.md](docs/MANUAL.md)
- [CONTRIBUTING.md](CONTRIBUTING.md)

In practice:

- do not change the stable `6MP` merge behavior casually
- treat highlight recovery regressions as critical
- test against real RAF files, not only synthetic examples

## License

This repository is licensed under the MIT License.

See [LICENSE](LICENSE).
