# EXR.pvd

OpenEXR decoder plugin for PictureView 3 (the image viewer plugin for Far Manager 3 by Pavel
Skakov), built for both the x64 and the x86 (32-bit) Far Manager. It is the `exr` plugin of the
deckodeck monorepo (see the top-level `docs/BUILD.md` for the build) and decodes with OpenEXR
3.4.13 (its C core), Imath 3.2.2, libdeflate 1.25 and OpenJPH 0.30.1, all linked statically: the
finished `EXR.pvd` imports `KERNEL32.dll` and nothing else, so it needs no runtime, no WIC codec and
no GDI+.

What it does:

- `.exr` and `.sxr` files, scanline or tiled (level 0 of mip/rip maps), any line order, every
  compression scheme: none, RLE, ZIPS, ZIP, PIZ, PXR24, B44, B44A, DWAA, DWAB, HTJ2K.
- Channels `R`, `G`, `B` (+ `A`); `Y` with `RY`/`BY` chroma (reconstructed with the library's own
  filters) or `Y` alone; otherwise the first layer with `R`, `G`, `B`; otherwise the first channel
  as grey. HALF and FLOAT samples; UINT channels are not colour.
- Multi-part and stereo files: one part is shown - the first with colour channels, the `left` view
  when views are named; deep parts are skipped.
- The display window is the picture; the data window is composited into it (cropped outside,
  black or transparent black where there is no data).
- Colour: scene-linear light with 1.0 = 100 nit (or the file's `whiteLuminance`), the file's
  primaries (Rec.709, Rec.2020, P3-D65, ACES AP0/AP1 by matching, any other chromaticities by a
  run-time matrix; `colorInteropID` as the fallback) converted to sRGB, highlights compressed with
  BT.2390 tone mapping from the picture's own 99.99th-percentile peak. Every page is handed over
  as 16-bit BGRA; premultiplied alpha becomes straight alpha.
- Files from archives and virtual panels (the host hands over the whole file in memory).

## Install

Copy `EXR.pvd` next to `0PictureView.dll` in the PictureView installation (typically
`%FARPROFILE%\Plugins\PictureView` or `<Far>\Plugins\PictureView`) and restart Far. The plugin
registers with priority 10 and is picked up by PictureView's automatic format detection. Install
the x86 DLL next to the 32-bit `0PictureView.dll`; the two DLLs are not interchangeable. The build
and the scripts never touch the Far Manager installation.

## Where things are

```
plugins/exr/
  CMakeLists.txt          identity (EXR <version>, priority 10), exr_core / exr_adapter /
                          exr_composition, pvdkit_add_plugin(exr ...), the license ports
  src/core/               Channels, Parts, Windows, Composite, Encode, Colour, Describe: every
                          decision, without the library
  src/adapters/exr/       Context (OpenEXRCore over the file bytes), Pixels (the chunk loop),
                          LuminanceChroma (RgbaYca), Decoder / DecoderFactory
  src/DefaultPlugin.cpp   pvd::makePlugin(): FileSource + DecoderFactory + Describer + CodecPlugin
  tests/core tests/adapters tests/e2e tests/support   exr_core_tests, exr_adapter_tests,
                          exr_e2e_tests, the reference pipeline and the generated reference pixels
  fixtures/               sample files + SOURCES.md (origin, license, expected values, SHA-256)
  scripts/                fetch-fixtures.ps1, openexr-images.sha256, make-synthetic-fixtures.ps1
                          (+ make_synthetic_fixtures.py, run through uv)
  package/                static English/Russian readmes and ChangeLog for the zip
  DESIGN.md               the plugin's design; the shared design is docs/ARCHITECTURE.md
```

The plugin is written to `build/<preset>$env:PVDKIT_BUILD_SUFFIX/plugins/exr/EXR.pvd`;
`scripts/pack.ps1` produces `EXR-<version>-x64.zip` and `EXR-<version>-x86.zip` with the plugin,
static distribution documents and dependency licenses.

## Tests and fixtures

- `exr_core_tests` - channel and part selection, window layout and compositing, the PQ encoding
  and the percentile peak, chromaticity matching, the describer (every branch).
- `exr_adapter_tests` - `exr::Decoder` on every fixture (header facts and info line, PQ codes at
  seven positions of every accepted fixture against an independent double-precision reference,
  luminance/chroma files against the library's own `Imf::RgbaInputFile` on every pixel, every
  rejection and its category, the size limits, decoding into caller buffers), the composed
  production plugin, and the unit-level leak gate.
- `exr_e2e_tests` - loads the built `EXR.pvd` with `LoadLibraryW`, resolves the eight exports and
  drives them like the host: every fixture from disk and from memory, host pixels against the
  reference pipeline, pinned whole-image hashes, callback abort, out-of-range pages, two files at
  once, rejections of non-EXR and malformed input, behaviour after `pvdExit`, four threads decoding
  concurrently; a skipped 4K timing case (`--no-skip=true -tc="4K timing*"`).
- `exr_sequence_tests`, `exr_leak_tests` (with the hostile corpus), `exr_check_imports`,
  `exr_check_exports`, `exr_package_docs` - the shared gates.

Fixtures live in `fixtures/` and are documented in `fixtures/SOURCES.md`: 19 files pinned from
`AcademySoftwareFoundation/openexr-images` (BSD-3-Clause; `scripts/fetch-fixtures.ps1` re-downloads
them against `scripts/openexr-images.sha256`) and 53 synthetic files generated by
`scripts/make-synthetic-fixtures.ps1` with the OpenEXR 3.4.13 Python binding (CC0-1.0), which also
writes the reference pixel table the adapter and e2e tests pin against.

## Known limitations

- Deep images are not shown (deep parts are skipped; a deep-only file is refused).
- Multi-part files show one part, stereo files the left view; other parts, views and layers are
  not reachable (multi-part as pages is a documented future extension).
- Mip/rip maps show level 0; the `preview` attribute and the pixel aspect ratio are ignored
  (PictureView shows square pixels).
- No exposure or tone controls: the picture is shown at 1.0 = 100 nit (or `whiteLuminance`) with
  BT.2390 from its own peak, by design.
- The whole picture is decoded when the file is opened (the tone-mapping peak needs every pixel);
  the session keeps the decoded overlap of the data and display windows (8 bytes per pixel) while
  it is open. RGB and tiled parts are read one chunk at a time and cropped to the display window,
  so a data window far larger than the display window costs only the chunks it touches;
  luminance/chroma (Y/RY/BY) parts are reconstructed over the whole data window (about 40 bytes
  per data-window pixel while the file opens), so for them the data window's area is held to the
  same 16384 × 16384 limit as the display window's.
- A `chromaticities` attribute no colour derivation can use (a white with y = 0, collinear
  primaries, non-finite values) is ignored: the picture is shown as Rec.709 and the info line says
  `linear Rec.709 assumed, unusable chromaticities (...)`. CIE XYZ files (the primaries at the
  XYZ axes, white E) are converted without chromatic adaptation, as OpenEXR's own viewer does.
- Files are mapped with `FILE_SHARE_WRITE | FILE_SHARE_DELETE` so Far can keep working with them.
  If another process truncates a file while a page is being decoded, reading the mapped view
  raises a structured exception that the C++ firewall cannot catch - the same behaviour as the
  bundled decoders that map files.
- Images are limited to 16384 × 16384 pixels in area (268 megapixels) and 32768 pixels per side,
  tiles to 4096 pixels per side.

## Changes

- 1.0.0 — initial release.
