# deckodeck

Decoder plugins for PictureView 3, the picture viewer plugin for Far Manager 3 (by Pavel
Skakov). Built for x64 and x86 Far; each plugin is one self-contained `.pvd` — no runtime,
no codecs, only Windows.

## Download

Each release has a zip per architecture, x64 for 64-bit Far and x86 for 32-bit.

<!-- downloads:begin -->
| Plugin | Latest version | Release | Description | Libraries |
| --- | --- | --- | --- | --- |
| AVIF.pvd | 1.2.0 | [avif/v1.2.0](https://github.com/refaim/deckodeck/releases/tag/avif/v1.2.0) | Photos, HDR and animation | [libavif](https://github.com/AOMediaCodec/libavif), [dav1d](https://code.videolan.org/videolan/dav1d), [libyuv](https://chromium.googlesource.com/libyuv/libyuv) |
| EXR.pvd | 1.0.0 | [exr/v1.0.0](https://github.com/refaim/deckodeck/releases/tag/exr/v1.0.0) | HDR renders, layers and views | [OpenEXR](https://github.com/AcademySoftwareFoundation/openexr), [Imath](https://github.com/AcademySoftwareFoundation/Imath), [libdeflate](https://github.com/ebiggers/libdeflate), [OpenJPH](https://github.com/aous72/OpenJPH) |
| RPGMVP.pvd | 1.2.0 | [rpgmvp/v1.2.0](https://github.com/refaim/deckodeck/releases/tag/rpgmvp/v1.2.0) | Encrypted RPG Maker MV/MZ PNG | [libspng](https://github.com/randy408/libspng), [zlib](https://www.zlib.net/) |
<!-- downloads:end -->

## Install

Unpack the archive into PictureView's folder (the one with `0PictureView.dll`, usually
`Plugins\PictureView` in your Far), and restart Far.

## License

MIT, see [LICENSE](LICENSE). The bundled codec libraries keep their own licenses, listed in each
archive's `LICENSES.txt`; the PictureView plugin interface header is © Pavel Skakov.

Building, packaging and CI: [docs/BUILD.md](docs/BUILD.md).
