# v1.0.0 Release Notes

Release date: 2026-06-13

This is the first public release of SuperCCD S3 RAF to DNG, a Windows desktop application for converting Fujifilm FinePix S3 Pro `.RAF` files into editable `DNG` files, with a focus on the Super CCD SR II sensor's separate `S` and `R` responses.

## Highlights

### Core Functionality

- Loads one or more Fujifilm S3 Pro `RAF` files
- Extracts separate `S` and `R` shot data
- Merges both responses into a highlight-safe `6MP` CFA DNG
- Preserves the original embedded RAF preview for the GUI file list and DNG preview embedding
- Provides a preview workflow for tuning the `S/R` highlight handoff before export

### GUI Features

- RAF file list with embedded thumbnails
- independently resizable preview window for the currently selected RAF file
- draggable and zoomable preview
- preview exposure, white-balance, and tint controls
- preview rotation options
- adjustable `S -> R` highlight handoff parameters
- optional export of individual S and R plane images
- convert current previewed RAF
- convert all listed RAF files
- drag-and-drop support for adding RAF files
- save and restore default parameter values

### 16-bit Preview Pipeline

- Preview generation and preview exports remain at 16-bit precision internally.
- The live display uses fast lookup-table adjustments after scaling to preserve responsive slider interaction.
- Added a luminance-preserving chroma cleanup pass to further reduce colored pixel artifacts around borders and high-contrast edges without softening image detail.
- Added an optional final-image correction for strongly isolated light and dark preview pixels. It changes only classified outlier pixels, preventing global color or detail differences between corrected and uncorrected rendering.
- Added a disabled-by-default `Correct isolated light/dark pixels` checkbox for the live preview and JPEG/TIFF preview exports. DNG output remains unchanged.
- The isolated-pixel correction setting is included in saved and restored defaults.
- Preview zoom and adjustment sliders now render only the visible viewport instead of rebuilding a potentially enormous full-frame image at the selected zoom.

### Preview Export

- Added an output format selector with `JPEG` and lossless `16-bit TIFF` options.
- TIFF export preserves the actual 16-bit RGB preview data instead of upconverting an 8-bit image.
- Both formats support `12 MP` and resized `6 MP` output.
- TIFF filenames use the same size suffix convention, such as `*_preview_12MP.tif` and `*_preview_6MP.tif`.
- JPEG quality is enabled only when JPEG output is selected.

### Preview Sharpening

- Added a mild `Sharpening` slider with a range of `0%` to `100%`.
- Live sharpening is applied after display scaling and a short idle delay, so exposure, white-balance, and other slider movement does not wait for sharpening.
- JPEG and TIFF exports are sharpened at their final output resolution using the 16-bit image data.
- Sharpening changes luminance detail without increasing color separation at edges.
- The sharpening setting is included in saved and restored defaults.

### Preview Window

- Moved the image preview into a separate, independently resizable window.
- The RAF list and processing settings remain together in the main window.
- Added `Show Preview` to reopen or bring the preview window to the front.
- The preview window remembers its size and position between sessions.

### White Balance Picker

- Added a checkable white balance picker for the live preview.
- The resizable sample box assumes the selected pixels are neutral gray and
  calculates matching white-balance and tint slider values.
- The mouse wheel resizes the picker while it is active, and left-click applies
  the sample.
- Picker calculations use the full-resolution 16-bit preview data.

### Preview Controls

- Added `Preview Gamma` control.
- Added `Preview Contrast` control.
- Added `Preview Saturation` control.
- Added `Preview Highlight Compression`.
- Wired highlight compression into both the live preview and exported preview JPG.
- Wired preview gamma and contrast settings more cleanly through the CFA preview path.

### Saved Defaults

- Expanded `Save Current As Default` so all preview sliders are persisted.
- `Reset Defaults` and first-run defaults now use the current built-in preview starting values.

### Command Line

- Added standalone `--version` and `-v` options that print the current program version without opening the GUI.
- Convert a RAF file: `superccd2dng.exe input.raf output.dng --6mp-cfa`

### RawTherapee Workflow

- Added a bundled RawTherapee processing profile: `RawTherapee profile\s3pro_dng.pp3`
- Documented how to use that profile as a starting point for the converted `*_sr_merged.dng` files.
- Documented how to configure a RawTherapee dynamic default profile for these files.

The included profile provides basic correction choices for the current workflow, including exposure shaping, tone adjustments, demosaicing, sharpening, and the default crop/rotation used for these files.

## Notes

- TIFF preview export requires a build with LibTIFF support.
- LibTIFF can now be enabled alongside the Adobe DNG SDK so DNG and 16-bit TIFF export are both available.
- Sharpening affects the live preview and preview exports only; it does not affect DNG output.
- Preview export writes a JPG of the currently rendered preview, not the raw DNG output.
- Preview rendering was improved to reduce colored pixel artifacts around borders and other high-contrast edges.
- Highlight compression affects the preview and preview JPG export only.
- The new preview sliders are for evaluation only and do not alter the exported DNG data.
- The stable supported output remains the `6MP Raw CFA DNG` workflow.
- The generated DNG is currently a rotated output; final orientation and framing are expected to be corrected in RawTherapee.
