# Release Notes v1.3.0

Release date: 2026-06-14

This release builds on `v1.2.0` with a focused round of multicore performance work. The processing pipeline now scales more consistently with the available CPU cores, and the first preview or conversion of a RAF file spends less time in serial setup work.

## Highlights

### Parallel Processing Across The Pipeline

- Added a shared worker scheduling utility in `src/ParallelProcessing.h` that splits row-based and range-based work across the available CPU cores.
- Moved more CPU-heavy stages in `src/SuperCCDProcessor.cpp`, `src/CfaPlaneAlignment.cpp`, `src/MainWindow.cpp`, and `src/PreviewImageProcessing.cpp` onto the shared parallel execution path.
- Expanded multicore execution across CFA alignment analysis, S/R merge work, preview image generation, and display-oriented image cleanup.

### Faster First-Pass RAF Processing

- Updated the cache-building path to decode the independent `S` and `R` shots concurrently before alignment and merge.
- Reduced the startup cost of the first preview or first conversion for a RAF file on systems with multiple cores.
- Kept the existing cache reuse behavior, so repeated work on the same RAF file remains faster than the first pass.

### Threading Reliability And Test Coverage

- Added an explicit `Threads` dependency in `CMakeLists.txt` for the application and standalone test targets.
- Added `src/tests/ParallelProcessingTest.cpp` to verify that parallel range scheduling visits each item once and correctly propagates worker exceptions.
- Switched timestamp formatting in concurrent raw-read paths to thread-safe local time helpers on Windows and POSIX platforms.

## Notes

- `v1.3.0` is primarily a performance and responsiveness release; it does not change the stable `6MP Raw CFA DNG` output path.
- The multicore improvements are most visible during the first preview or conversion of a RAF file, when the app still needs to decode and populate its caches.
