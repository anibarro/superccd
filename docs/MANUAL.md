# Application Manual

## Purpose

This application converts Fujifilm FinePix S3 Pro `RAF` files into `DNG` files based on a merged `S/R` Super CCD workflow.

The intended final output is the merged file:

- `_sr_merged.dng`

The `_s_pixels.dng` and `_r_pixels.dng` files are diagnostic companions and are also useful for understanding the merge.

## Main Workflow

1. Start the application.
2. Click `Add RAF Files...`.
3. Select one or more `.RAF` files.
4. Select an output folder.
5. Select a RAF file from the list.
6. Click `Update Preview`.
7. Adjust preview and merge settings while viewing the separate preview window.
8. Click `Export Preview` to save the current preview as a JPEG or 16-bit TIFF if needed.
9. Click `Convert` to export the currently previewed file.
10. Click `Convert All` to export every RAF in the list.

## File List

Each RAF entry shows:

- embedded thumbnail, when available
- file name
- camera model
- ISO, shutter speed, and aperture
- focal length and lens model, when available

Available actions:

- `Add RAF Files...`
- `Remove Selected`

`Convert` operates on the file that is currently being previewed, not merely the last selected row.

## Output Files

For an input like:

```text
DSCF0125.RAF
```

the application writes:

- `DSCF0125_6MP_CFA_s_pixels.dng`
- `DSCF0125_6MP_CFA_r_pixels.dng`
- `DSCF0125_6MP_CFA_sr_merged.dng`

The main file is:

- `*_sr_merged.dng`

The merged and per-plane DNG files now carry a populated EXIF IFD with the
camera capture metadata (EXIF version, color space, capture date/time,
ISO, shutter, aperture, focal length) so raw editors and other tools can
read the original capture information from the file.

## Preview Controls

The image preview opens in a separate window from the RAF list and settings.
Resize or move it independently as needed. Its size and position are restored
the next time the application starts. Click `Show Preview` to reopen it after
closing it or to bring it to the front.

### Update Preview

Generates a preview for the selected RAF file.

### Export Preview

Saves the currently rendered preview as a `JPEG` or lossless 16-bit RGB `TIFF`.

- opens a popup to choose the destination folder
- lets you choose between `JPEG` and `16-bit TIFF`
- lets you set JPEG quality from `1` to `100`
- can export at `12 MP` or resized as `6 MP`
- writes JPEG files as `*_preview_12MP.jpg` or `*_preview_6MP.jpg`
- writes TIFF files as `*_preview_12MP.tif` or `*_preview_6MP.tif`
- exports the same preview adjustments currently shown in the app, including shadow/highlight recovery and sharpening
- requires the selected RAF file to have an up-to-date preview first

The preview source is rendered internally at 16-bit precision. Preview exports apply adjustments to that 16-bit source, and TIFF preserves the resulting 16-bit RGB values without JPEG compression. For responsive controls, the on-screen preview is first scaled to the selected zoom and then rendered to 8-bit RGB, so it can show more visible banding than the final `JPEG` or `TIFF` export.

Important behavior:

- the first preview or conversion for a RAF file is slower
- later previews of the same file are faster because the app reuses cached decoded data
- CPU-heavy alignment, interpolation, merge, demosaic, and preview cleanup stages use the available CPU cores
- the independent `S` and `R` shots are decoded concurrently, although an individual LibRaw decode can still contain a short single-core phase

### Update preview automatically

When enabled, parameter changes trigger preview regeneration automatically after a short delay.

### How the image is developed

The preview and the preview exports are developed from a linear floating-point
image, where 1.0 is the point at which the `S` photosites clip and the `R` data
carries on above it, up to about +2.7 EV at ISO 100 and +4 EV at high ISO.

Nothing in the pipeline looks at the image to decide how bright it should be. A
frame opens at the exposure it was shot at: the base rendering was fitted to the
response of the JPEG engine of the camera, and it lands within about 0.2 EV of
the embedded camera JPEG on the S3 Pro test frames. A frame shot two stops under
looks two stops under.

All the data above `S` white stays in the image while you edit. The tone curve
approaches display white without reaching it, so no control can clip the
highlights: pushing them only compresses them.

### Tone

