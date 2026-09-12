# PictureView PVD interface — reference material

Everything here is authored by Pavel Skakov (PictureView plugin for Far Manager 3) and is kept for
reference only; nothing in this directory is compiled into the plugins except `PictureViewPlugin.h`.

- `PictureViewPlugin.h` — the PVD decoder interface v1 (UTF-8 re-encoding of the CP1251 original
  from the PictureView SDK). The only file the build uses.
- `examples/` — the author's sample decoders: `pvdBMP.cpp` (file mapping, read-only image, palette,
  bottom-up pitch), `pvdIJL.cpp` (allocating decoder, BGR24), `pvdDjVu.cpp` (multi-page decoder over
  DjVuLibre, BGR24 top-down, GPLv3 as marked in its header). They document how the host calls the
  eight exports; our tests/e2e drivers follow them.
- `dist-docs/` — the readme and help/language files shipped with the PictureView 3 distribution
  (build 2021.04.19): decoder interfaces, priorities (PVD > GDI+ 9.5 > WIC 8 > ACDSee 6.5 > GFL 3.5),
  key bindings. Useful when deciding a plugin's priority or checking host behaviour.

The distribution's binary decoders (`*.pvd`, `*.apl`, `libgfl340.dll`) are deliberately not kept
here; the bundled decoders import `KERNEL32.dll` + `msvcrt.dll`, ours import `KERNEL32.dll` only.
