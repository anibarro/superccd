# Release Notes v1.4.4

Release date: 2026-07-20

This release adds a noise reduction control for the R (secondary) pixel data during the S+R merge process, addressing the characteristic noise and grain present in R pixel regions.

## Highlights

### R Pixel Noise Reduction

- Added a new **R noise reduction** slider (0–100) in the **Transition Settings** group.
- The slider controls an edge-aware bilateral noise reduction filter applied to the R pixel plane before merging. It reduces grain/luma noise while preserving luminance edges, using variance-adaptive range sigma so flat areas get stronger smoothing than edge regions.
- The control defaults to 0 (no noise reduction), preserving the original behavior.
- The noise reduction includes a luminance-dependent activity curve that reduces NR effect in deep shadows (< 3% white) and specular highlights (> 90% white) where NR would be less useful or potentially harmful.
- The NR is applied to both the preview and the final merged DNG output, ensuring consistent results.

### Technical Details

The R pixel noise reduction uses an edge-aware bilateral filter approach:

1. **Luma NR**: Edge-aware spatial smoothing with a 5×5 CFA stride-2 neighborhood (24 same-channel neighbors). The Gaussian spatial kernel (σ=1.5) is combined with intensity-dependent range weights. Range sigma scales from 0.2% to 15% of white point based on strength. Blend factor uses √(strength) for more responsive control at low values.

2. **Activity curve**: Prevents NR artifacts in extreme tonal regions by smoothly fading out the NR effect in deep shadows and highlights.

3. **Parallel processing**: The NR is applied in parallel by horizontal bands for performance.

4. **Zero-copy optimization**: When the slider is at 0, the original R plane is used without any copy or processing overhead.

### CLI Support

A new command-line flag for batch processing:

```bash
superccd2dng input.raf output.dng --r-luma-nr=0.3
```

- `--r-luma-nr=<value>`: Set NR strength (0.0 to 1.0)

## Notes

- `v1.4.4` is a quality-of-life release focused on addressing the characteristic noise in R pixel data that can make merged images look strange when transitioning from smooth S pixel regions to noisy R pixel regions.
- The NR control works in conjunction with the existing transition settings (Merge start, Transition width, Smoothness) and affects both preview and DNG output.
- The noise reduction is applied before the S+R merge, ensuring that the R pixel data is cleaned before blending with the S pixel data.
- These changes affect both the live preview path and the raw DNG conversion pipeline, providing consistent results between preview and final output.
