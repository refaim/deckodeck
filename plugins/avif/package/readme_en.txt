AVIF for PictureView 3
**********************

Shows AVIF pictures in Far: photos from phones and the web, animated AVIFs
page by page, 10-bit, 12-bit and HDR files. A decoder for PictureView, the
picture viewer plugin for Far Manager 3.

How to use:
  Nothing to learn. Open an .avif file the way you open any picture in
  PictureView. Animated files come as pages, switch them as usual.

  Transparency is shown. Pictures deeper than 8 bits go to PictureView as
  16-bit, so with dithering or 10-bit output enabled in its settings you
  see all the precision of the file. Rotation, mirroring and cropping
  stored in the file are applied.

  Not done yet: HDR files (PQ, HLG) are shown without tone mapping, so
  they look dark or washed out; embedded ICC profiles are not applied.

Install:
  Unpack the archive into PictureView's folder (the one with
  0PictureView.dll, usually Plugins\PictureView in your Far) and restart
  Far. Take the x64 archive for 64-bit Far and the x86 one for 32-bit Far.

  The plugin is self-contained: no runtime, no codecs, no GDI+, only
  Windows itself.

Licence: MIT. The plugin bundles libavif (BSD-2-clause), dav1d
(BSD-2-clause) and libyuv (BSD-3-clause); their licence texts are in
LICENSES.txt. The PictureView plugin interface header is (c) Pavel Skakov.

Roman Kharitonov
