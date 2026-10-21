SVG for PictureView 3
*********************

Shows SVG and SVGZ pictures in PictureView, the picture viewer plugin for
Far Manager 3. The renderer is resvg, built into the plugin.

How to use:
  Open an SVG or SVGZ file like any other picture. One still page is shown
  at its intrinsic size. Text uses the fonts installed in Windows.
  Transparency, filters, clipping, masks, gradients and embedded images
  follow resvg. Relative image links are supported for SVGs opened from
  disk. SVG files inside archives open too, but their relative references
  may be missing because an archive entry has no reliable document folder.
  Nothing is downloaded from the internet.

  Limits: one still page; no animation, scripts or interactive SVG.
  Zooming scales the rendered bitmap without rendering the SVG again.
  Pictures over 16,384 pixels on a side or 67,108,864 pixels in total are
  refused. This is not a complete browser SVG implementation.

  When memory is exhausted inside resvg, Far may terminate unexpectedly.
  Size checks do not remove this risk.

  The plugin is self-contained: no runtime, no additional codecs, no GDI+.
  Compatibility libraries for older Windows are included, but Windows 7 has
  not been tested.

Install:
  Unpack the archive into PictureView's folder (the one with
  0PictureView.dll, usually Plugins\PictureView in your Far) and restart Far.
  Take the x64 archive for 64-bit Far and the x86 one for 32-bit Far.

License: MIT. The plugin bundles resvg 0.48.1 and YY-Thunks 1.2.3 (both
MIT). Their notices, the notices of the Rust dependencies, and the Rust
toolchain copyright notices are in LICENSES.txt. The PictureView plugin
interface header is (c) Pavel Skakov.

Roman Kharitonov
  https://github.com/refaim/deckodeck
