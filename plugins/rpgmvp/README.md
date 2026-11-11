# RPGMVP.pvd

PictureView 3 decoder for RPG Maker MV `.rpgmvp` and RPG Maker MZ `.png_` encrypted PNG and WebP images.
The plugin reconstructs the first 16 image bytes without an encryption key. PNG uses its fixed
signature and IHDR prefix; WebP uses the file size and the unencrypted payload to restore its RIFF
header and first chunk type.

It supports PNG greyscale, RGB, indexed, greyscale-alpha and RGBA images, tRNS transparency,
1/2/4/8/16-bit source depths and Adam7 interlacing. PictureView's UTF-16 entry point receives native
RGB24 or straight RGBA32 for 8-bit-and-shallower sources. Deeper opaque images use RGB48;
transparent images use straight RGBA64. Both preserve every 16-bit sample in little-endian order.
RGB16 decodes directly into the final six-byte pixel buffer; gray16 expands in that same buffer,
without an eight-byte-per-pixel intermediate image. PVD format codes are 25, 33, 49 and 65;
storage is 3, 4, 6 and 8 bytes per pixel. The legacy UTF-8 entry point retains BGR24/BGRA32/BGRA64.
Native output uses the RGB formats documented by the PictureView SDK v1.1 (2026.09.30).
Still WebP images support lossy VP8, lossless VP8L and extended VP8X containers, including alpha.
WebP output is native RGB24/straight RGBA32 or legacy BGR24/straight BGRA32. PNG keeps its existing
stream callback without a full-file copy. The WebP adapter feeds the reconstructed prefix and the
mapped tail separately to libwebp's incremental decoder; libwebp may copy compressed input
internally, while decoded pixels go directly into the final page buffer.

The plugin is built for x64 and x86 with libspng, zlib and libwebp linked statically. The libwebp
decoder-only target has SIMD enabled; its BSD-3-Clause license and patent grant are included in
the package notices.

Limitations: PNG `gAMA`/`cHRM` chunks and ICC colour management are not applied. RPGMVP has no
CICP source signalling beyond the PNG sRGB chunk, so the shared HDR/wide-gamut pipeline does not
change its output. WebP ICC/EXIF/XMP presence comes from the VP8X feature flags; profiles and EXIF
orientation are not applied. Animated WebP is rejected. APNG is deliberately exposed as one still
page containing its default image. The shared resource limits are 268,435,456 pixels total and
32,768 pixels on either side, in addition to each codec's own format limits.

See [DESIGN.md](DESIGN.md) for the boundary design and [fixtures/SOURCES.md](fixtures/SOURCES.md)
for fixture provenance and verified expectations.

## Changes

- 1.3.0 — unpublished candidate: encrypted still WebP support through libwebp, including lossy,
  lossless and extended files with alpha. PNG behavior is preserved.
- 1.2.0 — direct UTF-16 paths and native RGB24/RGBA32/RGB48/RGBA64
  through the wide entry point. Opaque 16-bit pages use 25% less pixel storage with RGB48;
  legacy BGR/BGRA output is preserved.
- 1.1.0 — transparency is now displayed and 16-bit PNGs are delivered to PictureView as 16-bit instead of being reduced to 8-bit.
- 1.0.0 — initial release.
