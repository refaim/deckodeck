# Task 1 — fix round after review

Repository: current directory. Read `AGENTS.md` and `docs/ARCHITECTURE.md` completely first (§2 and §4
were amended after the review — read them fresh). Then read the review verbatim:
`C:\Users\Roma\AppData\Local\Temp\claude\c--Users-Roma-Dev-PictureView3\630ccd33-5bbb-4673-9fea-509fa47e861e\scratchpad\review-task1-result.md`

Task 1 (infrastructure, canonical headers, guard test, fixtures) was implemented by a previous agent
and rejected with 6 substantive findings and 12 nits. Fix all 6 substantive findings and nits 1–11
(nit 12 is a process note). Rules of engagement: TDD where there is logic (the guard scanner and the
coverage gate have logic — write the failing cases first), no commits, `git add` at the end, report
honestly with command output.

Build isolation: set `AVIFPVD_BUILD_SUFFIX=-fix1` before any cmake/ctest call. Fresh build dirs.

## Required changes, in this order

1. **Overlay port instead of archiver override (findings 1 and 2).**
   - Create `ports/libavif/` as a copy of vcpkg's `ports/libavif` (from
     `C:\Users\Roma\scoop\apps\vcpkg\current\ports\libavif`, same version 1.4.2 port-version 1; keep
     `vcpkg.json` identical except bump `"port-version"` by one so the overlay is distinguishable).
   - Add a patch `merge-static-libs-clang-cl.patch` applied in `portfile.cmake` that changes
     `cmake/Modules/merge_static_libs.cmake` so the `MSVC` branch is tested **before** the Clang/GNU
     branch (or the Clang branch gets `AND NOT MSVC`). In that MSVC branch the bundling tool must be
     resolvable under clang-cl: honour `CMAKE_LIBTOOL` if set, else `find_program` for `llvm-lib`
     next to the compiler, else `lib`. Set `CMAKE_LIBTOOL` to llvm-lib in
     `cmake/clang-cl.toolchain.cmake` if needed.
   - Register the overlay in `CMakePresets.json` via `VCPKG_OVERLAY_PORTS` = `${sourceDir}/ports`.
   - Remove from `cmake/clang-cl.toolchain.cmake`: `CMAKE_AR`, `CMAKE_RANLIB`, the
     `CMAKE_STATIC_LINKER_FLAGS*` clearing, `CMAKE_USER_MAKE_RULES_OVERRIDE`. Delete
     `cmake/clang-cl-rules.cmake`. The toolchain sets only compilers, linker, rc, and
     `CMAKE_MSVC_RUNTIME_LIBRARY` (ARCHITECTURE §4).
   - Prove it: after a clean configure, show that `build/debug-fix1/CMakeCache.txt` has
     `CMAKE_AR` = llvm-lib, that libavif installed `avif.lib` under the clang triplet, and that
     dav1d/libyuv/doctest still build (or restore from cache — say which). Then prove the archive
     truncation is back: build `avifpvd_core`, force a relink twice, and show with
     `llvm-ar t` / `llvm-lib /list` that each object appears exactly once.
2. **Guard test (finding 3, nit 4).** Rewrite the scanner: token-boundary regexes, include checks
   case-insensitive and accepting both `<...>` and `"..."`, digit-separator-aware `'` handling,
   `catch\s*\(`. Encode the clarified layering rule: files under `src/core/**` may include
   `pvd/Types.hpp` and `pvd/Plugin.hpp` but must not include `pvd/Shim.hpp`, `pvd/ContextHandle.hpp`,
   `pvd/Firewall.hpp`, `pvd/PluginFactory.hpp`, `<windows.h>`, or `avif/avif.h`. Add every case from
   the reviewer's table as a self-test (all must be flagged), plus negatives (`deleted`, `newline`,
   `renew`, `free_list`, a `'\''` char literal, a string containing `delete`, a comment containing
   `malloc`, `268'435'456`) that must not be flagged.
3. **Animation fixture (finding 4).** Regenerate `anim_3frames.avif` with per-frame durations
   100 / 200 / 300 ms. Approach that works with ffmpeg's avif muxer: a concat demuxer list with
   three single-colour PNG/`color` inputs and `duration 0.1` / `0.2` / `0.3` lines (repeat the last
   file line as the concat docs advise), `-fps_mode passthrough` (or `-vsync passthrough`), lossless
   `libaom-av1` `gbrp`; then verify with `ffprobe -select_streams v:0 -show_frames` /
   `-show_packets` that the three sample durations are 0.1, 0.2, 0.3 (the avis track's per-sample
   `stts` durations). If ffmpeg genuinely cannot express that, stop and say so in the report with the
   exact evidence — do not substitute a constant. Update `SOURCES.md` with the verified values.
4. **Adapters target STATIC (finding 5)** with the empty-glob guard pattern already used in
   `tests/e2e/CMakeLists.txt`.
5. **Coverage gate (finding 6).** `scripts/coverage.ps1` must (a) discover test executables from the
   build (e.g. `ctest --show-only=json-v1` or a CMake-generated list) instead of a hard-coded list,
   (b) assert that every `src/**/*.cpp` and every `src/**/*.hpp` that contains executable code
   appears in the `llvm-cov export` file list — at minimum every `.cpp` — and fail with the missing
   names otherwise, (c) keep the 100/100 numeric gate. Show it failing on a deliberately untested
   temporary `src/core/Untested.cpp` (then delete that file) and passing afterwards.
6. **Nits 1, 2, 3, 5, 6, 7, 8, 9, 10, 11** as written in the review. For nit 8: discover Ninja via
   `PATH` first, then `C:\Users\Roma\scoop\apps\vcpkg\current\downloads\tools\ninja-*`, and keep the
   current path as the last fallback (CMake presets cannot glob, so do the discovery in a small
   CMake include or a `cmake -P` bootstrap; a documented one-line `scripts/bootstrap.ps1` is also
   acceptable if the presets then just work).

## Verify and report
Fresh `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`, same for
`release`, `scripts/coverage.ps1`, `scripts/check-imports.ps1` on the release adapter test binary,
the archive-truncation proof, the ffprobe proof for the animation durations, and the guard self-test
output. Quote everything. List any review item you did not address and why.
