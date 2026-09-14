# Release Notes v1.6.0

Release date: 2026-09-14

This release replaces the previous flat exposure controls with a simpler
contrast-based workflow and keeps the result consistent across the preview,
exposure scopes, and exported output.

## Highlights

### Contrast-Based Flat Exposure

- Flat exposure now uses two controls: **Contrast amount** and **Contrast
  pivot**.
- The contrast curve is centered on the selected pivot and no longer exposes
  the obsolete flat range and transition controls.
- Flat processing operates on normalized luma and scales RGB together, which
  preserves the image chroma while changing brightness and contrast.
- Preview rendering, waveform and histogram source data, preview export, and
  final export use the same flat exposure behavior.

### Simplified Contrast Controls

- Removed the legacy highlight compression, tone balance, and balance bias
  controls from the main window.
- The unified **Contrast** group now contains the amount and pivot controls
  used by the current flat workflow.
- Presets save the new settings as `previewContrastAmount` and
  `previewContrastPivot`.

### Presets

- Updated the built-in **Default** preset to the current contrast-based
  exposure settings.
- Removed the obsolete **Natural** preset.
- Presets now carry version `1.6.0`.

## Notes

- Older preset keys are ignored by the current UI. Presets without the new
  contrast keys use the current control defaults.
- The raw DNG conversion format remains the `6MP Raw CFA DNG`.