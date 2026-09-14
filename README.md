# deckodeck

**deckodeck** (repository: <https://github.com/refaim/deckodeck>) is a pair of decoder plugins for
PictureView 3, the image viewer plugin for Far Manager 3 by Pavel Skakov: `AVIF.pvd` and
`RPGMVP.pvd`, built for both the x64 and the x86 (32-bit) Far Manager. Both implement
PictureView's PVD decoder interface v1 (`third_party/pvd/PictureViewPlugin.h`) over shared
libraries, and every plugin is a single statically linked `<NAME>.pvd` that imports `KERNEL32.dll`
and nothing else, so it needs no runtime, no WIC codec and no GDI+. New releases are published as
GitHub Releases on the repository above and announced in the PictureView forum thread; that is
this project's release channel. The shared libraries underneath both plugins (`src/`, the
`pvdkit_core`/`pvdkit_pvd`/`pvdkit_win` targets, the `pvdkit::` C++ namespace, the
`PVDKIT_BUILD_SUFFIX` environment variable, `cmake/pvdkit-*.cmake`, and the coverage script's
`pvdkit-<id>-*.profraw` profile filenames) keep their original name, **pvdkit**, throughout this
repository and its documentation; "deckodeck" names the product and the repository, "pvdkit" names
that shared PVD kit layer underneath it.

Plugins:

- `plugins/avif` — `AVIF.pvd`, AVIF decoder over libavif 1.4.2 + dav1d 1.5.3 + libyuv. See
  `plugins/avif/README.md` for what it supports and how to install it.
- `plugins/rpgmvp` — `RPGMVP.pvd`, decoder for RPG Maker MV/MZ encrypted PNG images
  (`.rpgmvp`/`.png_`) over libspng + zlib. See `plugins/rpgmvp/README.md` for what it supports and
  how to install it.