All controls start at 0 (Gamma at 1.00), which is the image as shot. Their ranges
and behavior follow Adobe Camera Raw, but these are this application's own
algorithms, not Adobe's.

- `Exposure`: exposure compensation in stops, from -5 to +5.
- `Contrast`: steepens or flattens the midtones, pivoting on middle gray.
- `Highlights`: negative values bring the bright areas down and reveal the `R`
  detail above `S` white. It follows areas of the image rather than single pixels,
  so texture keeps its contrast as the tones come down.
- `Shadows`: positive values lift the dark areas, also by area. The lift fades out
  below the sensor noise floor, where there is nothing left to recover but noise,
  and fine detail near that floor is held back so lifted shadows stay clean.
- `Whites`: moves the point where tones start rolling off into white.
- `Blacks`: sets the black point. Negative deepens, positive lifts.
- `Gamma`: midtone gamma, as in Levels. 1.00 leaves the image unchanged and higher
  values brighten the midtones. It does not move black or white.

### Color

The image is converted from camera color with the color matrix of the camera, so
neutral settings give natural colors. As in a camera, the tone curve works on each
channel separately, so bright saturated colors keep their strength and shift toward
yellow or white as they fill up.

- `Temperature`: the color temperature of the light, in Kelvin. Each photo opens at
  the value of the camera; higher values make the image warmer. The slider moves evenly
  in perceived color. `As Shot` returns to the camera's white balance.
- `Tint`: relative to the white balance as shot, and positive is more magenta.
- `WB picker`: turn it on, move the box over something neutral, use the mouse wheel
  to resize the box, and left-click to set Temperature and Tint.
- `Vibrance`: raises muted colors the most and protects skin tones.
- `Saturation`: scales all colors evenly, at constant lightness.

### Highlight reconstruction

Where even the `R` photosites saturate, on the sun, a bright cloud, a lamp or a strong
reflection, each channel stops at its own ceiling and the white balance pulls them
apart, which turns those highlights pink. The ceiling is measured in each file. Each
capped channel is rebuilt from the other two, and areas with two or more capped
channels fade toward neutral. Colors below their ceilings are real data and stay as
they are, however bright. The controls update live and do not re-run the merge.

- `Rebuild saturated highlights`: turn it off to see what the sensor recorded.
- `Threshold`: how close to the ceiling, in percent, the repair begins. Lower it if
  pink edges remain, which can happen on frames where no ceiling could be measured.
  Raise it if bright colors near the ceiling lose saturation.
- `Neutralize`: how far areas with two or more saturated channels go toward white.
  0 keeps the recorded hue, which for a blue lamp is cyan. 100 gives pure white.

### Detail

`Sharpening` is one light luminance pass on the finished image, at output size and
as the very last step, so colors are not touched and it shows at every zoom level.
6 MP exports are resized first and sharpened afterwards.

It eases off in the two places where it would mostly raise noise: the deep shadows,
and highlights that come from the noisier `R` photosites. Midtone texture is sharpened
at full strength.

### Correct isolated light/dark pixels

Corrects only strongly isolated light or dark pixels. Other pixels stay unchanged. It
affects the live preview and preview exports, but not the DNG.

### Preview Rotation

Rotates the preview display for orientation checking.

- Normal, Rotate 90 CW, Rotate 180, Rotate 90 CCW
- does not affect export
- useful for checking image orientation before opening in RawTherapee

### Preview Method

Selects how the merged S/R CFA is turned into the preview and preview exports.

- `Amaze debayer` is the default. It demosaics with the AMaZE algorithm at the native
  6 MP of the sensor and keeps fine color detail best. Previews and exports are `6 MP`.
- `Reconstruction` renders on the `12 MP` diagonal grid and exports at `12 MP` or
  `6 MP`. It takes about half the time to update after a merge change.

The app starts on `Amaze debayer` each time. The method only affects the preview and
preview exports; it does not change the `6MP Raw CFA DNG` conversion.

### Preview zoom

Controls the displayed preview scale.

- wheel zoom is supported
- drag with left mouse button to pan
- zoom keeps the viewport center stable

### Drag and Drop

You can drag RAF files directly onto the file list to add them. The application accepts `.raf` files dropped anywhere on the list area.

## Exposure Tools

