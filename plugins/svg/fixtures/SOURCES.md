# SVG/SVGZ fixture sources and expectations

All fixtures in this directory are self-authored test data for deckodeck. They are
licensed under the project MIT license (Copyright (c) 2026 Roman Kharitonov). No scraped
corpus, third-party fixture, browser snapshot or font file is included.

The bundle has 21 fixtures: 17 accepted SVG/SVGZ cases and 4 rejected cases. The local
PNG asset is `fixtures/assets/checker-2x2.png`. `manifest.json` records the dimensions,
analytic RGBA8 samples, rejection reason, byte size and SHA-256 of every file.

## Generation and determinism

`generate_fixtures.py` writes all fixture bytes and `manifest.json` using only the Python
standard library. It has no timestamp output. `valid.svgz` uses gzip mtime 0 and a zero
filename field. Rerunning the generator against the same Python/zlib build reproduces the
manifest hashes.

`validate_fixtures.py` checks hashes and sizes, strict UTF-8, ElementTree well-formedness,
gzip framing and decompression, PNG chunk CRCs, zlib decompression, PNG filter bytes and
the expected RGBA bytes of the checker asset and embedded data URI. It does not render
SVG. A passing file-format check is not rendering proof.

## Pixel convention

The expectation is one static RGBA8 page at `tree.size().to_int_size()`, with straight
(non-premultiplied) alpha. No background is synthesized: unpainted pixels are
`(0,0,0,0)`. A transparent result must reset RGB to zero after un-premultiplication. For
example, a 50% red pixel is `(255,0,0,128)`, not the premultiplied `(128,0,0,128)`.

Sample coordinates are integer pixel indices, interpreted at the pixel centre
`(x + 0.5, y + 0.5)`. Solid shapes have integer edges at those samples, so no
antialiasing ambiguity is involved. The gradient is the only tolerance-based colour
case. Its four pixel centres have `t = (x + 0.5) / 4`, giving sRGB grey values
31.875, 95.625, 159.375 and 223.125; the manifest uses nearest bytes and tolerance 3.

## Accepted fixtures

| Fixture | What independent expectation it establishes |
|---|---|
| `basic-2x2-colors.svg` | Red, green, blue and yellow pixels are packed top-down as RGBA8. |
| `alpha-ramp.svg` | Alpha 0, 0.5 and 1; transparent RGB is zero and 50% red is straight red. |
| `alpha-composite.svg` | Premultiplied source-over of 50% red over 50% blue becomes `(170,0,85,191)`. |
| `clip.svg` | A user-space clip leaves red columns 0-1 and blue columns 2-3. |
| `mask.svg` | Black mask content removes green; white content keeps opaque green. |
| `gradient-linear.svg` | Four horizontal black-to-white samples with the stated tolerance. |
| `filter-color-matrix.svg` | `feColorMatrix` halves `(200,100,50)` to exactly `(100,50,25)` in sRGB. |
| `transform-translate-scale.svg` | `translate(1 1) scale(2)` maps the unit square to `[1,3]x[1,3]`. |
| `transform-rotate-90.svg` | A 2x1 rect rotated 90 degrees about `(2,2)` occupies the right column. |
| `viewbox-scale.svg` | A 4x2 viewBox scales to the 8x4 viewport as four 4x2 colour blocks. |
| `units-in.svg` | `1in x 0.5in` at the default 96 dpi is 96x48 pixels. |
| `viewbox-only.svg` | Missing width/height fall back to the 6x3 viewBox size. |
| `xml-declaration-comments.svg` | XML declaration and comments are accepted and paint nothing. |
| `text-cyrillic.svg` | Cyrillic-only `Привет` produces ink through system fonts, not a glyph golden. |
| `image-data-uri.svg` | A base64 PNG data URI renders the checker quadrants. |
| `image-relative.svg` | A relative PNG asset renders the same checker quadrants. |
| `valid.svgz` | Gzip mtime 0, decompressed bytes identical to `basic-2x2-colors.svg`, same pixels. |

## Rejected fixtures

| Fixture | Intended rejection |
|---|---|
| `malformed.svg` | Unterminated XML; resvg/usvg parse failure. |
| `not-svg.svg` | Well-formed XML whose root is not `svg`; resvg/usvg parse failure. |
| `invalid-utf8.svg` | Raw `0xff` byte; strict UTF-8 decode failure. |
| `truncated.svgz` | Gzip magic followed by truncated compressed data; gzip failure. |

