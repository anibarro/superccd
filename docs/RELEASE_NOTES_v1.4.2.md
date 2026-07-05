# Release Notes v1.4.2

Release date: 2026-07-05

This release builds on `v1.4.1` with a new **Exposure Tools** window that adds in-app **Histogram** and **Waveform** overlays for the live preview, plus a stack of performance work that keeps the new overlays cheap to update on every preview redraw.

## Highlights

### New Exposure Tools Window

- Added a dedicated top-level **Exposure Tools** window that hosts the new histogram and waveform overlays.
- The window is opened via a new **Show Exposure Tools** checkbox in the main window and behaves like the existing floating Preview window: it can be moved, resized, and closed independently, and its state is remembered across launches.
- A new **Meter visible area only** checkbox at the bottom of the Exposure Tools window lets you switch between metering the full preview image and metering only the sub-rect currently visible inside the Preview window (taking the current zoom and scroll position into account).

### Histogram Overlay

- New `HistogramWidget` that displays a classic RGB + luma histogram for the current preview image.
- Supports three visualization modes selectable from a toolbar:
  - **All** - a single plot with the R, G, B, and luma curves overlaid.
  - **RGB split** - three side-by-side plots, one per RGB channel.
  - **Luma** - a single plot with the luma channel only.
- Uses Rec. 709 luma weights so the luma curve matches what the eye actually perceives.
- Per-channel histograms are drawn in log space so shadow detail stays visible even when the image has bright highlights.
- The histogram is sampled in 8-bit-per-channel form (matching what the user actually sees on screen) and binned into 256 buckets per channel.

### Waveform Monitor

- New `WaveformWidget` that displays a classic waveform monitor. For each column of the source image we count, for each bin in `[0, 255]`, how many pixels in that column landed in that bin. The result is drawn with brightness proportional to the count.
- Supports the same three visualization modes as the histogram (**All**, **RGB split**, **Luma**), selectable from a toolbar dropdown.
- Includes a **Transparency** slider and spin box in the waveform tab (0% = fully opaque, 100% = fully transparent) so the waveform can be dialed back over a busy preview.
- Uses a per-column peak reference instead of a single global peak, so a single bright column does not dim every other column in the waveform. Each column self-normalizes.
- Includes a global-peak-aware clipping/crush indicator on the side so the red/blue bar still shows a meaningful readout when the per-column reference is small.

### Visual Polish

- The histogram and waveform visualizations were retuned for a cleaner, more legible look: tighter line weights, a more readable color palette, and consistent grid + axis decorations between the two tools.
- The waveform now uses per-column normalization so bright columns no longer wash out the rest of the trace.
- The histogram's luma curve is drawn consistently with the waveform's luma reference, so the two overlays agree about the image's brightness distribution.

### Performance

The exposure tools ship with a stack of performance fixes that keep the new overlays from dominating preview redraws, especially on the Raspberry Pi:

- The exposure tools short-circuit the (relatively expensive) per-pixel sampling pass when the cached source image and visible rect have not actually changed. This avoids a full histogram/waveform rebuild on every preview-control tick when the only thing that changed was, e.g., a scroll or zoom that does not change the metered rect.
- The waveform uses a cached identity of the source data (pointer, size, format) and only re-samples when the cache is no longer valid. This is the same kind of fingerprint cache the histogram already had.
- The MainWindow caches the most recent source image and visible rect and only re-pushes to the exposure tools when something actually changed, so adjusting a slider no longer forces a histogram + waveform recompute on every tick.
- The HistogramWidget and WaveformWidget now re-derive their peak references lazily and only when the data has actually been re-sampled.
- The auto-preview timer and the histogram/waveform recompute paths are wired up so the new tools do not introduce visible lag when dragging sliders on the Raspberry Pi.

## Notes

- `v1.4.2` is a quality-of-life release focused on the new Exposure Tools. The headline change is the addition of the histogram and waveform overlays; everything else is cleanup and performance.
- The exposure tools are off by default. Enable them with the new **Show Exposure Tools** checkbox in the main window. Their state is persisted across launches.
- The performance work is largely transparent: existing workflows continue to work, but the new overlays are noticeably cheaper to update on slow hardware (Raspberry Pi) and on fast hardware (desktop) they are essentially free.
- The new tools only affect the live preview path. They do not change the raw DNG conversion path, the EXIF metadata written to DNGs, the AMaZE preview path, or any of the preview export behavior.
