# Contributing

## Current Development Policy

This repository is not a general greenfield app. It is a reverse-engineered workflow around one camera and one stable export path.

The current stable path is:

- `6MP Raw CFA DNG`

Treat that path as the product.

## Before Changing Anything

Understand these constraints first:

- highlight preservation is more important than bright default rendering
- regressions in the `6MP` merge are unacceptable unless there is clear evidence of improvement
- experimental 12MP code is not the reference workflow

## Expected Areas For Contributions

- documentation
- code cleanup that does not alter behavior
- metadata improvements for the stable CFA DNG path
- GUI quality-of-life improvements
- build and packaging improvements
- reproducible testing around known RAF samples

## Areas That Need Extra Caution

- `mergePrimaryAndProjectedSecondary(...)`
- any `S/R` handoff behavior
- CFA layout metadata
- anything that changes the stable merged DNG output

If you touch those areas, provide:

- exact rationale
- before/after evidence
- sample RAFs used
- visual comparison notes

## Suggested Review Standard

For image-pipeline changes, do not rely only on code inspection.

Use at least:

- one normal sample
- one high-ISO sample
- one highlight-stress sample

Check:

- highlight recovery
- false color
- transition smoothness between `S` and `R`
- shadow noise behavior
- RawTherapee interoperability

## Build

Primary local build command in this repository:

```powershell
cmd /c build_windows.cmd build
```

GitHub release zip:

```powershell
cmd /c build_windows.cmd package
```

## Coding Notes

- Prefer conservative edits over broad rewrites.
- Keep ASCII unless there is a real reason not to.
- Do not remove experimental history unless it is clearly dead and risk-free.
- Do not mix behavioral changes with cosmetic refactors.

## Documentation Expectations

If you change user-visible behavior, update:

- [README.md](README.md)
- [docs/MANUAL.md](docs/MANUAL.md)

## Authorship Note

This repository explicitly documents that the implementation was produced entirely through AI-driven development sessions. Preserve that statement unless the project policy changes.
