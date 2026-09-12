# Task 6 — 32-bit (x86) build of AVIF.pvd

Goal: the same plugin, built for 32-bit Far Manager / PictureView x86, with the same guarantees:
static CRT, imports `KERNEL32.dll` only, exactly the eight undecorated `pvd*` exports, every test
green, the coverage and import gates passing for the x86 configuration too. No change of behaviour.

Read `AGENTS.md`, `docs/ARCHITECTURE.md` and `README.md` first. The x64 build is the reference; do
not regress it — every x64 preset must keep working exactly as before (run them at the end).

Already prepared by the orchestrator (verify, adjust if wrong):
- `triplets/x86-windows-static-clang.cmake` — vcpkg triplet, chainloads
  `cmake/clang-cl-x86.toolchain.cmake` (clang-cl from the x64 LLVM dir cross-compiling with
  `CMAKE_<LANG>_COMPILER_TARGET = i686-pc-windows-msvc`).
- The vcpkg binary cache has been warmed for `doctest`, `libavif[dav1d]`, `libyuv`,
  `libjpeg-turbo` on that triplet with the overlay port (`ports/libavif`) — a fresh configure should
  restore, not rebuild. If it rebuilds, fine; if a port fails on x86, report the exact error.

## Deliverables

1. **Presets** in `CMakePresets.json`: `debug-x86`, `release-x86`, `coverage-x86` (configure, build,
   test), mirroring the x64 ones: `VCPKG_TARGET_TRIPLET = x86-windows-static-clang`, toolchain via
   the same vcpkg toolchain file with the chainload above, `binaryDir` =
   `${sourceDir}/build/${presetName}$env{AVIFPVD_BUILD_SUFFIX}`, `VCPKG_HOST_TRIPLET = x64-windows`
   (host tools stay x64). If CMake's MSVC platform detection needs `CMAKE_SYSTEM_PROCESSOR`/
   `CMAKE_GENERATOR_PLATFORM` hints or `-m32` flags for Ninja + clang-cl, add them in the x86
   toolchain (keep the x64 toolchain untouched). Verify the compiler really targets i686: the
   configure log's compiler ID line, `llvm-readobj --file-header` on any object (Machine
   `IMAGE_FILE_MACHINE_I386`), and the final DLL header.

2. **Exports on x86.** `__stdcall` decorates x86 symbols as `_pvdInit@0`, `_pvdFileOpen@28`, etc.
   The host looks up the bare names. The `.def` file's `EXPORTS` list with bare names makes the
   linker export them undecorated (lld-link resolves `pvdInit` to `_pvdInit@0` via the def) — verify
   with `llvm-readobj --coff-exports` that the eight exported names are exactly `pvdExit,
   pvdFileClose, pvdFileOpen, pvdInit, pvdPageDecode, pvdPageFree, pvdPageInfo, pvdPluginInfo` with
   no leading underscore and no `@N`. If lld-link needs the decorated internal names in the def
   (`pvdInit = _pvdInit@0`), do NOT hand-write them: generate them, or switch the export mechanism
   to `__pragma(comment(linker, "/EXPORT:pvdInit=_pvdInit@0"))` guarded by `#ifdef _M_IX86` in
   `Exports.cpp` — pick the mechanism that keeps ONE source of truth for both architectures and
   document it in ARCHITECTURE §3.4. The e2e test (`GetProcAddress` by bare name) is the acceptance
   test for this; it must pass on x86 without modification of its lookup.

3. **Sizes and types.** Audit for x86 correctness: `std::size_t` is 32-bit — every place that
   multiplies width × height × bytesPerPixel, computes pitch × height, compares against
   `maxPixels`, or converts `std::uint64_t` file sizes (`GetFileSizeEx`, `MapViewOfFile` view size,
   `INT64 lFileSize` from the host, `avifDecoderSetIOMemory` size) must still be done in 64-bit
   and then checked before narrowing to `size_t`/`SIZE_T`. A 3 GB file must be refused cleanly on
   x86 (`FileOpenFailed`/`TooLarge`), not truncated or wrapped. `static_assert`s or explicit checks
   where a narrowing happens; add unit tests that exercise the narrowing paths with values above
   `SIZE_MAX` on 32-bit (compile-time `if constexpr (sizeof(std::size_t) == 4)` in tests is fine,
   but the checks in `src/` must be architecture-independent code without `#ifdef`). Check
   `hardware_concurrency`, `lround` clamping, and any `reinterpret_cast` between pointer and integer.

4. **Gates on x86.** `scripts/check-imports.ps1` must pass on the x86 DLL (KERNEL32.dll only —
   watch for `msvcrt`/`ucrtbase` or `___chkstk`-style helper imports, and for the x86-specific
   `__security_cookie`/`__report_gsfailure` pulling nothing dynamic). `scripts/coverage.ps1` must
   accept a preset name argument (default `coverage`) so it can run `coverage-x86` with the same
   100 % lines / branches gate; the DLL profile check must work for the x86 DLL too. `ctest
   --preset release-x86` must include `check_imports` for the x86 DLL.

5. **Tests on x86.** All five test executables build and pass as 32-bit processes (they are, since
   the whole tree is x86). The e2e concurrency test, long-path tests and the `maxPixels` tests must
   still be meaningful on x86 (`16384 × 16384 × 4` = 1 GiB is allocatable in a 32-bit process only
   sometimes — if a test allocates that much, make the limit test use the `TooLarge` path without
   allocating, and note it).