The application ships a dedicated **Exposure Tools** window that hosts in-app
**Histogram** and **Waveform** overlays for the live preview. Enable the
window with the **Show Exposure Tools** checkbox in the main window. It
opens as an independent top-level window, can be moved and resized freely,
and is restored at its last size and position on the next launch.

The Exposure Tools window has two tabs:

- **Histogram** - a classic RGB + luma histogram for the preview image.
- **Waveform** - a classic waveform monitor for the preview image.

Both tools are metered from the same image source the on-screen preview is
built from, so they stay in sync with the visible preview adjustments
(exposure, white balance, gamma, contrast, shadows, etc.).

### Meter visible area only

A checkbox at the bottom of the Exposure Tools window toggles between:

- **Full preview image** (default) - the tools meter the full preview image.
- **Visible area only** - the tools meter only the sub-rect currently
  visible in the Preview window (taking the current zoom and scroll
  position into account).

The "Visible area only" mode is useful when you have zoomed in on a small
part of the preview and want to check the histogram or waveform of just
that part of the image.

### Histogram tab

The histogram shows the per-channel R, G, B, and luma (Rec. 709 weighted)
distribution of the metered image, binned into 256 buckets per channel.

A **Histogram mode** dropdown in the toolbar selects the visualization:

- **All** - a single plot with the R, G, B, and luma curves overlaid.
- **RGB split** - three side-by-side plots, one per RGB channel.
- **Luma** - a single plot with the luma channel only.

Per-channel histograms are drawn in log space so shadow detail stays
visible even when the image has bright highlights. The histogram is
sampled in 8-bit-per-channel form (matching what you see on screen).

### Waveform tab

The waveform shows, for each column of the metered image, how many pixels
in that column landed in each `[0, 255]` bin, drawn with brightness
proportional to the count. This makes it easy to spot crushed shadows,
clipped highlights, and channel imbalance at a glance.

A **Waveform mode** dropdown in the toolbar selects the visualization
layout (same three modes as the histogram: **All**, **RGB split**, **Luma**).

A **Transparency** slider and spin box in the toolbar let you dial the
waveform's opacity. 0% is fully opaque, 100% is fully transparent. The
slider is useful when the waveform is on top of a busy preview and you
want to peek through it.

The waveform uses a per-column peak reference instead of a single global
peak, so a single bright column does not dim every other column in the
trace. Each column self-normalizes.

### Performance

The exposure tools are designed to stay cheap to update on every preview
redraw. They short-circuit the (relatively expensive) per-pixel sampling
pass when the cached source image and visible rect have not actually
changed, so adjusting a slider on the main window does not force a
full histogram + waveform recompute on every tick.

The tools are off by default. Enable them with the **Show Exposure Tools**
checkbox in the main window; their open/closed state is persisted across
launches.

## Merge Controls

These controls affect the exported `6MP Raw CFA` result.

The `Transition Settings` group is collapsible. Click the group title to
hide or show the curve graph and the merge sliders. The collapsed/expanded
state is remembered between sessions.

### Merge start

The `S` brightness (as a percentage of the channel clip level) at which the
`S -> R` merge begins. Pixels darker than this are kept as pure `S`.

The default is 90%. `S` stays linear right up to clipping and has about 8 times less
noise than `R` at the same brightness, so a later start gives cleaner highlights.

Use it when:

- you want to push the merge later (higher value) so `S` is preserved
  longer into the highlights
- you want to bring `R` in earlier (lower value) to recover highlights sooner

The slider range is 0 to 100, but it is **non-linear**: positions 0..80
map linearly to start = 0..0.9985, and positions 80..100 use a smoothstep
curve that lets you reach start = 1.0 (no merge at all, pure `S`) with
fine control. The last 20% of the slider travel covers the last 0.15% of
S brightness — this is the range where the most useful fine-tuning lives,
because in practice the merge window either covers the clipped highlights
or it doesn't, and the meaningful change is concentrated in a very thin
slice at the top of the brightness range.

### Transition width

The width of the `S -> R` transition in `S` brightness units.

Use it when:

- `R` is too noisy at high ISO and you want a shorter, sharper handoff
- you want a longer, gentler blend between `S` and `R`

The slider range is 1 to 100. 1 is the narrowest transition the UI allows
(a small but non-zero blend region, so the merge curve is always visible
in the graph). 100 is the widest transition the algorithm allows. The end
of the transition is always clamped to stay within the valid `S` brightness
range, so the slider stays meaningful across its full range.