All four should make `pvdFileOpen` return `FALSE` in both disk and memory modes. The
suggested wrapper error is `ParseFailed`; `NotRecognised` is acceptable for the non-SVG
root only if the wrapper gates it before resvg, but the fixture itself is supplied without
that assumption.

## Wrapper assumptions exercised here

- Leave `usvg::Options::dpi` at its default 96, or set it explicitly to 96.
- Load system fonts with `fontdb::Database::load_system_fonts()` through
  `Options::fontdb_mut()`. No font file is bundled.
- Set `Options::resources_dir` to the SVG's parent directory so
  `assets/checker-2x2.png` resolves locally.
- Accept both `href` and `xlink:href`; this bundle uses the SVG2 `href` spelling.
- Render one static tree, convert tiny-skia's premultiplied RGBA8 output to straight
  RGBA8, and expose it as one page with top-down rows.

The Cyrillic fixture contains only the six Cyrillic letters `Привет`: no Latin letters,
digits or punctuation. Its source-level oracle requires both system-font ink and
Cyrillic-only text, so an ASCII-only render can no longer satisfy the case through the
broad dark-pixel count. The manifest also carries a `drop-cyrillic-text` negative control:
the integrated PVD renderer test must physically remove the `<text>` element, render the
remaining white background, observe zero qualifying ink and require the oracle to fail.
That negative control is a rendering requirement, not an XML inference.

The source tree used for these assumptions is the read-only resvg checkout at commit
`68b14c4` (`crates/usvg/src/parser/options.rs` defaults `dpi` to 96;
`crates/resvg/examples/minimal.rs` shows system-font loading and
`tree.size().to_int_size()`). resvg is MIT/Apache-2.0 licensed; no resvg file or rendered
image is copied into this bundle. The upstream resvg test suite can be cited as an
optional external reference, but this bundle does not depend on network access.

## Proposed deckodeck tests

These names follow the doctest and `PluginHost` conventions in
`plugins/rpgmvp/tests/e2e/E2eTests.cpp`. The main checkout executor should generate a
small C++ expectation header from `manifest.json` rather than add a JSON dependency.

1. `SVG fixtures decode to one static RGBA8 page in disk and memory modes`: iterate all
   accepted cases, call `readFixture`/`openImage`/`decodePage`, check one page, no
   animation, width, height and `nBPP == 32`, then compare each manifest sample with its
   tolerance.
2. `SVG alpha is straight and transparent pixels have zero colour`: assert the
   `alpha-ramp` and `alpha-composite` samples; a premultiplied leak fails immediately.
3. `SVG clip mask filter gradient and transforms match analytic samples`: assert the
   exact cases and the gradient tolerance range.
4. `SVG dimensions follow width height viewBox and 96 dpi`: cover `viewbox-scale`,
   `viewbox-only` and `units-in`.
5. `SVG accepts comments UTF-8 text data URI and local assets`: cover the XML wrapper,
   `text-cyrillic`, `image-data-uri` and `image-relative`.
6. `SVG rejects malformed XML non-SVG invalid UTF-8 and broken gzip`: assert
   `pvdFileOpen` is false in both modes for the four rejected fixtures.
7. `SVG Cyrillic-only text has system-font ink`: keep the white background samples, count
   dark pixels in the manifest ROI and check the manifest text is Cyrillic-only. Then
   render the `drop-cyrillic-text` mutation and require zero qualifying ink and a failed
   oracle. Do not compare glyph bitmaps.
8. `SVGZ decodes identically to its uncompressed source`: compare the full decoded bytes
   of `valid.svgz` and `basic-2x2-colors.svg`.

## Validation performed

On 2026-10-21, in this staging tree:

```
uv run --no-cache python staging/generate_fixtures.py
  wrote 21 fixtures (17 valid, 4 rejected)

uv run --no-cache python staging/test_text_oracle.py
  Ran 4 tests
  OK

uv run --no-cache python staging/validate_fixtures.py
  manifest schema: 1
  fixtures: 21 (17 valid, 4 rejected)
  assets: 1 PNG
  PNG: chunk CRCs, zlib decompression, filter type 0 and RGBA bytes PASS
  XML: well-formed SVG, well-formed non-SVG, malformed XML and invalid UTF-8 classified PASS
  gzip: valid mtime=0 source equality and truncated-stream rejection PASS
  rendering: NOT RUN - pixel expectations remain analytical
  RESULT: PASS
```

Deferred to the integrated e2e run: actual resvg pixels, antialiasing/coverage, system
font ink on the target Windows host, text ROI counts, and final PVD page/alpha metadata.
