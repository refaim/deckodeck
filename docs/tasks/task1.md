# Task 1 — infrastructure, canonical headers, guard test, fixtures

You are working in the repository at the current directory (`C:\Users\Roma\Dev\PictureView3\avif`).
Start by reading `AGENTS.md` and `docs/ARCHITECTURE.md` completely. They are binding. The parent folder
`..` contains the PictureView SDK (`../sdk/PictureViewPlugin.h`, CP1251-encoded), two example decoders
(`../examples/`) and the bundled plugin binaries (`../plugin/`). Read the SDK header and both examples.

Deliver exactly the following. No product logic beyond what is listed; later tasks implement the
core, the shim and the adapters in parallel on top of what you leave behind, so the headers, the build
and the scripts must be solid.

## 1. SDK header copy
`third_party/pvd/PictureViewPlugin.h` = `../sdk/PictureViewPlugin.h` re-encoded from CP1251 to UTF-8
(Git Bash `iconv -f CP1251 -t UTF-8`, or PowerShell `Get-Content -Encoding 1251`). Code must be
byte-identical apart from the comment encoding; add a one-line header comment saying where it came from.

## 2. Build system (ARCHITECTURE §4)
- Root `CMakeLists.txt` (project `avifpvd`, version 1.0.0, C++ only) plus one `CMakeLists.txt` per
  directory under `src/` and `tests/` using `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`. Create the
  library targets now even if they have only headers (`avifpvd_core`, `avifpvd_pvd`, `avifpvd_adapters`
  as INTERFACE or STATIC as appropriate — switch to STATIC as soon as a .cpp exists). Do NOT create the
  `avifpvd` shared library target yet (Task 2 adds `Exports.cpp`); leave a clearly marked TODO in
  `src/pvd/CMakeLists.txt` describing exactly how to add it (OUTPUT_NAME AVIF, SUFFIX .pvd, PREFIX "",
  `/DEF:${CMAKE_CURRENT_SOURCE_DIR}/AVIF.def`). Create `src/pvd/AVIF.def` with the 8 exports now.
- `CMakePresets.json`: configure presets `debug`, `release`, `coverage`; build and test presets of the
  same names. Generator Ninja. Compilers: clang-cl and lld-link from
  `C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin/`.
  `binaryDir` = `${sourceDir}/build/${presetName}$env{AVIFPVD_BUILD_SUFFIX}`.
  Toolchain: vcpkg's `scripts/buildsystems/vcpkg.cmake` under `C:/Users/Roma/scoop/apps/vcpkg/current`
  (prefer `$env{VCPKG_ROOT}` when set, otherwise that path — implement the fallback in a tiny
  `cmake/vcpkg-root.cmake` or via preset `cacheVariables`), `VCPKG_TARGET_TRIPLET` =
  `x64-windows-static-clang`, `VCPKG_OVERLAY_TRIPLETS` = `${sourceDir}/triplets`,
  `VCPKG_HOST_TRIPLET` = `x64-windows` (host tools stay MSVC-built, that is fine).