The repository's own code is licensed under the MIT License; see `LICENSE`.

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
`dist/<NAME>-<version>-x86.zip` with their SHA-256.

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
./scripts/lint.ps1 -BuildDir build/release -ReleaseDir build/release          # static analysis, zero findings (x64)
./scripts/lint.ps1 -BuildDir build/release-x86 -ReleaseDir build/release-x86  # the same for the 32-bit build
cmake --preset asan; cmake --build --preset asan; ctest --preset asan  # AddressSanitizer over the whole suite (x64)
```

`lint.ps1` is the static-analysis gate and exits non-zero on any finding. `-BuildDir` is any
configured build of the architecture (its `compile_commands.json` feeds clang-tidy and cppcheck;
every preset exports one, and `debug` works as well as `release`), `-ReleaseDir` the Release
build whose `plugins/<id>/<NAME>.pvd` BinSkim reads; `scripts/package.ps1` passes its own
from-scratch release directory as both, once per architecture, before it zips anything. The
tools: clang-format (`--dry-run --Werror`, `.clang-format`: Microsoft style, 120 columns, 4
spaces) over `src/**`, `plugins/*/src/**`, `tests/**` and `plugins/*/tests/**`; clang-tidy
(`.clang-tidy`: `clang-analyzer-*`, `bugprone-*`, `performance-*`, `portability-*`, warnings as
errors) over every translation unit of the compile database, in parallel, reporting headers under
`src/`, `tests/` and every plugin's `src/` and `tests/`, with `tests/.clang-tidy` and
`plugins/<id>/tests/.clang-tidy` relaxing two checks for test code; cppcheck
(`--enable=warning,performance,portability`, the same compile database restricted to `src/` and
`plugins/*/src/`, `cppcheck-suppressions.txt`); PSScriptAnalyzer over
`scripts/`, `plugins/*/scripts/` and the root `.psd1` files (`PSScriptAnalyzerSettings.psd1`);
and BinSkim over every release `<NAME>.pvd` (`binskim.psd1`: error and warning results fail; the
release build links `/DEBUG:NONE`; `binskim.psd1` lists which rules that leaves unevaluated and
which mitigations - Spectre, CET shadow stack, `/sdl` - are deliberately not done and why).
BinSkim cannot judge `/HIGHENTROPYVA` on a DLL (BA2015 is not applicable), so the script reads
that bit from every 64-bit image with llvm-readobj itself. `-Tools` selects a subset
(`-Tools clang-format,cppcheck`); `-Jobs` is the number of parallel clang-tidy processes, half
the logical cores by default, and every tool runs at below-normal priority so the machine stays
usable meanwhile. Suppressions live in those files, each with its reason. A new
plugin copies `tests/.clang-tidy` into its own `tests/`. The gate is not part of `ctest`.

The `asan` preset builds every target - the plugin DLLs included - with AddressSanitizer
(`-fsanitize=address /Od /Zi`, `/MT`, RelWithDebInfo so the release ports are used) and runs the
whole suite under it, the leak scenarios and their hostile corpus included; any ASan report fails
the run. It is x64 only, and it is a memory-error gate, not a leak gate: LeakSanitizer does not
exist in clang 19's Windows runtime (`detect_leaks is not supported on this platform`), so leaks
are the level-1 job of `<id>_leak_tests` in every preset. Only our own code is instrumented: the
vcpkg ports (libavif, dav1d, libyuv) are built without ASan, so inside them only the interceptors
(`malloc`, `free`, `memcpy`, ...) see anything - the hostile-corpus claim under this preset is
about our code on hostile input and the host-side read of every byte of every page handed out,
not about a port's own instructions. `scripts/package.ps1` runs it from scratch before zipping.
Details and the other toolchain caveats: `docs/ARCHITECTURE.md` par. 4.

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
                tests/{core,adapters,e2e}, fixtures/, scripts/,
                package/{readme_en.txt,readme_ru.txt,ChangeLog}, README.md, DESIGN.md
tests/          pvd, core, adapters (win), guard (source rules) — shared tests; e2e holds the
                host driver and VERSIONINFO test every plugin's e2e executable compiles in;
                support holds the leak accounting, the hostile corpus generator and the leak
                scenarios every plugin's leak executable compiles in
cmake/          toolchains, vcpkg wrapper, pvdkit-plugin.cmake (the plugin helpers)
docs/           ARCHITECTURE.md (the shared design) and the task history
scripts/        coverage, import/export checks, lint, build-all, package (all plugins)
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
- `<id>_package_docs` - repeats the identity, encoding and CRLF checks on the static distribution
  documents copied into the plugin's package staging directory.
- `leakcheck_tests` and `<id>_leak_tests` - the leak gate (level 1 of `docs/tasks/task10-leaks.md`,
  every preset, both architectures). `tests/support/LeakCheck` snapshots the process - live blocks
  and bytes of the process heap (`HeapWalk`; one mechanism for Debug and Release because every
  static CRT in the process, the plugin DLL's included, allocates from that heap, which
  `_CrtMemCheckpoint` could never see across modules), the handle count, the mapped views
  (committed `MEM_MAPPED` regions via `VirtualQuery`: a leaked `MapViewOfFile` is neither a block
  nor a handle and barely moves the commit charge), and the commit charge as a coarse
  cross-check - around a warm-up and N = 200 host-level operations, and requires no growth in
  blocks, bytes, handles or views (a pass that grows gets a second pass and both are printed; the
  second decides only when the first grew by at most two small blocks, else the first stands -
  a cache that fills after warm-up is a finding; growth is reported as the list of blocks that
  appeared). `leakcheck_tests` proves the accounting on deliberately leaking fakes (a block, a
  handle, a view); `<id>_leak_tests` (registered by `pvdkit_add_plugin_e2e_tests`, no plugin code needed)
  drives the real DLL like the host through the e2e driver over the plugin's fixture directory,
  which it classifies through the DLL itself: disk and memory round trips with init/exit, close
  without free, a decode aborted at each callback step, every rejection path, two pages
  outstanding freed in both orders, N sessions open at once, init/exit cycled, eight threads
  decoding concurrently, `LoadLibrary`/`FreeLibrary` cycled, host-like browsing with a sliding
  window of three open files (aborts, page switches, close with pages out), and the hostile
  corpus - every rejected fixture plus 200 deterministic mutants of the accepted ones (byte flips,
  truncations, corrupted box sizes, spliced and trailing garbage, zeroed ranges; generated into
  `%TEMP%` at test time, seed 20260912) opened in both modes, each of which must be refused or
  decode to a usable image. Every scenario prints one `[leak] ...` line with its deltas and
  timings; `PVDKIT_LEAK_ITERATIONS=5000` turns a run into a soak. The "< 30 s per architecture"
  budget is met by the plain presets (~15 s Release, ~24-27 s Debug); the instrumented `coverage`
  and `asan` builds run the same N and take 30-42 s, by design. The unit-level counterpart lives
  in `adapter_tests` (`FileMapping`/`FileSource` open and close) and `<id>_adapter_tests`
  (decoder create/decode/destroy, refusals, the composed plugin).

Fixtures belong to their plugin (`plugins/<id>/fixtures/` with a `SOURCES.md`).
