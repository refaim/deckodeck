# AVIF.pvd

AVIF decoder plugin for PictureView 3 (the image viewer plugin for Far Manager 3 by Pavel
Skakov), built for both the x64 and the x86 (32-bit) Far Manager. It implements PictureView's
PVD decoder interface v1 (`third_party/pvd/PictureViewPlugin.h`) and decodes with libavif 1.4.2,
dav1d 1.5.3 and libyuv, all linked statically: the finished
`AVIF.pvd` imports `KERNEL32.dll` and nothing else, so it needs no runtime, no WIC codec and no
GDI+.

What it does:

- Still images and image sequences (`avis`): every frame is a page with its display time.
- 8, 10 and 12-bit sources, 4:4:4 / 4:2:2 / 4:2:0 / 4:0:0, grid images, progressive files.
- Alpha (straight, 32-bit BGRA output); opaque images are handed over as 24-bit BGR.
- `clap`, `irot` and `imir` transformative properties, applied in that order.
- Files from archives and virtual panels (the host hands over the whole file in memory).

## Install

Copy `AVIF.pvd` next to `0PictureView.dll` in the PictureView installation (typically
`%FARPROFILE%\Plugins\PictureView` or `<Far>\Plugins\PictureView`) and restart Far. The plugin
registers with priority 10 and is picked up by PictureView's automatic format detection. The
build and the scripts never touch the Far Manager installation.

## Build

