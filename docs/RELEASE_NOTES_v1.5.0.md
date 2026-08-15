# Release Notes v1.5.0

Release date: 2026-08-15

This release overhauls the preview workflow, adds named presets and per-image settings persistence, and improves shadow and highlight handling for more predictable, editable results.

## Highlights

### Named Presets

- Replaced the old **Reset Defaults** / **Save Current As Default** buttons with a **Preset** drop-down and a **Save current** button.
- Presets are stored as individual JSON files in the `presets/` folder next to the executable (or inside the macOS app bundle's `Resources/presets`).
- The built-in **Default** preset is read-only; users can save as many custom presets as they like.
- The preset combo shows an asterisk (`*`) when the current control values differ from the loaded preset, so it is always obvious whether a preset has been tweaked.
- Presets now capture the full UI state: S/R transition parameters, R luma noise reduction, all preview adjustments (exposure, white balance, tint, gamma, contrast, shadows, shadow range, saturation, sharpening, highlight compression, tone balance, balance bias), preview method, and outlier correction.

### Per-Image Settings Persistence

- Settings for each input RAF are automatically saved in a `.superccd_settings.json` file inside the selected output folder.
- When a previously-processed image is selected again, its settings are restored automatically.
- This makes it easy to return to a folder of images later and pick up exactly where you left off, without relying on global defaults or manual preset selection.

### Reworked Exposure and Gamma Controls

- The preview **Exposure** control is now a linear brightness percentage (-100% to +100%) instead of f-stops.
  - 0% keeps the original brightness.
  - +100% doubles the linear brightness.
  - Slider step is 0.1% for fine control.
- The **Gamma** slider and spin box now work in true display gamma values from 0.10 to 4.00, with a default of 2.2.
  - 1.0 leaves the preview linear, 2.2 applies the standard sRGB-like encoding curve, and values above 2.2 darken the midtones further.
  - Standard values such as 1.8, 2.0, 2.2 and 2.4 are ticked on the slider for quick selection.
- Both controls use a C locale for the numeric spin box so the decimal separator is always a dot, avoiding locale-specific parsing issues.

### Improved Shadow Recovery

- The shadow recovery curve has been rewritten to guarantee monotonic output.
- The old curve could fold back on itself at high strength and narrow range, producing an inverted, negative-like shadow artefact.
- The new curve lifts dark values with a power curve and blends it using a log-space sigmoidal mask, so shadows get brighter smoothly while midtones and highlights stay untouched.
- The mask threshold extends from the deepest shadows (~1% white) up to midtones (~60% white) as the range slider increases.

### Higher Preview Window Quality

- The live preview is now rendered through a full 16-bit-per-channel lookup table before conversion to 8-bit display.
- The internal preview image is stored in linear light, so the Gamma control behaves as a true display gamma and scaling/interpolation operate on linear values.
- This avoids the intermediate 8-bit quantisation that caused banding in smooth gradients, especially after exposure, gamma, and shadow adjustments.
- The LUT is rebuilt on every slider change (≈1 ms) so preview responsiveness is unchanged, while the pixel loop is actually faster.

### Better Highlight Recovery Handling

- Preview normalisation now prefers the sensor's white level over the image's actual highlight percentile.
- This preserves the visual difference between under- and over-exposed frames and retains highlight-recovery information from the R plane that can exceed the sensor white level.
- Combined with the 16-bit preview LUT, bright highlight rolloff is smoother and more predictable.

### Preset Distribution and Packaging

- The `presets/` directory is now copied into Windows packages, Raspberry Pi packages, and the macOS app bundle Resources during build and packaging.
- CMake post-build steps also copy presets next to the executable (Windows/Linux) or into the bundle (macOS) so local developer builds find them automatically.

### Test Coverage

- Added a `preview_image_processing_test` that exercises the shadow-recovery LUT path and verifies the curve is monotonic for multiple strength/range combinations.
- Added to the CTest suite so builds with `BUILD_TESTING=ON` will run it automatically.

## Notes

- `v1.5.0` is a usability and quality release. It does not introduce new export formats; the stable output path remains the `6MP Raw CFA DNG`.
- Presets created with earlier releases are not automatically migrated because the old defaults were stored in application settings rather than a preset file. Users can recreate their preferred starting point once with **Save current**.
- Per-image settings are tied to the output folder. Moving or renaming the output folder will start a fresh `.superccd_settings.json` file.
- The exposure value in the Default preset is stored in tenths of a percent; existing JSON presets can continue to be edited by hand if desired.
