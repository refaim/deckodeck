EXR for PictureView 3
*********************

Shows OpenEXR pictures in Far: renders, compositing passes, HDR photos
and textures - scanline or tiled, ZIP, PIZ, PXR24, B44, DWA, HTJ2K and
the rest. A decoder for PictureView, the picture viewer plugin for Far
Manager 3.

How to use:
  Nothing to learn. Open an .exr (or .sxr) file the way you open any
  picture in PictureView. There are no knobs by design: the picture is
  shown the way a colour-managed viewer shows it by default.

  EXR pixels are linear light with no brightness of their own, so the
  plugin takes 1.0 as 100 nit (or as the file's whiteLuminance), honours
  the file's primaries (Rec.709, Rec.2020, P3-D65, ACES AP0/AP1 or any
  custom chromaticities) and squeezes the highlights into the display
  range with BT.2390 tone mapping from the picture's own peak. Every
  picture is handed to PictureView as 16-bit BGRA, so with dithering or
  10-bit output enabled in its settings you see the whole precision.
  Transparency is shown; the EXR premultiplied alpha becomes straight
  alpha. The display window is what you see: data outside it is cropped
  and areas without data are black (transparent black with alpha).

  Of a multi-part or stereo file one part is shown: the first one with
  colour channels, the left view when there are views. Deep parts are
  skipped. Channels: R, G, B (and A); Y with RY/BY chroma or Y alone;
  otherwise the first layer with R, G, B; otherwise the first channel
  as grey. UINT channels (ids, masks) are not colour.

  Not done yet: deep images, multi-part files as pages, the right view,
  layers other than the first, user exposure or tone controls.

Install:
  Unpack the archive into PictureView's folder (the one with
  0PictureView.dll, usually Plugins\PictureView in your Far) and restart
  Far. Take the x64 archive for 64-bit Far and the x86 one for 32-bit Far.

  The plugin is self-contained: no runtime, no codecs, no GDI+, only
  Windows itself.

License: MIT. The plugin bundles OpenEXR (BSD-3-clause), Imath
(BSD-3-clause), libdeflate (MIT) and OpenJPH (BSD-2-clause); their
license texts are in LICENSES.txt. The PictureView plugin interface
header is (c) Pavel Skakov.

Roman Kharitonov
  https://github.com/refaim/deckodeck
