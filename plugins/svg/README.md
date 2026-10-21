# SVG.pvd

SVG and SVGZ renderer for PictureView 3, the image viewer plugin for Far Manager 3 by Pavel
Skakov. It is the `svg` plugin of the deckodeck monorepo, built for x64 and x86, with the
unmodified resvg 0.48.1 C API linked as a static Rust library through YY-Thunks. This is
version 1.0.0.

One still page is rendered at the SVG's intrinsic CSS-pixel size (the default 96 dpi). CSS,
text with installed system fonts, clipping, masks, gradients and filter effects, transparency,
and embedded or referenced raster images are handled by resvg. External images referenced by a
file opened from disk resolve relative to that file's directory. Archive and virtual-panel
input has no reliable document base, so such relative assets may be missing; nothing is fetched
over HTTP. System fonts are loaded once on first use, and parse calls are serialized through
the shared resvg options object.

PictureView's UTF-16 entry point receives native RGB24 or RGBA32 through the current interface;
the legacy UTF-8 entry point receives BGR24 or BGRA32. resvg produces 8-bit premultiplied RGBA,
and the plugin converts it to straight alpha before handing the page to PictureView.

## Limitations

- One static page. SMIL/CSS animation, scripts, interactive DOM behaviour and browser
  navigation are not provided.
- PictureView zooms the rendered bitmap; the SVG is not re-rendered for each zoom level.
- Feature coverage is resvg's, not a guarantee of full browser SVG parity.
- Intrinsic width or height above 16,384 pixels, or more than 67,108,864 output pixels, is
  refused before the output buffer is allocated. These limits cannot bound every allocation
  made inside resvg/Rust.
- Under severe memory pressure, an allocation inside resvg/Rust can abort the process before
  the C++ firewall can act. Far may therefore close; the observed isolated failure returned
  `0xC0000409`. This is an accepted limitation of keeping resvg in-process, not a bounded-error
  path and not a general relaxation of the other plugins' OOM handling.
- Windows 7 compatibility libraries are linked through YY-Thunks, but running the plugin on an
  actual Windows 7 installation has not been verified. The x64 and x86 binaries were checked
  manually in Far.

## Libraries

[resvg 0.48.1](https://github.com/linebender/resvg) provides parsing and rasterization;
[YY-Thunks 1.2.3](https://github.com/Chuyu-Team/YY-Thunks) supplies the static Windows
compatibility overlay. The plugin contains no custom SVG parser, font database or renderer, and
downloads no font pack. Dependency notices are in the archive's `LICENSES.txt`; the Rust
toolchain copyright notices are included in that same file.

## Changes

- 1.0.0 - SVG and SVGZ, one still page at intrinsic size, system-font text,
  relative disk resources, and straight-alpha RGB/RGBA output.
