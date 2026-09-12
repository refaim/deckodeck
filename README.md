# pvdkit

Decoder plugins for PictureView 3 (the image viewer plugin for Far Manager 3 by Pavel Skakov),
built for both the x64 and the x86 (32-bit) Far Manager: shared libraries that implement
PictureView's PVD decoder interface v1 (`third_party/pvd/PictureViewPlugin.h`) once, plus one
directory per plugin. Every plugin is a single statically linked `<NAME>.pvd` that imports
`KERNEL32.dll` and nothing else, so it needs no runtime, no WIC codec and no GDI+.

Plugins:

- `plugins/avif` — `AVIF.pvd`, AVIF decoder over libavif 1.4.2 + dav1d 1.5.3 + libyuv. See
  `plugins/avif/README.md` for what it supports and how to install it.

## Build

Requirements (already installed on the reference machine, see `AGENTS.md`): clang-cl 19, lld-link,
llvm-cov / llvm-profdata from the VS 2022 Build Tools, CMake >= 3.28, Ninja (vcpkg's copy is found
automatically), vcpkg in manifest mode. `triplets/x64-windows-static-clang.cmake` and
`triplets/x86-windows-static-clang.cmake` build every dependency with the same toolchain and the
static CRT; `ports/` holds overlay ports (today `libavif`, patched for clang-cl's static-library
merge). Each plugin's libraries are a vcpkg manifest feature named after the plugin (`vcpkg.json`).

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release          # shared + per-plugin unit, adapter, guard and e2e tests + the import/export-table checks
```

Every preset builds every plugin; `-DPVDKIT_PLUGINS=avif` (a semicolon-separated list of plugin
directory names) restricts the build, and the vcpkg install, to those plugins. A plugin is written
to `build/release$env:PVDKIT_BUILD_SUFFIX/plugins/<id>/<NAME>.pvd` (`build/release/plugins/avif/AVIF.pvd`
when the suffix is unset; the suffix lets several builds of the same tree coexist). The `debug`
preset builds the same targets unoptimised.

### 32-bit build (x86)

The same plugins for the 32-bit Far Manager / PictureView come from the `*-x86` presets:

```powershell
cmake --preset release-x86
cmake --build --preset release-x86
ctest --preset release-x86      # the same tests, run as 32-bit processes, plus both table checks
```

The DLLs land in `build/release-x86$env:PVDKIT_BUILD_SUFFIX/plugins/<id>/<NAME>.pvd`
(`IMAGE_FILE_MACHINE_I386`, imports `KERNEL32.dll` only, exports the eight bare `pvd*` names).
`debug-x86` and `coverage-x86` mirror `debug` and `coverage`. The x86 presets use the triplet
`triplets/x86-windows-static-clang.cmake`: the same x64-hosted clang-cl cross-compiles for
`i686-pc-windows-msvc`, and the triplet loads the x86 Visual Studio library environment for the
vcpkg ports (dav1d's meson build needs it to link against the x86 CRT). Install an x86 DLL next
to the 32-bit `0PictureView.dll`; the two architectures are not interchangeable.

`scripts/build-all.ps1` builds both architectures, runs the import and export checks on every
DLL and copies them to `dist/x64/<NAME>.pvd` and `dist/x86/<NAME>.pvd`; `scripts/package.ps1`
does the same from scratch and produces `dist/<NAME>-<version>-x64.zip` and
`dist/<NAME>-<version>-x86.zip` (`<NAME>.pvd`, `README.txt`, `LICENSES.txt`) with their SHA-256.

Every DLL carries a VERSIONINFO resource (version, author, copyright, library versions). A
plugin's name, version and priority are declared once, in `pvdkit_plugin_identity(...)` in its
`plugins/<id>/CMakeLists.txt`; the generated `pvd/PluginConstants.hpp` feeds the C++ side, the
resource and the package. `PVDKIT_AUTHOR` and `PVDKIT_COPYRIGHT` can be overridden at configure
time (`cmake --preset release -DPVDKIT_AUTHOR=...`). The kit itself has no version.

Checks that are part of the definition of done:

```powershell
./scripts/coverage.ps1                                  # 100 % lines and branches on src/** and plugins/*/src/**
./scripts/coverage.ps1 -Preset coverage-x86             # the same gate for the 32-bit build
./scripts/check-imports.ps1 -Path build/release/plugins/avif/AVIF.pvd   # imports == KERNEL32.dll only
./scripts/check-exports.ps1 -Path build/release/plugins/avif/AVIF.pvd   # exports == the eight bare pvd* names
```

`coverage.ps1` configures and builds the `coverage` preset (`-Preset coverage-x86` for x86), runs
every test executable (each plugin's e2e run loads its instrumented DLL and merges its profile too,
and the script fails unless every DLL really wrote one: two raw profiles filed under that plugin's
id by its own e2e process, whose counters execute `Exports.cpp` in the DLL's own mapping — another
plugin's profiles do not count), prints the llvm-cov report, writes
HTML to `build/<preset><suffix>/html` and fails unless `src/**` and `plugins/*/src/**` are at
100 % lines and 100 % branches. It is a whole-tree gate: run it with every plugin enabled.

## Layout

```
src/pvd/        PVD boundary: value types, IPlugin/IFileSession, Shim (marshalling + firewall),
                Exports.cpp (the 8 exports, compiled once per plugin), Plugin.def, Plugin.rc,
                PluginConstants.hpp.in (generated per plugin), PluginIdentity.hpp, PvdApi.hpp
src/core/       decisions: CodecPlugin, FileSession, Transform, PixelBuffer, Narrow, and the
                IDecoder / IFileSource / IImageDescriber contracts
src/adapters/   win/ (file mapping, UTF-8 paths)
plugins/<id>/   one plugin: src/core, src/adapters/<lib>, src/DefaultPlugin.cpp (composition),
                tests/{core,adapters,e2e}, fixtures/, scripts/, package/README.txt.in, README.md, DESIGN.md
tests/          pvd, core, adapters (win), guard (source rules) — shared tests; e2e holds the
                host driver and VERSIONINFO test every plugin's e2e executable compiles in
cmake/          toolchains, vcpkg wrapper, pvdkit-plugin.cmake (the plugin helpers)
docs/           ARCHITECTURE.md (the shared design) and the task history
scripts/        coverage, import/export checks, build-all, package (all plugins)
```

`docs/ARCHITECTURE.md` describes the layering, the canonical interfaces and how a plugin is added;
`AGENTS.md` lists the rules every change must follow (C++23, `unique_ptr`-only ownership,
`std::expected` for expected failures, 100 % coverage, no commits by agents).

## Tests

`ctest --preset release` runs:

- `pvd_tests`, `core_tests`, `adapter_tests` - unit tests of the three shared layers (doctest);
  `pvd_tests` compiles `Exports.cpp` against a generated test identity.
- `guard_tests` - scans `src/` and every `plugins/*/src/` for forbidden tokens, layering
  violations and cross-root includes.
- `<id>_core_tests`, `<id>_adapter_tests`, `<id>_e2e_tests` - each plugin's own tests; the e2e
  executable loads the built DLL with `LoadLibraryW`, drives the eight exports like the host on
  the plugin's fixtures and reads the VERSIONINFO resource back.
- `<id>_check_imports` and `<id>_check_exports` - the import-table and export-table policies per
  plugin. Registered for the Release configuration only (`add_test(... CONFIGURATIONS Release)`):
  the `release` test presets pass `-C Release`, and on the multi-config
  `Visual Studio 17 2022 -T ClangCL` fallback `ctest -C Release` selects them.

Fixtures belong to their plugin (`plugins/<id>/fixtures/` with a `SOURCES.md`).
