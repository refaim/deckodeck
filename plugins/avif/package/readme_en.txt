AVIF for PictureView 3
**********************

Shows AVIF pictures in Far: photos from phones and the web, animated AVIFs
page by page, 10-bit, 12-bit and HDR files. A decoder for PictureView, the
picture viewer plugin for Far Manager 3.

How to use:
  Nothing to learn. Open an .avif file the way you open any picture in
  PictureView. Animated files come as pages, switch them as usual.

  Transparency is shown. Identity 8-bit SDR pictures use BGR24 when opaque
  or BGRA32 with alpha. Pictures needing HDR or wide-gamut presentation,
  at any depth, and all pictures deeper than 8 bits use BGRA64. Thus, with
  dithering or 10-bit output enabled in PictureView's settings, you see all
  the precision of deep files. Rotation, mirroring and cropping stored in
  the file are applied. EXIF orientation is honoured when the file has
  no irot/imir transformation.

  HDR (PQ/HLG) and wide-gamut (Rec.2020/P3) pictures are converted to
  sRGB for display, with BT.2390 tone mapping for HDR.

  Not done yet: embedded ICC profiles are not applied; PictureView ignores
  them and deckodeck does not have a colour-management system yet. Gain maps
  and user-adjustable exposure or tone settings are not supported.

Install:
  Unpack the archive into PictureView's folder (the one with
  0PictureView.dll, usually Plugins\PictureView in your Far) and restart
  Far. Take the x64 archive for 64-bit Far and the x86 one for 32-bit Far.

  The plugin is self-contained: no runtime, no codecs, no GDI+, only
  Windows itself.

License: MIT. The plugin bundles libavif (BSD-2-clause), dav1d
(BSD-2-clause) and libyuv (BSD-3-clause); their license texts are in
LICENSES.txt. The PictureView plugin interface header is (c) Pavel Skakov.

Roman Kharitonov
  https://github.com/refaim/deckodeck
