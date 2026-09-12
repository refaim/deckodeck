# RPGMVP.pvd

PictureView 3 decoder for RPG Maker MV `.rpgmvp` and RPG Maker MZ `.png_` encrypted PNG images.
The plugin reconstructs the fixed first 16 PNG bytes in a libspng stream callback, then reads the
remaining bytes directly from PictureView's mapped file. No encryption key and no full-file copy are
needed.

It supports PNG greyscale, RGB, indexed, greyscale-alpha and RGBA images, tRNS transparency,
1/2/4/8/16-bit source depths and Adam7 interlacing. Opaque images are returned as BGR24 and images
with direct or tRNS alpha as straight BGRA32. The plugin is built for x64 and x86 with libspng and
zlib linked statically.

Limitations: output is 8 bits per channel; gamma correction and ICC colour management are not
applied. APNG is deliberately exposed as one still page containing its default image. The shared
resource limits are 268,435,456 pixels total and 32,768 pixels on either side.

See [DESIGN.md](DESIGN.md) for the boundary design and [fixtures/SOURCES.md](fixtures/SOURCES.md)
for fixture provenance and verified expectations.
