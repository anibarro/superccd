# Release Notes v1.4.4

Release date: 2026-07-20

This release adds advanced noise reduction controls for the R (secondary) pixel data during the S+R merge process, addressing the characteristic noise and grain present in R pixel regions.

## Highlights

### R Pixel Noise Reduction

- Added two new noise reduction sliders in the **Transition Settings** group:
  - **R color NR** (0–100): Suppresses chroma noise in the R pixel plane before merging. Targets high-frequency color variations among same-channel CFA neighbors using a bilateral filter with wide range sigma for aggressive chroma smoothing while preserving luminance edges.
  - **R luma NR** (0–100): Reduces grain/luma noise in the R pixel plane before merging. Uses an edge-aware bilateral filter that preserves luminance edges while smoothing flat areas, with intensity-dependent range weights for natural-looking results.
- Both controls default to 0 (no noise reduction), preserving the original behavior.
- The noise reduction includes a luminance-dependent activity curve that reduces NR effect in deep shadows (< 3% white) and specular highlights (> 90% white) where NR would be less useful or potentially harmful.
- The NR is applied to both the preview and the final merged DNG output, ensuring consistent results.

### Technical Details

The R pixel noise reduction uses a sophisticated dual bilateral filter approach:

1. **Luma NR**: Edge-aware spatial smoothing with a 5×5 CFA stride-2 neighborhood (24 same-channel neighbors). The Gaussian spatial kernel (σ=1.5) is combined with intensity-dependent range weights. Range sigma scales from 0.2% to 15% of white point based on strength. Blend factor uses √(strength) for more responsive control at low values.

2. **Color NR**: Same spatial kernel and neighborhood structure, but with a wider range sigma (0.2% to 20%) to let spatial smoothing dominate, effectively low-pass filtering chroma while preserving luma detail.

3. **Activity curve**: Prevents NR artifacts in extreme tonal regions by smoothly fading out the NR effect in deep shadows and highlights.

4. **Parallel processing**: The NR is applied in parallel by horizontal bands for performance.

5. **Zero-copy optimization**: When both sliders are at 0, the original R plane is used without any copy or processing overhead.

### CLI Support

Two new command-line flags for batch processing:

```bash
superccd2dng input.raf output.dng --r-color-nr=0.5 --r-luma-nr=0.3
```

- `--r-color-nr=<value>`: Set color NR strength (0.0 to 1.0)
- `--r-luma-nr=<value>`: Set luma NR strength (0.0 to 1.0)

## Notes

- `v1.4.4` is a quality-of-life release focused on addressing the characteristic noise in R pixel data that can make merged images look strange when transitioning from smooth S pixel regions to noisy R pixel regions.
- The NR controls work in conjunction with the existing transition settings (Merge start, Transition width, Smoothness) and affect both preview and DNG output.
- The noise reduction is applied before the S+R merge, ensuring that the R pixel data is cleaned before blending with the S pixel data.
- These changes affect both the live preview path and the raw DNG conversion pipeline, providing consistent results between preview and final output.
