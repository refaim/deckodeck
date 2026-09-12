# pvdkit — rules for every agent working in this repository

Read `docs/ARCHITECTURE.md` before touching anything. That file is the design; this file is the law.
If the two disagree, stop and report the conflict instead of picking one. A plugin's own design
lives next to it (`plugins/<id>/DESIGN.md`).

## What we are building

`pvdkit`: decoder plugins for PictureView 3 (image viewer plugin for Far Manager 3, by Pavel
Skakov), as a monorepo of shared libraries plus one directory per plugin. Every plugin speaks the
PVD decoder interface v1 defined in `third_party/pvd/PictureViewPlugin.h` (UTF-8 copy of
`../sdk/PictureViewPlugin.h`; never edit it). Reference implementations of that interface:
`../examples/pvdBMP.cpp`, `../examples/pvdIJL.cpp`. Bundled decoders for comparison (binary only):
`../plugin/*.pvd`. Plugins today: `plugins/avif` → `AVIF.pvd` (libavif + dav1d + libyuv).

Every plugin ships x64 and x86, links its codec libraries statically, and imports `KERNEL32.dll`
only. No WIC, no GDI+, no system codecs.

## Non-negotiable rules

1. **Language.** C++23 only (`/clang:-std=c++23`, guarded by `static_assert(__cplusplus >= 202302L)`).
   No C translation units of our own. Third-party C (libavif, dav1d, libyuv, ...) is consumed as
   static libraries, behind adapters, and is never modified.
2. **Ownership.** `std::unique_ptr` only. Forbidden anywhere under `src/` and `plugins/*/src/`:
   `new`, `delete`, `malloc`, `calloc`, `realloc`, `free`, `shared_ptr`, `weak_ptr`, owning raw
   pointers, manually managed arrays. Use `std::make_unique`, `std::make_unique_for_overwrite`,
   `std::vector`, `std::string`, `std::array`.
3. **Non-owning.** `std::span` for memory ranges, references for object dependencies,
   `std::string_view` for input strings, `std::optional<std::reference_wrapper<T>>` only if an optional
   non-owning reference is truly unavoidable. A raw pointer may appear only on the line inside an
   adapter that calls the foreign API. Never as a class member, never as a parameter or return type of
   our own functions. The single exception is `src/pvd/Exports.cpp`, which speaks the C ABI and is a
   pure marshalling layer with no logic.
4. **Adapters.** Foreign APIs (codec libraries, Win32, the PVD C ABI) are touched only inside
   `src/adapters/**`, `plugins/*/src/adapters/**` and `src/pvd/Exports.cpp`, with one exception:
   `src/pvd/PvdApi.hpp` includes `<Windows.h>` once (lean, NOMINMAX) solely to provide the typedefs
   the SDK header needs, and wraps the SDK header in `extern "C"`; every other pvd file gets the SDK
   types through it. `core/**` (shared or a plugin's) includes no foreign headers (`<windows.h>`,
   `avif/avif.h`, ...). Adapters are one-to-one over the foreign API and contain no decisions;
   decisions live in core. Shared `src/**` never includes anything from `plugins/**`; a plugin
   never includes another plugin. Each plugin has its own include root (`plugins/<id>/src`).
5. **Errors.** Expected failures (not our format, corrupt file, unsupported feature, page out of
   range, host abort, size limits, file cannot be opened) are `std::expected<T, Error>`. Exceptions
   are ON and reserved for OOM (`std::bad_alloc`) and broken invariants. Never throw for control
   flow. The 8 exports are wrapped in a catch-all firewall that turns any exception into `FALSE` /
   no-op. Nothing ever propagates into the host.
6. **TDD.** Failing test first, then the minimal code that passes, then refactor. Tests and the code
   they cover land in the same unit of work. No "tests later".
7. **Coverage.** 100% lines AND 100% branches on `src/**` and `plugins/*/src/**`, measured with
   llvm-cov via the `coverage` preset/script. Not 99.9. If a branch cannot be exercised, restructure
   the code so the branch does not exist. No `LCOV_EXCL`-style markers, no `__builtin_unreachable`,
   no `[[assume]]` to hide branches, no `#ifdef` tricks.
8. **Warnings.** `/W4 /WX` (and `-Wall -Wextra` semantics through clang). Silencing a warning requires
   a one-line written reason next to the pragma/flag.
9. **Reuse before custom infrastructure.** doctest for tests, the codec library's helpers for its
   format, std for everything else. Do not write an ISOBMFF parser, a YUV converter, a thread pool
   or a test framework. Do not duplicate shared code into a plugin: if two plugins need it, it
   belongs in `src/`.
