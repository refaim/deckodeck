# SVG.pvd plugin design

## Scope and boundary

SVG.pvd renders one still SVG or SVGZ page at its intrinsic CSS-pixel size, with resvg's default
96 dpi. SVG parsing, SVGZ decompression, CSS, text, clipping, masks, gradients, filters and raster
images stay inside the unmodified resvg 0.48.1 C API. The C++ plugin owns only format recognition,
the PictureView session, output conversion and resource limits; it contains no XML parser, font
database, rasterizer, filter implementation or Rust code of its own.

The pinned source is resvg 0.48.1 at commit `68b14c4c3bccdb60344c777406486b54c36ec1a4`, built from
its upstream `Cargo.lock`. Cargo builds the `resvg-capi` static library with the default `svgz`,
`text`, `system-fonts`, `memmap-fonts` and `raster-images` features. The Rust font database starts
empty, so the adapter must call the library's system-font loading exactly once before the first
parse; the default features do not supply a bundled font pack.

The renderer is deliberately a still raster page. There is no script execution, interactive DOM or
SMIL/CSS animation, no HTTP fetching, and no re-render when PictureView changes zoom: the PVD
interface requests pages, not viewport renders, so the host zooms the raster that was produced.
Feature coverage is resvg's, with no promise of complete browser SVG behaviour.

## Build and imports

The upstream v0.48.1 tag archive is downloaded through CMake's `FetchContent` and pinned by SHA-256
`40dafea6b4b9d01e9d28b6d49f1e912daf3e9055676ad9179a5a2db6e7386945`. CMake invokes
`cargo rustc -p resvg-capi --lib --release --locked --target <msvc-target> --crate-type
staticlib` with `RUSTFLAGS=-C target-feature=+crt-static`; the targets are
`x86_64-pc-windows-msvc` and `i686-pc-windows-msvc`. The static CRT, static codec rule, C++23
rules and exactly nine PVD exports are the same as in the other plugins. CMake validates the
selected Rust 1.99.1 toolchain and invokes Cargo through `rustup run`; `docs/BUILD.md` documents
the builder installation and the absolute-toolchain override. No Rust runtime or codec DLL is
shipped.

YY-Thunks 1.2.3 is linked as its release's official static-library overlay, provenance commit
`b41fb80b8d316581eec0959600aeb0e7339d0e5e`; the pinned `YY-Thunks-Lib.zip` has SHA-256
`8d2417cea1914a678f766c73fd458bb85dfd6057878f8d1d87ddcfc370a42ef9`. It resolves Rust's
Windows API imports to the compatibility libraries. The reviewed recipe is verified for the
current Windows x64 and x86 builds only; running under Windows 7 itself remains unverified.

The final SVG plugin may import only `KERNEL32.dll`, `ntdll.dll` and `ole32.dll`. This is the
explicit SVG-only extension approved by Roma on 2026-10-21 for the Rust/resvg static library.
AVIF, EXR and RPGMVP remain strictly `KERNEL32.dll`-only, and the exception does not permit
changes to the third-party sources or any other plugin.

## Core and composition

`src/core/Format` requires at least two supplied bytes, accepts gzip input by its magic, and
otherwise treats input as an SVG candidate when, after an optional UTF-8 BOM and XML whitespace,
the next byte is `<`. A head that contains only BOM/XML whitespace is also a candidate because
the root may be beyond the head. It deliberately does not parse XML. resvg validates the DOCTYPE,
namespaces and SVG root; malformed XML, a non-SVG root, invalid UTF-8 and a truncated gzip stream
are refused, as is a gzip stream that does not contain SVG.

`src/core/Describe` reports format `SVG`, compression `None` or `GZip`, and an 8-bit RGBA
comment. The shared session sees one still page with alpha.

`src/core/Render` owns the pure raster policy and output rules: the upstream `max(1, round)`
intrinsic-size rule, the host `maxDimension`/`maxPixels` checks, tiny-skia's unconditional
`INT32_MAX / 4` row limit, the addressable byte count, destination-layout validation and the
premultiplied-to-straight conversion in every PVD byte order. `planRaster` returns a validated
plan, so the decoder stores the geometry and byte count instead of recomputing them during decode.

`src/DefaultPlugin.cpp` owns the Win32 file source, the resvg decoder factory (including its
plugin-wide font state), the describer, the shared colour tables and `CodecPlugin` in dependency
order. `SvgState` creates one `resvg_options`, calls `resvg_options_load_system_fonts` on first
use, and serialises parse calls because the resvg C API mutates the resources directory inside
that shared options object. Rendering runs outside the parse lock. This uses the permitted
SRWLOCK-backed `std::mutex`; it does not introduce a thread-safe function-local static, and the
system-font load remains a lazy one-time operation.

## Resource resolution and the shared source path

