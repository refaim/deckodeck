# Task 7 — restructure into the `pvdkit` monorepo

Goal: turn this repository (one plugin, AVIF.pvd) into `pvdkit`: shared libraries plus one
directory per plugin, so that the next plugin (RPGMVP.pvd, Task 8) adds only its adapter, its
composition root, its fixtures and its e2e tests. **Pure restructuring — zero behaviour change.**
Every gate that passes today must pass identically afterwards: all tests, 100 % line+branch
coverage on both architectures, KERNEL32-only imports, eight bare exports, the two zips.

Read `AGENTS.md`, `docs/ARCHITECTURE.md`, `README.md` first. Roma's decision: monorepo, **no git
submodules**, every plugin ships x64 and x86.

## Target layout

```
pvdkit/
  AGENTS.md  CLAUDE.md  README.md  CMakeLists.txt  CMakePresets.json  vcpkg.json
  cmake/  triplets/  ports/  scripts/  third_party/pvd/
  docs/ARCHITECTURE.md          (shared design: layers, rules, build, gates)
  docs/tasks/                   (history, keep as is)
  src/pvd/                      shared: Types, Plugin, Progress, Shim, ContextHandle, Firewall,
                                Exports.cpp, PvdApi.hpp, PluginFactory.hpp, AVIF.def → Plugin.def
                                (generic name; exports are the same for every plugin)
  src/core/                     shared: Error, IDecoder, IFileSource, PixelBuffer, FileSession,
                                AvifPlugin → GenericPlugin? (see "Open design points")
  src/adapters/win/             shared: FileMapping, FileSource, Utf8
  plugins/avif/
    CMakeLists.txt              target AVIF.pvd, links pvdkit_pvd + pvdkit_core + this adapter
    src/adapters/avif/          Decoder over libavif (moved from src/adapters/avif)
    src/core/Transform.*, Describe.*   AVIF-specific (clap/irot/imir, CICP description)
    src/DefaultPlugin.cpp       composition root for AVIF (makePlugin, PluginConstants values)
    src/AVIF.rc(.in)            VERSIONINFO
    tests/adapters/  tests/e2e/  tests/core/ (Transform/Describe tests)  fixtures/
    plugin.cmake or vcpkg feature: libavif[dav1d]
  plugins/rpgmvp/               (Task 8; empty now — do not create)
  tests/pvd/  tests/core/  tests/adapters/ (win)  tests/guard/     shared tests
  scripts/coverage.ps1 -Preset  scripts/check-imports.ps1  scripts/package.ps1 (all plugins)
```

Library target names: `pvdkit_pvd`, `pvdkit_core`, `pvdkit_win`; per plugin `avif_adapter`,
`avif_plugin` (SHARED, OUTPUT_NAME AVIF, SUFFIX .pvd). Namespace `avifpvd` → `pvdkit` for shared
code; plugin-specific code may keep a sub-namespace (`pvdkit::avif`).

## Open design points — decide, implement, document in ARCHITECTURE

1. `core::AvifPlugin` is generic except for its name and for calling `describe(meta)` and
   `Transform`. Split: `core::CodecPlugin` (shared: signature check via factory, file/memory mode,
   session creation, ImageInfo from a `describe` callback or from a per-plugin `IImageDescriber`)
   vs the AVIF-specific describer and transforms. `FileSession` currently calls
   `Transform::apply` when `hasTransforms` — either keep `Transforms` in `ImageMeta` as a generic
   concept (HEIF-family formats all have clap/irot/imir; PNG has none and its adapter just leaves
   them empty) and keep `Transform` shared, or inject a transform step. Prefer the first: it is
   less machinery and `Transforms{}` is a valid "none". Then `Transform.*` stays in `src/core`
   and only `Describe.*` (CICP names etc.) moves to `plugins/avif`. `ImageMeta` fields that are
   AVIF-only (`cicp`, `chroma`) — keep them; a PNG adapter fills `cicp = {2,2,2}` /
   `Yuv444`-equivalent or we generalise `chroma` to an `std::optional`. Decide and document.
2. `PluginConstants.hpp`: split into the shared shape (`kInterfaceVersion`, helpers) and per-plugin
   values (`kPluginPriority`, `kPluginName`, `kPluginVersion`) provided by each plugin's
   composition root; `Exports.cpp` gets the defaults from `makePlugin()`'s plugin info or from a
   per-plugin constants header passed by the plugin target (`target_compile_definitions` or a
   generated header). One source of truth per plugin, including the .rc.
3. The guard test's layering rules must be re-expressed for the new tree (shared `src/**` never
   includes `plugins/**`; a plugin's adapter never includes another plugin; `Exports.cpp` and
   `DefaultPlugin.cpp` exceptions as before). Self-tests both polarities.
4. `coverage.ps1`: gate over `src/**` **and** `plugins/*/src/**`; discovers every `*.pvd` under the
   build dir and requires each DLL's own profile (the check that exists today, generalised to N
   plugins). `check_imports` ctest per plugin. `package.ps1`: loops over plugins, produces
   `dist/<NAME>-<version>-{x64,x86}.zip`. Version per plugin (each plugin's `project()` or a
   `PLUGIN_VERSION` variable), not one global version.
5. Presets: unchanged names (`debug`, `release`, `coverage`, `*-x86`); the top-level build builds
   every plugin. Optional `-DPVDKIT_PLUGINS=avif` to restrict.

## Mechanics
- Use `git mv` for every move so history follows. Update includes, CMake, docs, README, guard
  rules, scripts. No functional edits beyond what the split requires; if you must change logic
  (e.g. the CodecPlugin split), do it with tests first and list it explicitly.
- Rename the repository directory: Roma asked for the monorepo to be called `pvdkit`; the
  orchestrator will move `C:\Users\Roma\Dev\PictureView3\avif` → `...\pvdkit` after this task —
  do not move the directory yourself; do not hard-code the directory name anywhere.
- `AVIFPVD_BUILD_SUFFIX` → `PVDKIT_BUILD_SUFFIX` (keep reading the old name as a fallback for one
  release, or just rename everywhere including README; say which).
- No commits; `git add -A` at the end (the orchestrator commits). No worktrees.

## Verify and report
Fresh `PVDKIT_BUILD_SUFFIX=-t7`: `ctest --preset release` (all tests incl. check_imports),
`ctest --preset release-x86`, `ctest --preset debug`, `scripts/coverage.ps1` and
`-Preset coverage-x86` (both 100/100, quote totals), `scripts/package.ps1` (two zips, SHA-256,
listing), `llvm-readobj --coff-exports` on both DLLs, `git status --short | head` showing renames
(`R`) rather than delete+add where possible. List every design decision taken for the open points.
