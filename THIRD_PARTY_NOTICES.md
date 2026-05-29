# Third-Party Notices

This repository contains the application's own source code and resources. It does not intentionally vendor third-party library source trees or redistribution packages.

Third-party software is used at build time and runtime. Those components keep their own licenses.

This file is a practical compliance note for this repository. It is not legal advice.

## Dependencies Used By This Project

### Qt 6

Used for:

- GUI
- windowing
- image display
- deployment support

Current project usage:

- `Qt6::Widgets`
- transitively deployed Qt runtime modules and plugins needed by the built app

Official licensing references:

- Qt licensing overview: https://doc.qt.io/qt-6/licensing.html
- Qt deployment: https://doc.qt.io/qt-6/deployment.html
- Qt LGPL obligations overview: https://www.qt.io/licensing/open-source-lgpl-obligations?hsLang=en

Current compliance position:

- This project is built and deployed with shared Qt libraries, not static linking.
- The repository ships the application source code, which is helpful for LGPL compliance.
- If you redistribute a built binary that includes Qt DLLs, you must also comply with the Qt license you are using.

Practical rule for this project:

- do not switch to static Qt linking without a separate license review
- if you distribute packaged binaries, include the applicable Qt license texts and notices with the package

### LibRaw

Used for:

- RAF decoding
- raw access
- thumbnail extraction

Official reference:

- https://www.libraw.org/docs

LibRaw states that it is available under:

- `LGPL 2.1`
- `CDDL 1.0`

Current compliance position:

- This repository does not vendor LibRaw source code.
- The application links against an installed LibRaw build.
- If binaries are redistributed with LibRaw DLLs, the redistribution package must include the relevant LibRaw license and attribution information for the licensing route being used.

### LibTIFF

Used for:

- DNG/TIFF writing

Official reference:

- https://www.simplesystems.org/libtiff/

Current compliance position:

- This repository does not vendor LibTIFF source code.
- The application links against an installed LibTIFF build.
- If binaries are redistributed with LibTIFF DLLs, include the LibTIFF copyright and license notice in the redistribution package.

### ExifTool

Used for:

- fallback embedded preview extraction

Official reference:

- https://exiftool.org/

ExifTool states on its homepage that it may be redistributed and/or modified under the same terms as Perl itself.

Current compliance position:

- ExifTool is optional in this project.
- The repository does not bundle ExifTool.
- If a redistribution package starts bundling ExifTool, its license and accompanying requirements must be included with that package.

## What This Repository Intentionally Does Not Ship

- Qt source code
- LibRaw source code
- LibTIFF source code
- ExifTool binaries
- deployment bundles from `build/`

## Repository Hygiene Decisions

To reduce licensing ambiguity:

- unused assets with undocumented provenance should not remain in the repository
- local virtual environments, test outputs, and logs are ignored from source control

## Redistribution Checklist

If you publish source only:

- keep this notice file
- keep the project `LICENSE`
- do not add third-party binaries casually

If you publish Windows binaries:

- document which Qt license path you are using
- include the license texts/notices required by Qt, LibRaw, LibTIFF, and any other bundled dependency
- do not bundle ExifTool unless you also bundle its license terms
- do not remove the user's ability to replace shared Qt libraries when relying on LGPL Qt
