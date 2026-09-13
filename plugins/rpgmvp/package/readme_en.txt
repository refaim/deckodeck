RPGMVP for PictureView 3
************************

Shows the encrypted pictures of RPG Maker MV and MZ games (.rpgmvp and
.png_) right in the game folder: sprites, tilesets, faces, backgrounds.
No key, no decrypting, no unpacking. A decoder for PictureView, the
picture viewer plugin for Far Manager 3.

How to use:
  Open the file the way you open any picture in PictureView. Files inside
  archives work too. Transparency is shown; 16-bit PNGs go to PictureView
  as 16-bit.

  Animated PNGs (APNG) show their first frame only. RPG Maker never makes
  them anyway.

Install:
  Unpack the archive into PictureView's folder (the one with
  0PictureView.dll, usually Plugins\PictureView in your Far) and restart
  Far. Take the x64 archive for 64-bit Far and the x86 one for 32-bit Far.

  The plugin is self-contained: no runtime, no codecs, no GDI+, only
  Windows itself.

Licence: MIT. The plugin bundles libspng (BSD-2-clause) and zlib (zlib
licence); their licence texts are in LICENSES.txt. The PictureView plugin
interface header is (c) Pavel Skakov.

Roman Kharitonov