10. **No commits.** The orchestrator commits. Never run `git commit`, `git push`, `git reset`,
    `git checkout -- <file>`, `git clean`, or anything destructive. Staging (`git add`) is fine.
    Do not use git worktrees; work directly in this checkout.
11. **Stay inside the repo.** Write only under this repository, the CMake build dirs under `build/`,
    vcpkg's own directories, and `%TEMP%`. Never touch `C:\Tools\FarManager`.
12. **Report honestly.** Finish with: what was done, what was not, the exact commands run and their
    results (test counts, coverage numbers, import table). No claim without the output that proves it.

## Toolchain (already installed, do not install other compilers)

- clang-cl 19.1.5, lld-link, llvm-cov, llvm-profdata from VS 2022 Build Tools:
  `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\`.
  MSVC STL 14.44.35207, Windows SDK 10.0.26100. clang-cl auto-detects both; no vcvars needed.
- CMake 4.4 on PATH. Ninja: use vcpkg's downloaded copy or `scoop install ninja` if absent.
  Fallback generator: `Visual Studio 17 2022` with `-T ClangCL`.
- vcpkg at `C:\Users\Roma\scoop\apps\vcpkg\current` (`vcpkg` on PATH), manifest mode (`vcpkg.json`),
  triplets `x64-windows-static-clang` and `x86-windows-static-clang` (`triplets/`; the x86 one
  cross-compiles with the same x64-hosted clang-cl and loads the x86 vcvars for the ports).
  Dependencies: `doctest` for the kit, one manifest feature per plugin (`avif`: `libavif[dav1d]`,
  which pulls `libyuv`). Changing a file under `cmake/` or `triplets/` changes every port's ABI
  hash and rebuilds the ports once. A warm-up build may already be in vcpkg's binary cache.
- Static CRT: `/MT` (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>`).
  Every final `.pvd` must import only `KERNEL32.dll`. Verify with
  `objdump -p <NAME>.pvd | grep "DLL Name"` (Git Bash) or `dumpbin /dependents`.
- Git Bash quirk: set `MSYS_NO_PATHCONV=1` when passing `/flags` to cl/clang-cl from bash, otherwise
  `/c` and `/nologo` become file paths. Prefer CMake presets over ad-hoc compiler invocations.
- clang-cl 19 silently ignores `/std:c++23preview` (drops to C++14). Always `/clang:-std=c++23`.
- Build directories are `build/<preset>$env:PVDKIT_BUILD_SUFFIX`; set a suffix per task so
  concurrent agents never share a Ninja/CMake state directory.

## Layout

```
AGENTS.md CLAUDE.md README.md docs/ARCHITECTURE.md
CMakeLists.txt CMakePresets.json vcpkg.json cmake/ (incl. pvdkit-plugin.cmake) scripts/ ports/ triplets/
third_party/pvd/PictureViewPlugin.h
src/pvd/        PVD boundary: value types, IPlugin/IFileSession, Shim (marshalling + firewall),
                Exports.cpp, Plugin.def, Plugin.rc, PluginConstants.hpp.in, PluginIdentity.hpp
src/core/       logic: CodecPlugin, FileSession, Transform, PixelBuffer, Narrow, IDecoder,
                IFileSource, IImageDescriber
src/adapters/   win/ (FileMapping, FileSource, Utf8)
plugins/<id>/   one plugin: CMakeLists.txt (identity + targets), src/core/, src/adapters/<lib>/,
                src/DefaultPlugin.cpp (composition root), tests/{core,adapters,e2e}/, fixtures/,
                scripts/, package/README.txt.in, README.md, DESIGN.md
tests/pvd/      shim, firewall, context handle, Progress, Exports with fakes (doctest)
tests/core/     core logic with fake decoder / file source / describer (doctest)
tests/adapters/ the win adapter on real files
tests/e2e/      the host driver + VERSIONINFO test, compiled into every plugin's e2e executable
tests/guard/    source-scanning test enforcing rules 2–4 over src/ and plugins/*/src/
```

## Definition of done for any task

- Tests were written first and are all green (`ctest` output included in the report).
- Coverage gate passes at 100% lines / 100% branches on `src/**` and `plugins/*/src/**` (numbers included).
- Guard test passes.
- Zero warnings on both architectures.
- The Release presets produce every `<NAME>.pvd` whose import table lists only `KERNEL32.dll` and
  whose export table is exactly the eight bare `pvd*` names (`<id>_check_imports`, `<id>_check_exports`).
