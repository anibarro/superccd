# Release Notes v2.0.0

Release date: 2026-11-06

This release rebuilds how images are developed. Exposure, tone and color controls are
new, photos open at the exposure they were shot at, the S + R merge has been reviewed
against real sensor data, and the whole dynamic range stays available while editing.

## Highlights

### Photos Open At Their Real Exposure

- The preview used to be scaled by the brightest content of each frame, so every photo
  came out balanced. Exposure is now anchored to the sensor white level, a fixed
  property of the camera.
- The base rendering is fitted to the response of the camera JPEG engine. It lands within
  0.2 EV of the embedded JPEG on the S3 Pro test frames, and 0.35 EV on the one S5 Pro
  test frame.
- Merged DNG files now carry `BaselineExposure`, so raw converters that honor it open
  them at the exposure as shot, with the extra highlight range available for recovery.

### New Develop Controls

- New **Tone**, **Color** and **Detail** groups: Exposure, Contrast, Highlights, Shadows,
  Whites, Blacks, Gamma, Temperature, Tint, Vibrance, Saturation and Sharpening. Ranges
  and behavior follow Adobe Camera Raw. The algorithms are this application's own.
- Editing happens on a linear floating-point image that holds the whole S + R range.
- Highlights and Shadows are locally adaptive, so texture keeps its contrast.
- The tone curve approaches white without reaching it, so no control clips highlights.
- Camera color matrices are now applied, so colors are right without a saturation boost.

### Highlight Reconstruction

- New **Highlight reconstruction** group. Where even R saturates, each capped channel is
  rebuilt from the other two, using a sensor ceiling measured in each file. It can be
  turned off, and **Threshold** and **Neutralize** update live.
- Very bright saturated colors, such as blue lamps, keep their brightness.

### Merge Review

- R photosites sit in the gaps of the S honeycomb, but were merged as if they shared the
  same position. R is now resampled onto the S positions, which cuts the S/R mismatch by
  17% to 40% on the test frames.
- Clip levels now come from each file, and the R gain fit is robust and works per color.
  Gains are about 18 / 16.5 / 15.4 rather than a flat 16.
- The default merge start moved from 62% to 90%, since S is linear up to clipping and has
  about 8 times less noise than R.

### AMaZE And Noise Reduction

- `Amaze debayer` at 6 MP is now the default method. `Reconstruction` remains available
  for 12 MP output and updates faster after a merge change.
- **R noise reduction** now follows the measured noise at each level.
- New **Chroma noise reduction** for the AMaZE preview and its exports.
- Both are fully off at 0, which is the default, so high-ISO highlights show the real
  grain of the R data until you choose otherwise.

### Sharpening

- Sharpening keeps the look of earlier versions: one light luminance pass on the finished
  image, at output size.
- It now eases off in deep shadows and R-sourced highlights, where it would mostly raise
  noise. Midtone texture is sharpened at full strength.

## Notes

- Develop settings from earlier versions used other units and are not carried over: those
  images open with neutral develop settings. Merge settings are kept, so previously
  edited images keep their old merge start.
