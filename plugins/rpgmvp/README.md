# RPGMVP.pvd

PictureView 3 decoder for RPG Maker MV `.rpgmvp` and RPG Maker MZ `.png_` encrypted PNG images.
The plugin reconstructs the fixed first 16 PNG bytes in a libspng stream callback, then reads the
remaining bytes directly from PictureView's mapped file. No encryption key and no full-file copy are
needed.

It supports PNG greyscale, RGB, indexed, greyscale-alpha and RGBA images, tRNS transparency,
1/2/4/8/16-bit source depths and Adam7 interlacing. Sources deeper than 8 bits are returned as
BGRA64; 8-bit-and-shallower opaque images use BGR24 and images with direct or tRNS alpha use
straight BGRA32. The plugin is built for x64 and x86 with libspng and zlib linked statically.

Limitations: PNG `gAMA`/`cHRM` chunks and ICC colour management are not applied. RPGMVP has no
CICP source signalling beyond the PNG sRGB chunk, so the shared HDR/wide-gamut pipeline does not
change its output. APNG is deliberately
exposed as one still page containing its default image. The shared resource limits are 268,435,456
pixels total and 32,768 pixels on either side.

See [DESIGN.md](DESIGN.md) for the boundary design and [fixtures/SOURCES.md](fixtures/SOURCES.md)
for fixture provenance and verified expectations.

## Changes

- 1.1.0 — sources deeper than 8 bits are delivered to the host as 16-bit BGRA (`nBPP` 64) instead
  of being reduced to 8 bits.
- 1.0.1 — the alpha channel is now flagged to the host; in 1.0.0 transparent images were displayed opaque.
- 1.0.0 — initial release.
