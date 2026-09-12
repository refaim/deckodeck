# Task 3 (adapters) — fix round after review

Repository: current directory. Read `AGENTS.md` and `docs/ARCHITECTURE.md` completely first (§3.8 was
amended after the review: `\\?\` only for long paths with `GetFullPathNameW` normalisation, `\\.\`
untouched; `imageSizeLimit` clamped to `[1, AVIF_DEFAULT_IMAGE_SIZE_LIMIT]`; the `file` span must
outlive the decoder). You own only `src/adapters/**` and `tests/adapters/**` (plus the one-line
`SOURCES.md` correction in item 5 below). No commits; `git add` at the end.

Build isolation: `AVIFPVD_BUILD_SUFFIX=-fix3` before every cmake/ctest call.

The reviewer (Opus) rejected the adapters with four substantive findings and fourteen nits. Fix all
four findings and nits 1–12 (nit 13 is a note; nit 14 is a design note handled by the orchestrator).
TDD: for every finding write the failing test first and show the red run.

## Substantive findings (verbatim)

1. `src/adapters/avif/Decoder.cpp:229-230` — `imageSizeLimit` is clamped to `uint32::max`, but
   libavif 1.4.2 only accepts `1..AVIF_DEFAULT_IMAGE_SIZE_LIMIT` (`read.c:5292-5296`: returns
   `AVIF_RESULT_NOT_IMPLEMENTED` otherwise; `avif.h:1292-1294`). Any `DecoderOptions::maxPixels` above
   16384² (or 0) makes every `create()` fail with `Internal: Not implemented`. Fix:
   `imageSizeLimit = clamp<uint64>(maxPixels, 1, AVIF_DEFAULT_IMAGE_SIZE_LIMIT)` (core still
   enforces the real `maxPixels` in `PixelBuffer::create`), plus tests for `maxPixels = 16384²+1`
   (must succeed on a 64×64 fixture) and `maxPixels = 0`.

2. `src/adapters/win/Utf8.cpp:52-54` (applied unconditionally by `FileSource.cpp:15`) — every
   drive-absolute path gets `\\?\`, which turns off Win32 path normalisation (`/`, `.`/`..`, trailing
   dots). Probe: `C:\...\fixtures/white_1x1.avif` → no prefix OK, with prefix → Win32 error 123; same
   for `..\`. Also `\\.\X:\...` device paths are mangled into `\\?\UNC\.\X:\...`. Fix: prefix only when
   the path length is `>= MAX_PATH` (normalise with `GetFullPathNameW` first), treat `\\.\` like
   `\\?\` (never touch). Tests: short absolute path containing `/` and `..` must open; a `\\.\`
   path is left alone; a ≥ MAX_PATH path with `/` separators opens (normalised then prefixed); the
   existing long-path test still passes; relative paths untouched; UNC long path → `\\?\UNC\`.

3. `tests/adapters/DecoderTests.cpp:287-300` — the truncated-file test accepts both `create()`
   succeeding and failing and either of two codes, so it cannot fail on a regression. Behaviour is
   deterministic (cut inside the `meta` box → `ParseFailed: Truncated data`). Pin
   `REQUIRE_FALSE(truncated)` + `code == ParseFailed`; the `else` branch is dead.

4. `tests/adapters/DecoderTests.cpp` — `avifDecoderNthImage(frame > 0)` is never exercised on a
   valid sequence; the only non-zero frame call is on a still expecting failure. `SOURCES.md:54`
   gives exact colours for all three `anim_3frames.avif` frames (unused); `quad_yuv420.avif` centre
   values (`SOURCES.md:52`, ±2) unused; no > 8-bit source, grid, or 4:0:0 file is decoded through the
   adapter (meta only). Add: anim frames 0, 1, 2 exact BGR + a backward seek (decode 2 then 0);
   quad_yuv420 ±2 at the four quadrant centres; `tenbit_444.avif`, `sofa_grid1x5_420.avif`,
   `gray_400.avif` and `cosmos1650_yuv444_10bpc_p3pq.avif` decoded end-to-end with sanity
   assertions (dimensions, and for the synthetic ones the exact/±2 colours from SOURCES.md).

## Nits to address (1–12)

1. `Decoder.cpp:19-37,160-163` — map `AVIF_RESULT_NO_IMAGES_REMAINING` → `PageOutOfRange` (libavif
   returns it exactly for `frameIndex >= imageCount`); `DecoderTests.cpp:198-200` must assert the code.
2. `Decoder.cpp:53-55,240-242` — `TooLarge` derived by substring-matching libavif's diagnostic text:
   add a comment citing libavif 1.4.2 `read.c:2153, 4046, 5337` ("dimensions are too large") so an
   upgrade re-checks it.
3. `Decoder.hpp:57-62` — document that the `file` span passed to `create()` must outlive the
   returned decoder (`avifIOCreateMemoryReader` is persistent; libavif keeps pointers into it).
4. `Decoder.hpp:37` — the public ctor accepts a null handle; either document the precondition or
   restrict construction to the factory (friend/private + factory), keeping the pitch-overflow test
   possible.
5. `Decoder.cpp:200-208` — pass `options.maxThreads` into `rgb.maxThreads` (store it in the Decoder);
   test that 1 vs 8 still give identical pixels.
6. `Decoder.cpp:19-37` — map parse-time `AVIF_RESULT_NOT_IMPLEMENTED` and
   `AVIF_RESULT_UNSUPPORTED_DEPTH` to `UnsupportedFeature` instead of `Internal`; assert in the
   enum-driven mapping test.
7. `Utf8.cpp:29` — guard `utf8.size() > INT_MAX` → `FileOpenFailed` before the `static_cast<int>`.
8. `FileMapping.cpp:31` — zero-length-file detail must not claim a Win32 error code; say
   "file is empty".
9. `DecoderTests.cpp:62-64,130` — assert `irotAngle == 1` (the boxes carry angle=1), not `!= 0`.
10. `DecoderTests.cpp:138` — rename or extend so the "mirror semantics" test actually asserts a
    mirror value.
11. `DecoderTests.cpp:277` — use the committed `not_avif.bmp` negative fixture too.
12. `src/adapters/CMakeLists.txt:3,9` / `tests/adapters/CMakeLists.txt:4-8` — remove the
    `if(AVIFPVD_ADAPTER_SOURCES)` scaffolding and the `else()` fallback (sources exist now); drop
    the redundant `find_package(libyuv)` if `libavif-config.cmake` already pulls it in (verify).

Also (from the reviewer's verification): `tests/fixtures/SOURCES.md:30-31` lists wrong dimensions
for `sofa_grid1x5_420.avif` (actual 1024×770) and `color_grid_alpha_nogrid.avif` (actual 80×80);
correct those two lines — the tests already assert the right values.

## Verify and report
`cmake --preset debug && cmake --build --preset debug && ctest --preset debug`, then
`scripts/coverage.ps1` (repo-wide gate must pass; quote src/adapters rows and TOTAL), then
`cmake --preset release && cmake --build --preset release --target adapter_tests` and
`scripts/check-imports.ps1` on that binary. Include every red run, the final doctest counts, and a
list of anything not done with the reason.