The shared `DecoderOptions::sourcePath` is an optional borrowed view of the stable absolute native
path supplied by `IFileData::sourcePath()`. Its owner is the session's file data and it remains
valid for the lifetime of the decoder; `CodecPlugin` copies it only for an on-disk open and
clears it for archive or virtual-buffer input. Existing plugins ignore the field.

For a file opened by name, the adapter passes the parent directory of that absolute path to
`resvg_options_set_resources_dir`. Relative `<image href="...">` resources therefore resolve
beside the SVG, including paths with Unicode names; this is covered by the adapter test that
copies the SVG and its PNG asset into a Unicode-named temporary directory. The parent path is
converted through the shared Win32 wide-to-UTF-8 boundary helper, which returns
`ConversionFailed` for an unpaired surrogate instead of letting a filesystem conversion throw.

For memory input the adapter resets `resources_dir` to `None`. That is not a filesystem deny:
with no document-relative base, resvg may fall back to the process current directory, so a
resource can still resolve by accident, but archive and virtual-panel documents have no reliable
base and their relative assets may be missed. Absolute resources and resources that happen to
resolve from the current directory are not categorically forbidden. No URL is fetched over HTTP.

## Rendering and PVD formats

resvg renders premultiplied RGBA8. The core conversion turns every pixel into straight alpha by
`round(channel * 255 / alpha)`, with zero colour for alpha zero, then writes RGBA32/RGB24 for the
wide entry point and BGRA32/BGR24 for legacy callers. Alpha is always advertised because an SVG
canvas can contain transparent pixels. Sixteen-bit output is not requested for the 8-bit resvg
result and is refused as `UnsupportedFeature` if a direct caller asks for it. The encoded output
is 8-bit, unpremultiplied and top-down.

Decoding reports progress at the shared 0/3, 1/3 and 2/3 stages. The host callback can abort
before render, after render/convert, or after the completed page is ready; an aborted attempt
retains no page. Closing a session releases all outstanding pages.

Before allocating output, the core raster planner rejects an intrinsic size that is non-finite,
zero, beyond `maxDimension`, or whose pixel count exceeds `maxPixels`. Positive fractional sizes
follow resvg's `max(1, round)` rule, so `0.1` becomes one pixel rather than an invalid cast.
Independently of the host policy, a width beyond `INT32_MAX / 4` is refused because tiny-skia's
`PixmapMut` cannot represent its row, and the total raster must fit the address space. The
production composition sets `maxPixels` to 67,108,864 (8192 x 8192) and `maxDimension` to 16,384.
The resulting output byte count is also bounded by the addressable `PixelBuffer`. These are
independent C++ preflights.

## Accepted memory-pressure limitation

The C++ preflights and the shared catch-all firewall cannot guarantee that an allocation inside
resvg/Rust will avoid process termination. resvg has no fallible-allocation C contract and a Rust
abort is not a C++ exception. In an isolated reproduction, a 64 MiB allocation made under a
192 MiB job cap ended with `0xC0000409` and aborted before the C++ firewall could return an
error. On 2026-10-21 Roma accepted that limitation for SVG: resvg stays inside the PVD process,
and PictureView/Far may exit when memory is exhausted. There is no helper process and no claim
that the size limits bound every internal allocation.

This is an SVG-specific accepted risk, not an unresolved design blocker. All other expected
failures still use `std::expected<T, Error>`, and the existing C++ OOM firewall remains in force
for the plugin and the other formats. The third-party sources are still unmodified.

## Tests and fixtures

Core tests cover the two-byte threshold, BOM/XML whitespace-only heads, declaration, comments,
case, gzip and non-SVG markup candidates, and the absence of a 4096-byte probe, plus
fractional-size rounding, renderer and policy boundaries, destination validation and
straight-alpha conversion in every byte order. Adapter tests cover exact opaque and translucent
pixels, malformed input, repeated decodes, external resources through `sourcePath` (including a
lone surrogate), system-font ink (including the negative control that removes the text), and
shared lazy font initialisation. The shared Win adapter tests cover wide-to-UTF-8 conversion and
its surrogate and length limits. E2E tests exercise both entry points, progress aborts, page
bounds, session ownership, disk/memory opens, the export/import gates and the shared leak
scenarios.

The generator and `fixtures/SOURCES.md` define a generated canonical bundle of 21 self-authored
cases: 17 accepted SVG/SVGZ inputs and 4 rejected inputs, plus the referenced PNG asset. The
integrated e2e fixture inventory is a separate, current classification: 17 main accepted
fixtures, 6 accepted compatibility fixtures retained from the earlier set, and 9 rejected
entries (including the PNG and support files). `FixtureInventoryTests.cpp` requires every
recursively discovered non-markdown file to appear exactly once in that inventory; the categories
are explicit classifications, not raw directory counts. See `fixtures/SOURCES.md` for provenance
and analytic expectations. Manual checks in Far and actual Windows 7 runtime checks have not yet
been recorded; this document does not claim them, nor does it claim that the whole test suite is
green.
