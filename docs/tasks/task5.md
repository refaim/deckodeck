# Task 5 — composition, AVIF.pvd, end-to-end tests, final gates

Repository: current directory. Read `AGENTS.md` and `docs/ARCHITECTURE.md` completely first. The pvd
layer (`src/pvd`), the core (`src/core`) and the adapters (`src/adapters`) are implemented, reviewed
and individually at 100 % coverage. You now wire them together and prove the whole plugin works as the
host will use it. You may touch any file, but keep changes outside `src/adapters/DefaultPlugin.cpp`,
`src/pvd/CMakeLists.txt`, `tests/e2e/**`, `scripts/**`, `README.md` and the root CMake files to what
is strictly needed, and list every such change in your report.

Build isolation: set `AVIFPVD_BUILD_SUFFIX=-t5` before any cmake/ctest call.

## Deliverables (TDD where there is logic)

1. `src/adapters/DefaultPlugin.cpp` — defines `pvd::makePlugin()` per ARCHITECTURE §3.7: an object
   owning `win::FileSource`, `avif::DecoderFactory`, `core::AvifPlugin` (declared in that order),
   forwarding `IPlugin`; `DecoderOptions{max(1, hardware_concurrency()), false, 16384*16384, 32768}`;
   `PluginInfo{10, "AVIF", "1.0.0", "AVIF decoder: " + avif::libraryVersions() + "; static build"}`.
   Unit-test it in `tests/adapters` (info values, `open` on a fixture succeeds, on garbage → NotAvif).
2. Shared library target `avifpvd` in `src/pvd/CMakeLists.txt`: `Exports.cpp` + `AVIF.def`,
   links `avifpvd_pvd`, `avifpvd_core`, `avifpvd_adapters`; `OUTPUT_NAME AVIF`, `SUFFIX .pvd`,
   `PREFIX ""`; static CRT; release has no PDB dependency; the DLL must not export anything but the
   8 functions (check with `llvm-readobj --coff-exports`).
3. `tests/e2e` — `e2e_tests` executable per ARCHITECTURE §5: `LoadLibraryW` of the built `AVIF.pvd`
   (path passed via a compile definition or a CMake-generated header), `GetProcAddress` of all 8
   exports, then drive them exactly like the host (see §1 and `../examples/pvdBMP.cpp`):
   - `pvdInit` == 1; `pvdPluginInfo` priority 10 / "AVIF" / "1.0.0" / comments mention libavif.
   - For every fixture in `tests/fixtures/SOURCES.md`: open from disk (`lFileSize` = real size, head =
     first 16 KiB) and from memory (`lFileSize = 0`, whole file); compare `pvdInfoImage`,
     `pvdInfoPage` and decoded pixels between the two modes (must be identical).
   - Synthetic fixtures: exact pixel checks (BGR order!) per quadrant/band; alpha bands; animation
     page count 3 and `lFrameTime` 100/200/300 (or whatever SOURCES.md documents); monochrome; 10-bit.
   - `abc_color_irot_alpha_irot.avif`: dimensions swapped relative to the coded size when the angle is
     odd; pixels differ from the `NOirot` twin in the expected way (compare rotated buffer of one to
     the other where SOURCES.md allows; at minimum, both decode and the sizes are consistent).
   - Callback: counts calls; a callback returning `FALSE` makes `pvdPageDecode` return `FALSE` and
     leaves nothing to free.
   - `pvdPageFree` then `pvdFileClose`; also `pvdFileClose` with an un-freed page; `pvdPageInfo` /
     `pvdPageDecode` with an out-of-range page → `FALSE`.
   - Rejections: `not_avif.png`, `not_avif.bmp`, `garbage.bin`, `truncated.avif` (open may succeed
     and decode fail, or open fails — assert no crash and `FALSE` somewhere), empty head, head shorter
     than 12 bytes.
   - `pvdExit`; calling exports after `pvdExit` returns `FALSE`; `FreeLibrary`.
   - Concurrency: open the same fixture in 4 threads simultaneously, decode, compare pixels, close.
4. `scripts/check-imports.ps1` registered as a ctest test in the `release` preset against the built
   `AVIF.pvd`; must pass (imports exactly `KERNEL32.dll`). If any other system DLL shows up, do not
   suppress it: report which symbol pulls it in (`llvm-readobj --coff-imports`) and stop.
5. `scripts/coverage.ps1` now runs all test executables including `e2e_tests` (the DLL is built
   instrumented in the coverage preset; merge its `.profraw` too) and the gate applies to all of
   `src/**`. Fix any gap by adding tests, never by excluding code.
