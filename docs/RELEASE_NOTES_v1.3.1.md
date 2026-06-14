# Release Notes v1.3.1

Release date: 2026-06-14

This minor revision builds on `v1.3.0` with a focused RAF list UI update. The file list now surfaces key shot metadata directly under each RAF name, and the thumbnail row layout has been tightened so the list stays denser and easier to scan.

## Highlights

### Richer RAF List Metadata

- Added camera model display under the RAF file name in the main file list.
- Added a second metadata line for `ISO`, shutter speed, and aperture.
- Added a third metadata line for focal length and lens model when that information is available in the RAF metadata.

### More Compact Thumbnail Rows

- Switched the RAF list thumbnail frame to a `96x64` `3:2` presentation instead of the previous square thumbnail box.
- Reduced extra line spacing in the list row text so metadata fits more cleanly beside the thumbnail.
- Kept each RAF row aligned to the thumbnail height to avoid oversized list entries.

### Metadata Extraction Support

- Added a lightweight metadata-read helper in `src/SuperCCDProcessor.cpp` and `src/SuperCCDProcessor.h` for list display use.
- Extended the shared metadata struct with focal length and lens model fields sourced from LibRaw metadata.
- Preserved existing thumbnail extraction behavior and tooltip error reporting while adding metadata tooltips for read failures.

## Notes

- `v1.3.1` is a UI-focused refinement release; it does not change the raw conversion pipeline or output format behavior from `v1.3.0`.
- Lens model and focal length are shown only when the source RAF metadata provides them.
