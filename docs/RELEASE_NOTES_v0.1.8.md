# v0.1.8 Release Notes

Release date: 2026-06-12

This release refines preview export output and keeps the preview workflow aligned with the rendered image.

## Highlights

### Preview export

- Added `Export Preview` next to `Update Preview`.
- Added an export popup to choose destination folder and JPEG quality.
- Export now saves the current adjusted preview as a `JPG`.
- Added export size options for preview JPG output: `12 MP` and resized `6 MP`.
- The resized export option uses a proportional resize with the short side set to `2016 px`.
- Exported preview JPG filenames now include the size suffix: `*_preview_12MP.jpg` or `*_preview_6MP.jpg`.

### Preview controls

- Added `Preview Highlight Compression`.
- Wired highlight compression into both the live preview and exported preview JPG.

### Saved defaults

- Expanded `Save Current As Default` so all preview sliders are persisted.
- `Reset Defaults` and first-run defaults now use the current built-in preview starting values.

## Notes

- Preview export writes a JPG of the currently rendered preview, not the raw DNG output.
- Preview rendering was improved to reduce colored pixel artifacts around borders and other high-contrast edges.
- Highlight compression affects the preview and preview JPG export only.