6. **Output**: `build/release-x86<suffix>/src/pvd/AVIF.pvd`. README: a section on the x86 build
   (presets, where the DLL lands, that it is for 32-bit Far/PictureView). `scripts/build-all.ps1`
   (optional but welcome): builds release x64 and x86 and copies both into `dist/x64/AVIF.pvd` and
   `dist/x86/AVIF.pvd` after running check-imports on each.

## Rules
TDD for every logic change (the narrowing checks in item 3 need failing tests first); no commits
(`git add` at the end); no worktrees; `AVIFPVD_BUILD_SUFFIX=-t6` for every configure; do not touch
`C:\Tools\FarManager`; do not run Far. Zero warnings under `/W4 /WX` on both architectures.

## Verify and report
`ctest --preset release-x86` (6 tests), `ctest --preset debug-x86`, `scripts/coverage.ps1 -Preset
coverage-x86` totals, `scripts/check-imports.ps1` on the x86 DLL, `llvm-readobj --file-header`
(Machine) and `--coff-exports` on it, its size; then the unchanged x64 presets: `ctest --preset
release`, `scripts/coverage.ps1`, check-imports on the x64 DLL. Quote everything. List every file
changed and anything not done with the reason.

## Addendum A — dav1d on x86 (observed failure, must be solved, not worked around)

A warm-up `vcpkg install libavif[dav1d]:x86-windows-static-clang` with the prepared triplet failed
in dav1d's meson configure: the compiler sanity check
(`clang-cl.exe --target=i686-pc-windows-msvc ... sanitycheckc.exe`) fails to link with
`LNK2001: unresolved external symbol _mainCRTStartup` — the compile step targets i686 but the link
step resolves against x64 CRT libraries (meson drives the linker itself; no x86 `LIB` paths are in
the environment and no `/machine:x86` reaches lld-link). Plain `clang-cl -m32 m.c` from a shell
links fine (it picks `Hostx64\x86\link.exe` and the x86 libpaths itself), so the toolchain is
capable; the fault is in how vcpkg's meson wrapper passes the target to the linker for a
chainloaded toolchain. Fix options, in order of preference: (a) make the triplet/toolchain provide
what vcpkg's meson support needs (`VCPKG_TARGET_ARCHITECTURE x86` already set; check
`vcpkg_configure_meson.cmake` for how it derives `c_link_args`/`cpu_family`/linker and what the
chainload must export — possibly `CMAKE_C_FLAGS`/`CMAKE_EXE_LINKER_FLAGS` with `-m32` /
`/machine:x86`, or leaving the linker to clang-cl instead of forcing lld-link); (b) an overlay
port for dav1d adding the needed meson cross-file settings; (c) as a last resort only, build dav1d
for x86 with MSVC `cl` (stock `x86-windows-static` triplet for that one port) — but then say so
loudly, because it breaks the "one toolchain" rule and the SIMD situation must be re-checked.
Logs of the failed attempt: `C:\Users\Roma\scoop\apps\vcpkg\current\buildtrees\dav1d\config-x86-windows-static-clang-dbg-*.log`.

## Addendum B — VERSIONINFO resource and packaging (both architectures)

7. Embed a Windows VERSIONINFO resource in `AVIF.pvd` (`src/pvd/AVIF.rc`, compiled by llvm-rc,
   added to the shared target). Fields: FILEVERSION/PRODUCTVERSION `1,0,0,0`; `FileVersion` and
   `ProductVersion` "1.0.0"; `FileDescription` "AVIF decoder plugin for PictureView (Far Manager)";
   `ProductName` "AVIF.pvd"; `InternalName` "AVIF.pvd"; `OriginalFilename` "AVIF.pvd";
   `CompanyName` and `LegalCopyright` from two CMake cache variables `AVIFPVD_AUTHOR` and
   `AVIFPVD_COPYRIGHT` (defaults: "Roman Kharitonov" and "Copyright (C) 2026 Roman Kharitonov" —
   the orchestrator may override them at configure time); `Comments` = the same string that
   `pvdPluginInfo` returns as comments (library versions). The version number must have ONE source
   of truth: `src/pvd/PluginConstants.hpp` already holds "1.0.0" — generate the .rc (or an included
   header) from the CMake `project(VERSION)` and static_assert/compare that PluginConstants agrees,
   so a bump cannot land in only one place. Test: e2e (or a small test in tests/e2e) reads the
   version block back from the built DLL via `GetFileVersionInfoW`/`VerQueryValueW` (link
   `version.lib` in the TEST only — the plugin itself must keep importing KERNEL32.dll only; verify
   with check-imports that the .rc did not add imports) and asserts FileVersion == "1.0.0" and
   CompanyName == the configured author.
8. `scripts/package.ps1`: builds `release` and `release-x86` (fresh suffix `-pkg`), runs
   check-imports on both DLLs, and produces `dist/AVIF-<version>-x64.zip` and
   `dist/AVIF-<version>-x86.zip`, each containing `AVIF.pvd`, `README.txt` (install: copy next to
   `0PictureView.dll` in Far's PictureView plugin folder; what is supported; limitations; author;
   version), and `LICENSES.txt` with the licence texts of libavif (BSD-2), dav1d (BSD-2), libyuv
   (BSD-3) copied from the vcpkg buildtrees/installed share directories at build time (not
   hand-typed). Version taken from the CMake project version. The script prints the two zip
   paths and their SHA-256.
