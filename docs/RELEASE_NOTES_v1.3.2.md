# Release Notes v1.3.2

Release date: 2026-06-14

This minor revision builds on `v1.3.1` with metadata improvements for exported files and a more robust preview rendering pipeline. Preview exports can now optionally carry the source RAF capture metadata, DNG export writes a broader set of capture fields directly into the generated file, and the preview normalization pipeline now uses sensor black/white levels together with a histogram-based highlight reference for more consistent exposure across frames.

## Highlights

### Preview Normalization Rework

- Extracted the preview color normalization logic into a dedicated `PreviewColorNormalization` module exposing `PreviewChannelGains`, `derivePreviewChannelGains`, and `previewScaleToFit16Bit` helpers.
- Replaced the previous "scale to the brightest single pixel" approach with a shared **histogram-based reference (99.95th percentile)** combined with the sensor's white level, so isolated bright pixels (specular highlights, blown skies) no longer drag the whole preview exposure down.
- Added explicit **per-channel black level subtraction** in both the CFA and RGB preview paths. The reference level now reflects actual sensor signal, so dark frames and high-base-ISO captures render with the intended headroom.
- Preview gain derivation is unchanged in behavior: AsShotNeutral/tint metadata still takes precedence over the per-image average ratios, but the resulting gains are now applied against a stable reference instead of a per-pixel max.

### R-Transition Control Sync Fix

- `MainWindow::applyParameterSettings()` now updates the delay and smoothness **spinboxes in addition to the sliders** when applying a preset or restoring a previous configuration.
- Signal blocking is also applied to the spinboxes so programmatic updates do not trigger redundant re-renders.

### Auto Preview by Default

- The **Auto Preview** option is now enabled by default when launching the application, so selecting a file in the list automatically refreshes the preview without an extra click.
- Adjusted default preview slider values to better match the new normalization: exposure `0`, gamma `40`, contrast `38`, highlight compression `30`, zoom `35`.
- The bundled `RawTherapee profile/s3pro_dng.pp3` has been retuned (exposure compensation `0`, contrast `37`, highlight compression `0`, sharpening contrast `7`, deconvolution radius `0.69`) so the downstream RawTherapee output matches the new on-screen rendering.

### Per-File Preview Rotation

- The preview now **automatically sets its rotation to match the original RAF file**: each item in the file list stores the rotation derived from the source RAF metadata, and when a file is selected the **Preview Rotation** combo is updated to match the embedded orientation (0°, 90°, 180°, or 270°). Portrait/landscape shots therefore display correctly without any manual rotation.
- Rotation is read from LibRaw's actual flip codes (`0`, `3` = 180°, `5` = 90° CCW, `6` = 90° CW, negative = mirrored) and is stored on the QListWidgetItem via a custom `Qt::UserRole + 1` data role.

### Preview Export EXIF Toggle

- Added an `Include EXIF metadata` checkbox to the preview export popup for `JPEG` and `16-bit TIFF` exports.
- When enabled, the exporter copies common capture metadata such as camera model, lens model, ISO, shutter speed, aperture, focal length, and capture date from the source RAF onto the exported preview file.
- On builds where `exiftool` is not available on the system, EXIF-enabled preview export will report an error instead of silently exporting a file without metadata.

### Expanded DNG Metadata

- DNG export now always writes the available capture metadata directly into the generated DNG, including focal length, lens model, aperture, shutter speed, ISO, camera identity, and capture timestamps.
- The LibTIFF DNG writer now emits additional capture tags such as `FocalLength`, `LensModel`, `DateTimeOriginal`, and `DateTimeDigitized` when those values are available from the source RAF metadata.
- This metadata is written by the DNG writer itself and does not depend on `exiftool`.

## Notes

- `v1.3.2` does not alter the merge logic itself; it focuses on metadata export and preview rendering quality.
- The preview pipeline now uses a histogram-derived reference instead of a per-pixel max; if you were previously tuning around the old scaling, the default slider values have been retuned accordingly.
- Preview export EXIF copying depends on `exiftool` being available in `PATH` or in the standard Homebrew install locations already checked by the application.
- DNG metadata embedding is handled directly by the LibTIFF export path and does not depend on `exiftool`.
- A new `preview_color_normalization_test` unit test exercises the channel-ratio stability, metadata-derived gains, and the new histogram-based reference behavior (isolated highlight outlier ignored, broad highlights retained).
