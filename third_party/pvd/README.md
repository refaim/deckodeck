# PictureView PVD interface — reference material

Everything here is authored by Pavel Skakov (PictureView plugin for Far Manager 3) and is kept for
reference only; nothing in this directory is compiled into the plugins except `PictureViewPlugin.h`.

- `PictureViewPlugin.h` — the PVD decoder interface v1.1, dated 2026.09.30, copied verbatim
  from the author's UTF-8 SDK header. Numeric interface version remains 1. SHA256:
  `3DE27BB331EF0E5019AABE712C867C487F1E433F492B37CABC9CD7391896EEE9`.
  The only file the build uses; includes `pvdFileOpenW`, alpha/ICC flags and the ICC tail.
- `examples/` — the author's sample decoders: `pvdBMP.cpp` (file mapping, read-only image, palette,
  bottom-up pitch), `pvdIJL.cpp` (allocating decoder, BGR24), `pvdDjVu.cpp` (multi-page decoder over
  DjVuLibre, BGR24 top-down, GPLv3 as marked in its header). They document how the host calls the
  original eight exports; our tests/e2e drivers additionally exercise `pvdFileOpenW`.
- `dist-docs/` — the readme and help/language files shipped with the PictureView 3 distribution
  (build 2021.04.19): decoder interfaces, priorities (PVD > GDI+ 9.5 > WIC 8 > ACDSee 6.5 > GFL 3.5),
  key bindings. Useful when deciding a plugin's priority or checking host behaviour.

The distribution's binary decoders (`*.pvd`, `*.apl`, `libgfl340.dll`) are deliberately not kept
here; the bundled decoders import `KERNEL32.dll` + `msvcrt.dll`, ours import `KERNEL32.dll` only.
