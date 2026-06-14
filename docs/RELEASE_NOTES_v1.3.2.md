# Release Notes v1.3.2

Release date: 2026-06-14

This minor revision builds on `v1.3.1` with metadata improvements for exported files. Preview exports can now optionally carry the source RAF capture metadata, and DNG export now writes a broader set of capture fields directly into the generated file.

## Highlights

### Preview Export EXIF Toggle

- Added an `Include EXIF metadata` checkbox to the preview export popup for `JPEG` and `16-bit TIFF` exports.
- When enabled, the exporter copies common capture metadata such as camera model, lens model, ISO, shutter speed, aperture, focal length, and capture date from the source RAF onto the exported preview file.
- On builds where `exiftool` is not available on the system, EXIF-enabled preview export will report an error instead of silently exporting a file without metadata.

### Expanded DNG Metadata

- DNG export now always writes the available capture metadata directly into the generated DNG, including focal length, lens model, aperture, shutter speed, ISO, camera identity, and capture timestamps.
- The LibTIFF DNG writer now emits additional capture tags such as `FocalLength`, `LensModel`, `DateTimeOriginal`, and `DateTimeDigitized` when those values are available from the source RAF metadata.
- This metadata is written by the DNG writer itself and does not depend on `exiftool`.

## Notes

- `v1.3.2` is a metadata-focused release; it does not alter the merge logic or preview rendering pipeline.
- Preview export EXIF copying depends on `exiftool` being available in `PATH` or in the standard Homebrew install locations already checked by the application.
- DNG metadata embedding is handled directly by the LibTIFF export path.
