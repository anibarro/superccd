# Release Notes v2.0.1

Release date: 2026-11-06

This release brings colors much closer to the camera rendering and makes white balance
easier to control.

## Highlights

### Camera-Like Color

- The tone curve now works on each channel separately, as in a camera. Bright saturated
  colors such as sunsets keep their strength and shift toward yellow, instead of turning
  pale.
- Very bright lamps render with white cores and a colored glow.
- Saturation now follows the embedded camera JPEG closely on ordinary frames.

### Temperature In Kelvin

- **Temperature** is now set in Kelvin, from 2000 K to 20000 K, and each photo opens at
  the value of the camera.
- The slider moves evenly in perceived color, which makes it much less sensitive.
- A new **As Shot** button returns to the white balance of the camera.
- The white balance picker sets the Kelvin value.

## Notes

- Temperature adjustments saved with 2.0.0 are not carried over: those images open with
  the white balance as shot.
- Images look different from 2.0.0 at the same settings, because of the new tone curve.