The transition width and the merge start work together smoothly. As the
merge start approaches 100% the window simply narrows toward the top of
the S brightness range; there is no abrupt cutoff.

### R noise reduction

Smooths the `R` pixels before merging, scaled to their measured noise. `R` has about 8
times the noise of `S`, so recovered highlights are grainy at high ISO. At 0 nothing is
filtered, which is the default. Higher values give cleaner, softer highlights. It only
touches pixels where `R` is used, and it also applies to the DNG.

### Chroma noise reduction

Smooths color noise in noisy areas, meaning shadows and, at high ISO, `R` highlights.
Clean detail keeps its color. At 0 nothing is filtered, which is the default. It only
affects the `Amaze debayer` preview and its exports, not the DNG.

### Smoothness

Eases the shape of the merge inside the transition width.

Use it when:

- the highlight transition looks too abrupt
- you want a more natural rolloff

0 produces a straight line. 100 produces a sharp exponential ramp that
approaches a 90 degree shoulder (ease-in shape).

The current defaults are intentionally tuned around a stable working result. Do not expect them to behave like generic HDR sliders.

### Transition Curve

A small graph drawn above the merge sliders that visualizes the live
`S -> R` merge curve used by the export pipeline.

- The horizontal axis is the normalized `S` brightness (0..1).
- The vertical axis is the `R` blend weight (0..1) actually applied to
  each `S` value.
- The graph is recomputed from the current `Merge start`, `Transition width`
  and `Smoothness` values and matches the exact math used by
  `SuperCCDProcessor`, so what you see is what gets written into the
  exported CFA DNG.
- A red marker on the `S` axis shows the current `Merge start` position
  to make it easy to see how close the merge window is to clipping.

Use it to find the right operating point for the merge sliders, especially
near the top of the `S` brightness range where the non-linear slider
mapping makes small changes meaningful.

### Export S/R Planes

When enabled, exports individual S and R plane DNG files alongside the merged result.

- `*_s_pixels.dng` - the S (primary/highlight) sensor response
- `*_r_pixels.dng` - the R (secondary/shadow) sensor response

These files are useful for:

- Understanding the merge behavior
- Experimenting with alternative blending approaches
- Diagnostic purposes

The main output file `*_sr_merged.dng` is always exported regardless of this setting.

## Defaults

### Reset Defaults

Restores built-in defaults for the UI parameters.

### Save Current As Default

Stores the current GUI values with `QSettings` so they load next time.

Saved values include:

- `Merge start`
- `Transition width`
- `Smoothness`
- the Tone, Color and Detail controls
- preview method
- preview rotation
- auto-preview state

## Recommended Post-Processing

1. Open `*_sr_merged.dng` in RawTherapee.
2. Apply the included `RawTherapee profile\s3pro_dng.pp3` processing profile.
3. Use the demosaic settings that work best for this workflow.
4. Adjust exposure manually as needed.
5. Finish the image there.

Important:

- the exported DNG is currently rotated by design
- final orientation and crop are part of the expected RawTherapee workflow

### Included RawTherapee Profile

The repository includes a RawTherapee processing profile here:

- `RawTherapee profile\s3pro_dng.pp3`

This is a basic correction profile intended specifically for the merged S3 Pro DNG output. It provides a starting point for:

- exposure and highlight handling
- white balance
- tone equalizer shaping
- crop and rotation
- demosaicing
- sharpening

It is a baseline profile, not a final look. You should still adjust each image as needed.
Its default crop and rotation are already built into the profile.

### Applying The Profile Manually

1. Open `*_sr_merged.dng` in RawTherapee.
2. Load `s3pro_dng.pp3`.
3. Review exposure, crop, rotation, and color before exporting.

### Setting A Dynamic Default Profile In RawTherapee

If you want RawTherapee to assign this profile automatically when these files are opened:

1. Make sure `s3pro_dng.pp3` is available from RawTherapee as a processing profile.
2. In RawTherapee, open `Preferences`.
3. Set the default processing profile for raw images to `(Dynamic)`.
4. Open `Dynamic Profile Rules`.
5. Add a new rule and attach `s3pro_dng.pp3`.
6. Set the `Camera` field so it matches the metadata shown by RawTherapee for these converted S3 Pro DNG files.
7. Optionally add other conditions only if you want to narrow the rule further.
8. Order the rules intentionally, because later matching rules override earlier ones.
9. If files were already browsed before creating the rule, select them in the file browser and run `Processing Profile Operations > Reset to Default`.