Requirements (already installed on the reference machine, see `AGENTS.md`): clang-cl 19, lld-link,
llvm-cov / llvm-profdata from the VS 2022 Build Tools, CMake >= 3.28, Ninja (vcpkg's copy is found
automatically), vcpkg in manifest mode. The `ports/libavif` overlay pins libavif 1.4.2#2 and patches
its static-library merge for clang-cl; `triplets/x64-windows-static-clang.cmake` and
`triplets/x86-windows-static-clang.cmake` build every dependency with the same toolchain and the
static CRT.

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release          # unit, adapter, guard and e2e tests + the import/export-table checks
```

The plugin is written to `build/release$env:AVIFPVD_BUILD_SUFFIX/src/pvd/AVIF.pvd`
(`build/release/src/pvd/AVIF.pvd` when the suffix is unset; the suffix lets several builds of the
same tree coexist). The `debug` preset builds the same targets unoptimised.

### 32-bit build (x86)

The same plugin for the 32-bit Far Manager / PictureView comes from the `*-x86` presets:

```powershell
cmake --preset release-x86
cmake --build --preset release-x86
ctest --preset release-x86      # the same tests, run as 32-bit processes, plus both table checks
```

The DLL lands in `build/release-x86$env:AVIFPVD_BUILD_SUFFIX/src/pvd/AVIF.pvd`
(`IMAGE_FILE_MACHINE_I386`, imports `KERNEL32.dll` only, exports the eight bare `pvd*` names).
`debug-x86` and `coverage-x86` mirror `debug` and `coverage`. The x86 presets use the triplet
`triplets/x86-windows-static-clang.cmake`: the same x64-hosted clang-cl cross-compiles for
`i686-pc-windows-msvc`, and the triplet loads the x86 Visual Studio library environment for the
vcpkg ports (dav1d's meson build needs it to link against the x86 CRT). Install the x86 DLL next
to the 32-bit `0PictureView.dll`; the two DLLs are not interchangeable.

`scripts/build-all.ps1` builds both architectures, runs the import and export checks on each DLL
and copies them to `dist/x64/AVIF.pvd` and `dist/x86/AVIF.pvd`; `scripts/package.ps1` does the
same from scratch and produces `dist/AVIF-<version>-x64.zip` and `dist/AVIF-<version>-x86.zip`
(`AVIF.pvd`, `README.txt`, `LICENSES.txt`) with their SHA-256.

Every `AVIF.pvd` carries a VERSIONINFO resource (version, author, copyright, library versions); the
version comes from `project(VERSION)` in `CMakeLists.txt`, and `src/pvd/PluginConstants.hpp` is
`static_assert`ed against it. `AVIFPVD_AUTHOR` and `AVIFPVD_COPYRIGHT` can be overridden at
configure time (`cmake --preset release -DAVIFPVD_AUTHOR=...`).

Checks that are part of the definition of done:

```powershell
./scripts/coverage.ps1                                  # 100 % lines and branches on src/**
./scripts/coverage.ps1 -Preset coverage-x86             # the same gate for the 32-bit build
./scripts/check-imports.ps1 -Path build/release/src/pvd/AVIF.pvd   # imports == KERNEL32.dll only
./scripts/check-exports.ps1 -Path build/release/src/pvd/AVIF.pvd   # exports == the eight bare pvd* names
```

`coverage.ps1` configures and builds the `coverage` preset (`-Preset coverage-x86` for x86), runs
every test executable (the e2e run loads the instrumented `AVIF.pvd` and merges its profile too,
and the script fails unless the DLL really wrote one: two raw profiles from the e2e process whose
counters execute `Exports.cpp` in the DLL's own mapping), prints the llvm-cov report, writes HTML
to `build/<preset><suffix>/html` and fails unless `src/**` is at 100 % lines and 100 % branches.

## Layout

```
src/pvd/        PVD boundary: value types, IPlugin/IFileSession, Shim (marshalling + firewall),
                Exports.cpp (the 8 exports), AVIF.def, AVIF.rc + Version.hpp.in (VERSIONINFO),
                PvdApi.hpp, PluginConstants.hpp
src/core/       decisions: AvifPlugin, FileSession, Transform, Describe, PixelBuffer, Narrow
src/adapters/   win/ (file mapping, UTF-8 paths), avif/ (libavif), DefaultPlugin.cpp (composition)
tests/          pvd, core, adapters, e2e (LoadLibrary of AVIF.pvd), guard (source rules), fixtures
docs/           ARCHITECTURE.md (the design) and the task history
scripts/        coverage, import/export checks, build-all, package, fixture download and generation
```

`docs/ARCHITECTURE.md` describes the layering and the canonical interfaces; `AGENTS.md` lists the
rules every change must follow (C++23, `unique_ptr`-only ownership, `std::expected` for expected
failures, 100 % coverage, no commits by agents).

## Tests and fixtures

`ctest --preset release` runs:

- `pvd_tests`, `core_tests`, `adapter_tests` - unit tests of the three layers (doctest).
- `guard_tests` - scans `src/` for forbidden tokens and layering violations.
- `e2e_tests` - loads the built `AVIF.pvd` with `LoadLibraryW`, resolves the eight exports and
  drives them like the host, and reads the VERSIONINFO resource back: every fixture from disk
  and from memory, exact pixel checks on the synthetic fixtures, animation timing, rotation,
  callback abort, rejection of non-AVIF input, behaviour after `pvdExit`, and four threads
  decoding concurrently.
- `check_imports` and `check_exports` - the import-table and export-table policies. Registered
  for the Release configuration only (`add_test(... CONFIGURATIONS Release)`): the `release` test
  presets pass `-C Release`, and on the multi-config `Visual Studio 17 2022 -T ClangCL` fallback
  `ctest -C Release` selects them.

Fixtures live in `tests/fixtures/` and are documented in `tests/fixtures/SOURCES.md` (origin,
licence, what each exercises, expected values). The libavif corpus files are BSD-2-Clause unless
noted there (`kodim03` - Eastman Kodak, unrestricted use; `cosmos1650` - CC BY 3.0; `weld_sato` -
Signature Edits unrestricted-use licence); `tests/fixtures/LIBAVIF_DATA_README.md` is the upstream
notice file. The synthetic fixtures are generated by `scripts/make-synthetic-fixtures.ps1` with
ffmpeg/libaom and are CC0-1.0. Re-download the corpus with `scripts/fetch-fixtures.ps1` (pinned
commit and SHA-256 list in `scripts/libavif-fixtures.sha256`).

## Known limitations (v1)

- HDR (PQ/HLG) sources are converted by matrix only; there is no tone mapping.
- ICC profiles are ignored (the PVD interface has no colour management), as in the bundled decoders.
- Gain maps, layered (`a1lx`) selection, progressive preview rendering and 16-bit output are not
  supported; EXIF orientation is not applied (AVIF's `irot`/`imir` take precedence by spec).
- Files are mapped with `FILE_SHARE_WRITE | FILE_SHARE_DELETE` so Far can keep working with them.
  If another process truncates a file while a page is being decoded, reading the mapped view
  raises a structured exception that the C++ firewall cannot catch - the same behaviour as the
  bundled decoders that map files.
- libavif rejects `clap`/`irot`/`imir` properties that are not marked essential
  (`clap_irot_imir_non_essential.avif`); such files are refused rather than shown untransformed.
