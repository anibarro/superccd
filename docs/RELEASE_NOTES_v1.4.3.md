# Release Notes v1.4.3

Release date: 2026-07-20

This release builds on `v1.4.2` with new tone mapping controls and a reorganized preview adjustments panel, plus improvements to the shadow recovery range control.

## Highlights

### New Tone Mapping Controls

- Added a new **Tone Mapping** control group to the preview adjustments panel with two sliders:
  - **Tone balance**: Controls the overall strength of the tone mapping effect. At 0 the effect is off; at 100 shadows are lifted and highlights are compressed for maximum dynamic range utilization.
  - **Balance bias**: Shifts the tone curve pivot point. Lower values favor highlight preservation (more highlight compression), higher values favor shadow preservation (more shadow lift), and 50 is neutral.
- The tone mapping uses a two-stage curve:
  1. A power-curve tone map with separate exponents for shadows and highlights, split at a bias-dependent pivot point
  2. A midtone contrast correction that applies a gentle S-curve around the pivot to compensate for slope reduction and keep the image looking punchy rather than flat
- Both controls are backed by numeric spin boxes for precise input and keyboard-driven adjustments.

### Reorganized Preview Adjustments Panel

- Reorganized the preview adjustments controls into three visual groups:
  - **Tone Mapping**: Tone balance and Balance bias
  - **Shadow Recovery**: Shadows and Shadow range
  - **Color**: Saturation, White balance, Tint, and White balance picker
- Each group is displayed in a styled panel frame with a subtle background and border for a cleaner, more scannable layout.
- The visual grouping makes it easier to find related controls and understand the adjustment categories at a glance.

### Improved Shadow Recovery Range Control

- Enhanced the shadow recovery range control with a proportional smoothstep falloff that scales with the pivot point.
- Low range values now have narrow transitions (affecting only the deepest shadows) while high range values have wide transitions (spanning the full tonal range).
- The transition width is proportional to the pivot, ensuring a natural blend at all range settings.
- This makes the Shadow range slider more intuitive and produces more natural-looking results across the full range of values.

## Technical Details

### Tone Mapping Algorithm

The new tone mapping implementation uses a sophisticated two-stage approach:

1. **Shadow/Highlight Split Processing**: The curve splits at a pivot point determined by the bias parameter (0.5 + bias × 0.25, clamped to [0.2, 0.8]). Shadows use a power curve with exponent `1/(1 - shadow_strength)` to lift dark values, while highlights use `1 + highlight_strength × 2.5` to compress bright values.

2. **Midtone Contrast Correction**: At the pivot point, the slope is reduced by the shadow lift and highlight compression. A sigmoid-shaped local contrast boost centered on the pivot compensates for this, estimated from the actual slope at the pivot and clamped to prevent over-sharpening.

### Shadow Recovery Improvements

The shadow recovery mask now uses a smoothstep falloff where the transition width scales proportionally with the pivot point:
- `shadowRange = 1.0`: Effect spans [0, 0.60] with gentle falloff
- `shadowRange = 0.0`: Effect confined to [0, 0.10] with smooth cutoff
- Transition width = `pivot + pivot × 1.5`, ensuring natural transitions at all range values

## Notes

- `v1.4.3` is a quality-of-life release focused on tone mapping and UI organization. The headline changes are the new tone mapping controls and the reorganized preview panel.
- The tone mapping controls work independently from the existing Exposure, Gamma, Contrast, and Shadows adjustments, and can be combined with them for fine-tuned preview rendering.
- The visual grouping is purely cosmetic and does not change any existing functionality or default values.
- The improved shadow recovery range control maintains backward compatibility: existing settings produce the same visual results, but the slider response is now more intuitive.
- These changes only affect the live preview path. They do not change the raw DNG conversion pipeline, EXIF metadata, or any export behavior.