According to RawPedia, dynamic rules are evaluated from top to bottom, all matching rules are combined, and later ones can override earlier ones:

- https://rawpedia.rawtherapee.com/Dynamic_processing_profiles

## Important Behavior

- The merged DNG stores the full `S + R` range and is scaled to fit 16 bits.
- Its `BaselineExposure` tag tells raw converters where `S` white is, so it opens at
  the exposure as shot with the extra highlight range available for recovery.
- Develop settings made in versions before 2.0 used different units and are not
  carried over. Their merge settings are kept.

## Known Limitations

- The application is specialized for Fujifilm S3 Pro RAF files.
- The GUI preview is approximate and optimized for interaction, so it can show more visible banding and lower apparent quality than the final `JPEG` or `TIFF` export.
- The repository still contains experimental code paths that are not part of the recommended workflow.

## Troubleshooting

### Preview is dark

The preview shows the exposure as shot, so an underexposed frame looks underexposed.
Raise `Exposure` or `Shadows`.

### Thumbnail says `No thumb`

The application could not extract the embedded RAF preview with the available extraction path.

### Convert does nothing

Make sure:

- a RAF file has been previewed first
- an output folder is selected

### The first preview or conversion is much slower than the next ones

That is normal.

The application caches decoded `S`, `R`, and projected merge data after the first pass for a given RAF file. The first preview or conversion pays that cost. Later operations on the same file are faster.

During the first pass, the application divides its own image-processing stages
across the available CPU cores and decodes the independent `S` and `R` shots
concurrently. LibRaw may still use one core during parts of each individual
decode, so CPU usage does not necessarily remain at 100 percent for the entire
operation.

### The merged DNG opens darker than expected

Files converted by versions before 2.0 have no `BaselineExposure`, so they open dark.
Convert again, or adjust the exposure in the raw editor. A raw editor that ignores
`BaselineExposure` will also show the file dark.

## Frequently Asked Questions

### What is Super CCD SR II?

The Fujifilm Super CCD SR II is a sensor technology found in the FinePix S3 Pro camera. It uses two photodiodes per pixel site - a primary "S" (Sensitivity) diode optimized for highlight capture and a secondary "R" (Range) diode optimized for shadow detail. This application merges the separate S and R captures into a single usable raw file.

### Why does the output look dark?

The merged DNG holds the full highlight range and uses `BaselineExposure` to open at the exposure as shot. No dynamic range is given up for this.

### Why is the image rotated?

The S3 Pro captures images in a rotated orientation internally. The exported DNG retains this rotation. Rotate and crop the image in RawTherapee as part of your standard workflow.


### Can I use files from other cameras?

No. This application is specifically designed for Fujifilm FinePix S3 Pro RAF files. Files from other cameras will not work.

### Where are my saved settings stored?

Settings are stored in your user profile via Qt's QSettings. On Windows, this is typically in the Registry under `HKEY_CURRENT_USER\SuperCCD\superccd2dng`.

### What is the Export S/R Planes option?

When enabled, this exports the individual S and R sensor responses as separate DNG files. These are useful for understanding the merge behavior or experimenting with alternative blending methods.

### Can I convert files from the command line?

Yes. Use: `superccd2dng.exe input.raf output.dng --6mp-cfa`

To display the installed program version without opening the GUI, use:

`superccd2dng.exe --version`

The short form `superccd2dng.exe -v` is also supported.

### What does the Merge start do?

This controls the S brightness at which the R (shadow) sensor starts being
blended with the S (highlight) sensor. Higher values let S clip more
before R takes over. Lower values bring R in earlier.

### What does the Transition width do?

This controls how wide the S to R transition is, measured in S brightness
units. The slider has a minimum of 1 so there is always a real (if narrow)
merge region; the end of the transition is always clamped to stay within
the valid S brightness range.

### What does the Smoothness do?

This controls how gradual the S to R transition is. Higher values create a smoother, more gradual blend. Lower values create a sharper transition.
