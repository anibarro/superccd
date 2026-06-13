# v0.1.9 Release Notes

Release date: 2026-06-13

This release improves preview image quality, adds true 16-bit TIFF preview export, and introduces responsive preview sharpening.

## Highlights

### 16-bit preview pipeline

- Preview generation and preview exports remain at 16-bit precision internally.
- The live display uses fast lookup-table adjustments after scaling to preserve responsive slider interaction.
- Added a luminance-preserving chroma cleanup pass to further reduce colored pixel artifacts around borders and high-contrast edges without softening image detail.
- Added an optional final-image correction for strongly isolated light and dark preview pixels. It changes only classified outlier pixels, preventing global color or detail differences between corrected and uncorrected rendering.
- Added a disabled-by-default `Correct isolated light/dark pixels` checkbox for the live preview and JPEG/TIFF preview exports. DNG output remains unchanged.
- The isolated-pixel correction setting is included in saved and restored defaults.
- Preview zoom and adjustment sliders now render only the visible viewport instead of rebuilding a potentially enormous full-frame image at the selected zoom.

### Preview export

- Added an output format selector with `JPEG` and lossless `16-bit TIFF` options.
- TIFF export preserves the actual 16-bit RGB preview data instead of upconverting an 8-bit image.
- Both formats support `12 MP` and resized `6 MP` output.
- TIFF filenames use the same size suffix convention, such as `*_preview_12MP.tif` and `*_preview_6MP.tif`.
- JPEG quality is enabled only when JPEG output is selected.

### Preview sharpening

- Added a mild `Sharpening` slider with a range of `0%` to `100%`.
- Live sharpening is applied after display scaling and a short idle delay, so exposure, white-balance, and other slider movement does not wait for sharpening.
- JPEG and TIFF exports are sharpened at their final output resolution using the 16-bit image data.
- Sharpening changes luminance detail without increasing color separation at edges.
- The sharpening setting is included in saved and restored defaults.

### Command line

- Added standalone `--version` and `-v` options that print the current program version without opening the GUI.

## Notes

- TIFF preview export requires a build with LibTIFF support.
- LibTIFF can now be enabled alongside the Adobe DNG SDK so DNG and 16-bit TIFF export are both available.
- Sharpening affects the live preview and preview exports only; it does not affect DNG output.
