# RPGMVP.pvd

PictureView 3 decoder for RPG Maker MV `.rpgmvp` and RPG Maker MZ `.png_` encrypted PNG images.
The plugin reconstructs the fixed first 16 PNG bytes in a libspng stream callback, then reads the
remaining bytes directly from PictureView's mapped file. No encryption key and no full-file copy are
needed.

It supports PNG greyscale, RGB, indexed, greyscale-alpha and RGBA images, tRNS transparency,
1/2/4/8/16-bit source depths and Adam7 interlacing. PictureView's UTF-16 entry point receives native
RGB24 or straight RGBA32 for 8-bit-and-shallower sources. Deeper opaque images use RGB48;
transparent images use straight RGBA64. Both preserve every 16-bit sample in little-endian order.
RGB16 decodes directly into the final six-byte pixel buffer; gray16 expands in that same buffer,
without an eight-byte-per-pixel intermediate image. PVD format codes are 25, 33, 49 and 65;
storage is 3, 4, 6 and 8 bytes per pixel. The legacy UTF-8 entry point retains BGR24/BGRA32/BGRA64.
Native output uses the RGB formats documented by the PictureView SDK v1.1 (2026.09.30).
The plugin is built for x64 and x86 with libspng and zlib linked statically.

Limitations: PNG `gAMA`/`cHRM` chunks and ICC colour management are not applied. RPGMVP has no
CICP source signalling beyond the PNG sRGB chunk, so the shared HDR/wide-gamut pipeline does not
change its output. APNG is deliberately
exposed as one still page containing its default image. The shared resource limits are 268,435,456
pixels total and 32,768 pixels on either side.

See [DESIGN.md](DESIGN.md) for the boundary design and [fixtures/SOURCES.md](fixtures/SOURCES.md)
for fixture provenance and verified expectations.

## Changes

- 1.2.0 — unpublished candidate: direct UTF-16 paths and native RGB24/RGBA32/RGB48/RGBA64
  through the wide entry point. Opaque 16-bit pages use 25% less pixel storage with RGB48;
  legacy BGR/BGRA output is preserved.
- 1.1.0 — transparency is now displayed and 16-bit PNGs are delivered to PictureView as 16-bit instead of being reduced to 8-bit.
- 1.0.0 — initial release.