6. `README.md`: final build/test/install instructions, fixture credits, known limitations (ARCH §8).
7. Guard test follow-ups from the Task 1 reviewer: (a) the core-layering include pattern only matches
   `pvd/<name>.hpp` — widen it to nested paths and `.h`/`.hpp` (`pvd/([a-z0-9_/]+)[.]h(pp)?`) and
   compare the whole captured name against the allowlist; (b) add the ARCHITECTURE §2 rule that
   `src/pvd/**` (shim side) and `src/adapters/**` never include each other, with the two explicit
   exceptions `src/pvd/Exports.cpp` (may include `pvd/PluginFactory.hpp` only — it must not include
   adapters headers either) and `src/adapters/DefaultPlugin.cpp` (may include `pvd/PluginFactory.hpp`,
   `pvd/Plugin.hpp`, `pvd/Types.hpp`). Self-tests in both polarities.

8. Cleanup items from the Task 2 (pvd) review — all small, do them:
   (a) add a `tests/pvd` ShimTest with two sessions alive at once: open two contexts, decode/free
   on each independently, close one, assert the other still works and `liveSessions` goes 2 → 1 → 0;
   (b) `Shim::pluginInfo`: fill the struct with the default constants (priority 10, "AVIF", "1.0.0",
   empty comments) first, then let the live plugin's `info()` overwrite, so the host never keeps
   garbage if `info()` throws — adjust the test that currently pins the leave-untouched behaviour;
   (c) introduce `src/pvd/PvdApi.hpp` that does the `<Windows.h>` (NOMINMAX, WIN32_LEAN_AND_MEAN) +
   `extern "C" { #include "third_party/pvd/PictureViewPlugin.h" }` dance once, and use it from
   Shim.hpp/Shim.cpp/Exports.cpp/tests instead of re-typedefs and three relative includes;
   (d) `src/pvd/AVIF.def`: bare `LIBRARY` like the SDK examples, not `LIBRARY AVIF`;
   (e) `src/pvd/CMakeLists.txt`: `avifpvd_pvd` must not link `avifpvd_core` PUBLIC (no pvd TU needs a
   core symbol); give `pvd_tests` its own `avifpvd_core` link line instead;
   (f) one shared header of plugin constants (`kPluginPriority = 10`, `kPluginName = "AVIF"`,
   `kPluginVersion = "1.0.0"`) used by both `Exports.cpp` defaults and `DefaultPlugin.cpp`;
   (g) `tests/pvd/ExportsTests.cpp`: an RAII guard that calls `pvdExit()`/reset in its destructor so a
   failed `REQUIRE` cannot leave the process state pointing at a dead stack `FakeState`;
   (h) `Shim.cpp` pitch cast to `INT32`: checked cast (pitch > INT32_MAX → return FALSE) with a test,
   rather than a silent wrap to a negative bottom-up pitch.

9. Cleanup items from the Task 4 (core) second review — small, do them:
   (a) `src/core/Transform.hpp/.cpp`: `crop`, `rotate`, `mirror` have no production callers and end in
   `.value()` (an unreachable throw). Remove them and their declarations; keep `validatedCrop`,
   `displaySize`, `hasTransforms`, `apply`; port the per-operation unit tests to go through `apply`
   with a single-transform `Transforms` (same literal expectation matrices). Update ARCHITECTURE §3.7
   accordingly (one sentence).
   (b) `Transform.cpp` kernel: rename the parameter `mirror` (shadows the removed function anyway)
   to `axis`.
   (c) if anything still takes a raw `CropRect` without validation, document the precondition.
11. Cleanup from the Task 3 (adapters) second review — small, do them:
   (a) `src/adapters/win/Utf8.cpp` `extended()`: if the string returned by `GetFullPathNameW` already
   starts with `\\?\` or `\\.\`, return it unchanged (GetFullPathNameW canonicalises `//?/` and
   `//./` spellings into those prefixes); tests with `//?/C:/…` and `//./C:/…` inputs ≥ MAX_PATH.
   (b) `tests/adapters/WinAdapterTests.cpp` `TempTree`: include the process id in the leaf directory
   name so two adapter_tests processes cannot delete each other's trees.
   (c) one comment line on each `detail::` block in `Decoder.hpp`, `FileMapping.hpp`, `Utf8.hpp`:
   "internal helpers exposed for unit tests; not part of the adapter contract".
10. `tests/fixtures/SOURCES.md`, the `quad_yuv420.avif` entry: state that the listed ±2 values are
    ffmpeg-swscale conversions of the coded YUV, that libavif+libyuv yields values up to ~11 away on
    the saturated quadrants (dav1d output is bit-exact; only YUV→RGB rounding differs), and that
    tests therefore compare against the generator's ideal colours with tolerance 16. Use the same
    tolerance and the ideal colours in the e2e pixel checks for that fixture.

## Verify and report
Run `cmake --preset release && cmake --build --preset release && ctest --preset release`,
`cmake --preset debug && cmake --build --preset debug && ctest --preset debug`, `scripts/coverage.ps1`,
`scripts/check-imports.ps1 -Path build/release-t5/.../AVIF.pvd`, and
`llvm-readobj --coff-exports` on the DLL. Report the size of `AVIF.pvd`, the full ctest summaries, the
coverage totals and the import/export tables. `git add`; do not commit.
