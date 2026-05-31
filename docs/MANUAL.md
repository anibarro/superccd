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
7. Adjust preview and merge settings if needed.
8. Click `Convert` to export the currently previewed file.
9. Click `Convert All` to export every RAF in the list.

## File List

Each RAF entry shows:

- embedded thumbnail, when available
- file name

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

## Preview Controls

### Update Preview

Generates a preview for the selected RAF file.

Important behavior:

- the first preview or conversion for a RAF file is slower
- later previews of the same file are faster because the app reuses cached decoded data

### Update preview automatically

When enabled, parameter changes trigger preview regeneration automatically after a short delay.

### Preview Exposure

Preview-only control.

- does not affect export
- useful because the highlight-safe raw opens dark by design

### Preview Gamma

Preview-only gamma correction adjustment.

- range: 0.0 to 3.0 (default 2.2)
- lower values brighten shadows, higher values increase contrast
- does not affect export

### Preview Contrast

Preview-only contrast adjustment.

- range: -200 to +200 (default 0)
- negative values reduce contrast, positive values increase contrast
- does not affect export

### Preview Saturation

Preview-only saturation adjustment.

- range: -100 to +100 (default 0)
- negative values desaturate, positive values increase color intensity
- does not affect export

### Preview White Balance

Preview-only white-balance bias.

- negative values cool the preview
- positive values warm the preview
- does not affect export

### Preview Tint

Preview-only tint adjustment for green channel balance.

- negative values shift toward magenta
- positive values shift toward green
- does not affect export

### Preview Rotation

Rotates the preview display for orientation checking.

- Normal, Rotate 90 CW, Rotate 180, Rotate 90 CCW
- does not affect export
- useful for checking image orientation before opening in RawTherapee

### Preview zoom

Controls the displayed preview scale.

- wheel zoom is supported
- drag with left mouse button to pan
- zoom keeps the viewport center stable

### Drag and Drop

You can drag RAF files directly onto the file list to add them. The application accepts `.raf` files dropped anywhere on the list area.

## Merge Controls

These controls affect the exported `6MP Raw CFA` result.

### R handoff delay

Moves the point where `R` starts taking over later in highlights.

Use it when:

- `R` is too noisy at high ISO
- you want to let `S` clip a little more before handing off

### R transition smoothness

Controls how wide and gradual the `S -> R` transition is.

Use it when:

- the highlight transition looks too abrupt
- you want a more natural rolloff

The current defaults are intentionally tuned around a stable working result. Do not expect them to behave like generic HDR sliders.

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

- `R handoff delay`
- `R transition smoothness`
- preview exposure
- preview white balance
- preview tint
- preview rotation
- auto-preview state

## Recommended Post-Processing

1. Open `*_sr_merged.dng` in RawTherapee.
2. Rotate the image to the correct orientation.
3. Crop the frame as needed.
4. Use the demosaic settings that work best for this workflow.
5. Adjust exposure manually as needed.
6. Finish the image there.

Important:

- the exported DNG is currently rotated by design
- final orientation and crop are part of the expected RawTherapee workflow

## Important Behavior

- The merged DNG is intentionally highlight-safe.
- That means the raw may open darker than a normal camera raw.
- This is a deliberate tradeoff to preserve highlight recovery.

## Known Limitations

- The application is specialized for Fujifilm S3 Pro RAF files.
- The GUI preview is approximate and optimized for interaction, not for final judging.
- The repository still contains experimental code paths that are not part of the recommended workflow.

## Troubleshooting

### Preview is dark

Increase `Preview exposure`.

### Thumbnail says `No thumb`

The application could not extract the embedded RAF preview with the available extraction path.

### Convert does nothing

Make sure:

- a RAF file has been previewed first
- an output folder is selected

### The first preview or conversion is much slower than the next ones

That is normal.

The application caches decoded `S`, `R`, and projected merge data after the first pass for a given RAF file. The first preview or conversion pays that cost. Later operations on the same file are faster.

### The merged DNG opens darker than expected

That is normal for the highlight-safe workflow. Adjust exposure in the raw editor.

## Frequently Asked Questions

### What is Super CCD SR II?

The Fujifilm Super CCD SR II is a sensor technology found in the FinePix S3 Pro camera. It uses two photodiodes per pixel site - a primary "S" (Sensitivity) diode optimized for highlight capture and a secondary "R" (Range) diode optimized for shadow detail. This application merges the separate S and R captures into a single usable raw file.

### Why does the output look dark?

The merged DNG is intentionally highlight-safe. It preserves highlight detail by keeping the image darker overall, expecting you to adjust exposure in your raw processor. This is a deliberate tradeoff for improved dynamic range.

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

### What does the R handoff delay do?

This controls where in the highlight range the R (shadow) sensor starts being blended with the S (highlight) sensor. Higher values let S clip more before R takes over. Lower values bring R in earlier.

### What does the R transition smoothness do?

This controls how gradual the S to R transition is. Higher values create a smoother, more gradual blend. Lower values create a sharper transition.
