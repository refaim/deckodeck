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
  range with BT.2390 tone mapping from the picture's own peak. PictureView
  versions using the wide filename entry point receive 16-bit RGB for
  opaque images or RGBA with transparency; the older entry point retains
  16-bit BGRA. Dithering and 10-bit output can use the 16-bit output
  precision. Opaque native RGB uses 25% less pixel storage for both the
  cached image and the displayed page. The EXR premultiplied alpha becomes
  straight alpha. The display window is what you see: data outside it is cropped
  and areas without data are black (transparent black with alpha).

  Parts, stereo views and complete colour layers are independent pages.
  Switch them with PictureView's page navigation. The previous preferred
  picture remains first; other pages are decoded when opened and keep
  their own size, alpha and colour. Deep parts are skipped. Channels:
  R, G, B (and A); Y with RY/BY chroma or Y alone; otherwise a usable
  channel as grey. UINT channels (ids, masks) are not colour. Luminance/chroma
  reconstruction also honours colorInteropID when chromaticities is
  absent; valid explicit chromaticities take precedence.

  The title is a short file summary. For several pages it shows their
  count and structure (multipart, multiview or layers), since PictureView
  keeps this text while navigating. Page-specific colour and channels
  are omitted from that summary.

  Not done yet: deep images, UINT-only channels, mip/rip levels beyond
  the first, user exposure or tone controls.

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
