# Release Notes v1.4.0

Release date: 2026-06-17

This release builds on `v1.3.2` with a broader preview workflow update. The preview UI now exposes dedicated shadow recovery controls, keeps the growing control set usable in a scrollable panel, and adds a selectable second rendering path based on AMaZE debayering for the merged 6 MP preview CFA. The final `v1.4.0` preview path also reduces false-color artifacts in rendered previews, improves the native 6 MP geometry used by the AMaZE route, and preserves the current zoom level when switching between files.

## Highlights

### Shadow Recovery Controls

- Added a new `Shadows` slider and matching `Shadow range` control to the preview panel.
- Shadow recovery is applied in both the live preview display path and the exported preview adjustment path, so `JPEG` and `16-bit TIFF` preview exports match the on-screen tonal lift.
- Both controls are fully wired through the existing slider/spinbox sync, saved defaults, reset defaults, and persisted application settings.

### Dual Preview Methods

- Added a `Method` selector in the preview controls with two choices: `Reconstruction` and `Amaze debayer`.
- `Reconstruction` remains the established preview path and continues to support the full preview/export workflow.
- `Amaze debayer` adds an alternative 6 MP preview rendering mode that demosaics the merged S/R CFA through the AMaZE path for a different detail/color tradeoff during evaluation.
- The selected preview method is stored in defaults and restored on startup.

### Preview Rendering Refinements

- Added a dedicated 16-bit false-color suppression pass for the rendered preview image before display/export-only adjustments are applied.
- Reworked the native geometry handling used by the AMaZE preview route so the sparse SuperCCD RGB preview is rectified onto a cleaner 6 MP grid before further processing.
- Preview export size labels now use explicit `12 MP` and `6 MP` naming, and the AMaZE preview route correctly limits export sizing to the supported 6 MP path.

### Preview UI Behavior

- Wrapped the growing preview control section in its own scroll area so the main window remains usable at smaller vertical sizes.
- Adjusted the main splitter defaults and minimum control-pane width to give the RAF list and preview controls a more stable initial layout.
- Fixed preview refresh on newly loaded files so the current zoom slider value is respected instead of being overwritten by an automatic fit-to-window zoom reset.

## Notes

- `v1.4.0` is primarily a preview-workflow release. The new `Amaze debayer` mode affects preview rendering and preview exports; it does not replace the raw DNG conversion path.
- The `Reconstruction` method remains available and is still the default-compatible path for users who prefer the earlier preview behavior.
- The shadow controls are preview-only adjustments, consistent with the existing exposure, white balance, gamma, contrast, saturation, sharpening, and highlight-compression controls.