- `triplets/x64-windows-static-clang.cmake`: same as vcpkg's `x64-windows-static` plus
  `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` pointing at `cmake/clang-cl.toolchain.cmake`, which sets
  `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER` to that clang-cl, `CMAKE_LINKER` to lld-link, and
  `CMAKE_RC_COMPILER` to llvm-rc (same directory) or the Windows SDK rc.exe. Purpose: libyuv only
  compiles its SIMD row functions with clang (vcpkg issue #28446), and we want one toolchain.
  If dav1d's meson build refuses clang-cl, do NOT fall back silently: report the exact error in your
  final message and, only as a last resort so that the rest of the work can proceed, use the stock
  `x64-windows-static` triplet and say so loudly.
- `vcpkg.json`: `libavif` with feature `dav1d`, and `doctest`. Add `builtin-baseline` = the commit
  of the vcpkg checkout (`git -C <root> rev-parse HEAD`, or `vcpkg x-update-baseline --add-initial-baseline`).
- Flags for our targets (an INTERFACE target `avifpvd_options` that everything links):
  `/clang:-std=c++23 /W4 /WX /permissive- /utf-8 /EHsc`; `CMAKE_MSVC_RUNTIME_LIBRARY` =
  `MultiThreaded$<$<CONFIG:Debug>:Debug>`; coverage preset adds
  `-fprofile-instr-generate -fcoverage-mapping` and `/Od /Zi`; release: `/O2 /Zi` with
  `/DEBUG:NONE`-equivalent (no PDB shipped), `/INCREMENTAL:NO`. Verify `__cplusplus == 202302L`
  with a `static_assert` in `src/core/Error.hpp`.
- Ninja: vcpkg has `downloads/tools/ninja-1.13.2-windows/`; put that on PATH in the presets via
  `environment`, or `scoop install ninja`. Either is fine; document which.

## 3. Canonical headers (ARCHITECTURE §3.1, §3.2, §3.3, §3.5, §3.6) — exactly as written there
`src/core/Error.hpp` (+ `Error.cpp` for `name(ErrorCode)`), `src/pvd/Types.hpp` (+ `Progress.cpp`),
`src/pvd/Plugin.hpp`, `src/core/IFileSource.hpp`, `src/core/IDecoder.hpp`. Doxygen-style one-liners
on every type. The only implementations: `Progress` and `name(ErrorCode)`. Tests for both in
`tests/pvd` and `tests/core` respectively (doctest; write them first). Both must be at 100 % lines and
branches in the coverage report.

## 4. Guard test (ARCHITECTURE §5, `tests/guard`)
A doctest executable that walks `src/` and fails on forbidden tokens as specified. Strip `//` and
`/* */` comments and string literals before matching. Add self-tests of the scanner on inline sample
snippets (positive and negative for every rule). It must pass on the tree you leave behind.

## 5. Adapter link smoke test (`tests/adapters`)
One doctest case that includes `<avif/avif.h>`, asserts `avifVersion()` is non-empty and that
`avifCodecName(AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_DECODE)` returns `"dav1d"`. This proves the
static dependencies link with clang-cl. It links `avifpvd_adapters`.

## 6. Fixtures (ARCHITECTURE §6)
- `scripts/fetch-fixtures.ps1`: downloads the listed libavif files from
  `https://raw.githubusercontent.com/AOMediaCodec/libavif/<commit>/tests/data/...` with a pinned
  commit (use the current `main` HEAD you can resolve via the GitHub API and write the SHA into the
  script and into `SOURCES.md`). Also fetch `tests/data/README.md` for the notices.
- `scripts/make-synthetic-fixtures.ps1`: uses `ffmpeg` on PATH (has `libaom-av1`, `avif` muxer) to
  generate the synthetic files listed in §6. Solid colours must be exact primaries
  (255,0,0 / 0,255,0 / 0,0,255 / 255,255,255). Use `-pix_fmt gbrp -c:v libaom-av1 -aom-params lossless=1`
  for the lossless RGB ones (identity matrix, full range). For the animation use three 1-frame inputs or
  a `color` lavfi source per frame and set per-frame durations 100/200/300 ms (ffmpeg's avif muxer
  writes an `avis` sequence when there is more than one frame; if per-frame durations cannot be
  expressed, use a constant 100 ms and document it in SOURCES.md). Negatives as listed, including
  `truncated.avif` = the first 60 % of `quad_yuv420.avif`.
- Run both scripts, commit their outputs under `tests/fixtures/` (keep everything under ~10 MB; drop
  the largest libavif files if needed and say which), and write `tests/fixtures/SOURCES.md` with, per
  file: origin, licence, what it exercises, and expected values you actually verified with `ffprobe`
  (width, height, frame count, pixel format, alpha yes/no). For the synthetic RGB files also list the
  exact expected pixel values per quadrant/band.

## 7. Scripts
- `scripts/check-imports.ps1 -Path <dll>`: prints the import table (use `llvm-readobj --coff-imports`
  from the LLVM directory, or `dumpbin /dependents` from Build Tools) and exits 1 unless the only
  imported module is `KERNEL32.dll` (case-insensitive). Demonstrate it on `../plugin/WebP.pvd`
  (must fail: it also imports msvcrt.dll) and show the output in your report.
- `scripts/coverage.ps1`: as in ARCHITECTURE §4, gate = 100 % lines and 100 % branches on `src/**`
  (ignore `tests/`, `third_party/`, anything under vcpkg). Print the totals table. It must work now on
  the tiny code base you leave behind.

## 8. Housekeeping
`README.md` (what this is, how to build/test/coverage, where AVIF.pvd ends up, how to install: copy
next to `0PictureView.dll` — do not touch `C:\Tools\FarManager` yourself). Keep `.gitignore` sane.
`git add` your work; do NOT commit.

## 9. Verify and report
Run: `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`, same for
`release`, then `scripts/coverage.ps1`. Include in your final message: the exact commands, the ctest
summary lines, the coverage totals table, the check-imports demonstration, the fixture list with
sizes, and anything you could not do. No claim without the output that proves it.
