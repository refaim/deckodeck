# AVIF.pvd — rules for every agent working in this repository

Read `docs/ARCHITECTURE.md` before touching anything. That file is the design; this file is the law.
If the two disagree, stop and report the conflict instead of picking one.

## What we are building

`AVIF.pvd`: an AVIF decoder plugin for PictureView 3 (image viewer plugin for Far Manager 3 x64,
by Pavel Skakov). The plugin speaks the PVD decoder interface v1 defined in
`third_party/pvd/PictureViewPlugin.h` (UTF-8 copy of `../sdk/PictureViewPlugin.h`; never edit it).
Reference implementations of that interface: `../examples/pvdBMP.cpp`, `../examples/pvdIJL.cpp`.
Bundled decoders for comparison (binary only): `../plugin/*.pvd`.

Decoding is done by libavif + dav1d + libyuv, linked statically. No WIC, no GDI+, no system codecs.

## Non-negotiable rules

1. **Language.** C++23 only (`/clang:-std=c++23`, guarded by `static_assert(__cplusplus >= 202302L)`).
   No C translation units of our own. Third-party C (libavif, dav1d, libyuv) is consumed as static
   libraries, behind adapters, and is never modified.
2. **Ownership.** `std::unique_ptr` only. Forbidden anywhere under `src/`: `new`, `delete`, `malloc`,
   `calloc`, `realloc`, `free`, `shared_ptr`, `weak_ptr`, owning raw pointers, manually managed arrays.
   Use `std::make_unique`, `std::make_unique_for_overwrite`, `std::vector`, `std::string`, `std::array`.
3. **Non-owning.** `std::span` for memory ranges, references for object dependencies,
   `std::string_view` for input strings, `std::optional<std::reference_wrapper<T>>` only if an optional
   non-owning reference is truly unavoidable. A raw pointer may appear only on the line inside an
   adapter that calls the foreign API. Never as a class member, never as a parameter or return type of
   our own functions. The single exception is `src/pvd/Exports.cpp`, which speaks the C ABI and is a
   pure marshalling layer with no logic.
4. **Adapters.** Foreign APIs (libavif, Win32, the PVD C ABI) are touched only inside `src/adapters/**`
   and `src/pvd/Exports.cpp`, with one exception: `src/pvd/PvdApi.hpp` includes `<Windows.h>` once
   (lean, NOMINMAX) solely to provide the typedefs the SDK header needs, and wraps the SDK header in
   `extern "C"`; every other pvd file gets the SDK types through it. `src/core/**` includes no foreign
   headers (`<windows.h>`, `avif/avif.h`, ...). Adapters are one-to-one over the foreign API and
   contain no decisions; decisions live in core.
5. **Errors.** Expected failures (not an AVIF, corrupt file, unsupported feature, page out of range,
   host abort, size limits, file cannot be opened) are `std::expected<T, Error>`. Exceptions are ON and
   reserved for OOM (`std::bad_alloc`) and broken invariants. Never throw for control flow. The 8
   exports are wrapped in a catch-all firewall that turns any exception into `FALSE` / no-op. Nothing
   ever propagates into the host.
6. **TDD.** Failing test first, then the minimal code that passes, then refactor. Tests and the code
   they cover land in the same unit of work. No "tests later".
7. **Coverage.** 100% lines AND 100% branches on `src/**`, measured with llvm-cov via the `coverage`
   preset/script. Not 99.9. If a branch cannot be exercised, restructure the code so the branch does
   not exist. No `LCOV_EXCL`-style markers, no `__builtin_unreachable`, no `[[assume]]` to hide
   branches, no `#ifdef` tricks.
8. **Warnings.** `/W4 /WX` (and `-Wall -Wextra` semantics through clang). Silencing a warning requires
   a one-line written reason next to the pragma/flag.
9. **Reuse before custom infrastructure.** doctest for tests, libavif helpers for AVIF, std for
   everything else. Do not write an ISOBMFF parser, a YUV converter, a thread pool or a test framework.
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
  triplet `x64-windows-static`. Dependencies: `libavif[dav1d]` (pulls `libyuv`), `doctest`.
  A warm-up build of exactly these packages may already be in vcpkg's binary cache.
- Static CRT: `/MT` (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>`).
  The final `AVIF.pvd` must import only `KERNEL32.dll`. Verify with
  `objdump -p AVIF.pvd | grep "DLL Name"` (Git Bash) or `dumpbin /dependents`.
- Git Bash quirk: set `MSYS_NO_PATHCONV=1` when passing `/flags` to cl/clang-cl from bash, otherwise
  `/c` and `/nologo` become file paths. Prefer CMake presets over ad-hoc compiler invocations.
- clang-cl 19 silently ignores `/std:c++23preview` (drops to C++14). Always `/clang:-std=c++23`.

## Layout

```
AGENTS.md CLAUDE.md README.md docs/ARCHITECTURE.md
CMakeLists.txt CMakePresets.json vcpkg.json cmake/ scripts/
third_party/pvd/PictureViewPlugin.h
src/pvd/        PVD boundary: value types, IPlugin/IFileSession, Shim (marshalling + firewall), Exports.cpp, AVIF.def
src/core/       logic: AvifPlugin, FileSession, Transform, Describe, PixelBuffer, IDecoder, IFileSource
src/adapters/   win/ (FileMapping, Utf8), avif/ (Decoder over libavif)
tests/pvd/      shim, firewall, context handle, Progress with fakes (doctest)
tests/core/     core logic with fake decoder / file source (doctest)
tests/adapters/ real adapters on real files
tests/e2e/      LoadLibrary(AVIF.pvd), drive the 8 exports on fixtures
tests/guard/    source-scanning test enforcing rules 2–4
tests/fixtures/ AVIF sample files + SOURCES.md (origin, licence, expected values)
```

## Definition of done for any task

- Tests were written first and are all green (`ctest` output included in the report).
- Coverage gate passes at 100% lines / 100% branches on `src/**` (numbers included).
- Guard test passes.
- Zero warnings.
- The Release preset produces `AVIF.pvd` whose import table lists only `KERNEL32.dll`.
