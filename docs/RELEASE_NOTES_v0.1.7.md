# v0.1.7 Release Notes

Release date: 2026-06-07

This release builds on `v0.1.6` with a more capable preview workflow and better persistence of preview settings.

## Highlights

### Preview export

- Added `Export Preview` next to `Update Preview`.
- Added an export popup to choose destination folder and JPEG quality.
- Export now saves the current adjusted preview as a `JPG`.
- Added export size options for preview JPG output.
- The resized export option now uses a proportional resize with the short side set to `2016 px`.

### Preview controls

- Added `Preview Highlight Compression`.
- Wired highlight compression into both the live preview and exported preview JPG.

### Saved defaults

- Expanded `Save Current As Default` so all preview sliders are persisted.
- `Reset Defaults` and first-run defaults now use the current built-in preview starting values.

## Notes

- Preview export writes a JPG of the currently rendered preview, not the raw DNG output.
- Highlight compression affects the preview and preview JPG export only.
