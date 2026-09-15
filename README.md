# deckodeck

Decoder plugins for PictureView 3, the picture viewer plugin for Far Manager 3 (by Pavel
Skakov). Today: `AVIF.pvd` — AVIF photos from phones and the web, animation as pages, 10-bit,
12-bit and HDR; and `RPGMVP.pvd` — RPG Maker MV/MZ encrypted PNG (`.rpgmvp`/`.png_`), no key
needed. Built for x64 and x86 Far; each plugin is one self-contained `.pvd` — no runtime, no
codecs, only Windows.

## Download

Each release has a zip per architecture, x64 for 64-bit Far and x86 for 32-bit, with the
SHA-256 in the release notes.

<!-- The rows are seeded for the 1.1.0 releases; each link resolves once its tag (avif/v1.1.0,
     rpgmvp/v1.1.0) has been pushed and the release workflow has published it. -->
<!-- downloads:begin -->
| Plugin | Latest version | Release |
| --- | --- | --- |
| AVIF.pvd | 1.1.0 | [avif/v1.1.0](https://github.com/refaim/deckodeck/releases/tag/avif/v1.1.0) |
| RPGMVP.pvd | 1.1.0 | [rpgmvp/v1.1.0](https://github.com/refaim/deckodeck/releases/tag/rpgmvp/v1.1.0) |
<!-- downloads:end -->

## Install

Unpack the archive into PictureView's folder (the one with `0PictureView.dll`, usually
`Plugins\PictureView` in your Far), and restart Far.

## License

MIT, see [LICENSE](LICENSE). The bundled codec libraries keep their own licenses, listed in each
archive's `LICENSES.txt`; the PictureView plugin interface header is © Pavel Skakov.

Building, packaging and CI: [docs/BUILD.md](docs/BUILD.md).
