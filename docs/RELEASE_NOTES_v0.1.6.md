# v0.1.6 Release Notes

Release date: 2026-06-04

This release focuses on two areas:

- better preview tuning controls inside the app
- a more complete RawTherapee handoff workflow for the generated S3 Pro DNG files

## Highlights

### Preview controls

- Added `Preview Gamma` control.
- Added `Preview Contrast` control.
- Added `Preview Saturation` control.
- Added `Preview Highlight Compression` control.
- Added `Export Preview` with JPEG quality and folder selection.
- Updated the gamma slider range to `0.0` to `3.0`.
- Wired preview gamma and contrast settings more cleanly through the CFA preview path.

These are preview-only adjustments intended to make it easier to judge the merged DNG before export. They do not change the exported raw data.

### RawTherapee workflow

- Added a bundled RawTherapee processing profile: `RawTherapee profile\s3pro_dng.pp3`
- Documented how to use that profile as a starting point for the converted `*_sr_merged.dng` files.
- Documented how to configure a RawTherapee dynamic default profile for these files.

The included profile provides basic correction choices for the current workflow, including exposure shaping, tone adjustments, demosaicing, sharpening, and the default crop/rotation used for these files.

### Packaging

- Updated the release packaging script so the generated zip now includes the `RawTherapee profile` folder and the bundled `s3pro_dng.pp3` file.

## Documentation

- Expanded the manual and README to reflect the current RawTherapee-based finishing workflow.
- Clarified that the included RawTherapee profile already applies the default crop and rotation.
- Improved end-user guidance around preview behavior and post-processing.

## Notes

- The stable supported output remains the `6MP Raw CFA DNG` workflow.
- The new preview sliders are for evaluation only and do not alter the exported DNG data.
- If you use the packaged release zip, the RawTherapee profile is now included directly in the archive.
