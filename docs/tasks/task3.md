# Task 3 — adapters (`src/adapters`, `tests/adapters`)

Repository: current directory. Read `AGENTS.md` and `docs/ARCHITECTURE.md` completely first; §3.5,
§3.6, §3.8 and §5 (`tests/adapters`) are your part. Infrastructure, canonical headers
(`src/core/IFileSource.hpp`, `src/core/IDecoder.hpp`, `src/core/Error.hpp`, `src/pvd/Types.hpp`) and
fixtures (`tests/fixtures/`, read `SOURCES.md`) already exist — do not restructure them. Other agents
are working **at the same time** in `src/pvd`, `src/core`, `tests/pvd`, `tests/core`. You own only
`src/adapters/**` and `tests/adapters/**` (the existing link smoke test there stays). Do not edit
anything else; if a shared file must change, describe it in your final report.

Build isolation: set `AVIFPVD_BUILD_SUFFIX=-t3` before any cmake/ctest call.

Read the installed `avif/avif.h` (under the build's `vcpkg_installed/x64-windows-static-clang/include`)
before writing a line: field names, `avifCropRectFromCleanApertureBox` signature, the `imir.axis`
comment, `avifDecoderNthImageTiming`, `avifPeekCompatibleFileType`, `avifRGBImage` user-buffer rules.

## Deliverables (TDD: failing test first, every time)

1. `src/adapters/win/Utf8.hpp/.cpp` — `Result<std::wstring> utf8ToWide(std::string_view)` using
   `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...)`; invalid UTF-8 → `FileOpenFailed` with
   detail. `std::wstring toExtendedPath(std::wstring_view)`: adds `\\?\` only for absolute drive paths
   (`X:\...`) not already prefixed; UNC `\\server\share` → `\\?\UNC\server\share`; relative paths
   untouched. Tests for every branch including empty string.
2. `src/adapters/win/FileMapping.hpp/.cpp` — RAII per §3.8, implements `core::IFileData`. Handles in
   `std::unique_ptr` with stateless deleters (`CloseHandle`, `UnmapViewOfFile`). Static factory
   `Result<std::unique_ptr<FileMapping>> open(std::wstring_view)`. Errors carry `GetLastError()` in
   `detail`. Zero-length file → `FileOpenFailed`. `src/adapters/win/FileSource.hpp/.cpp` implements
   `core::IFileSource` (UTF-8 → wide → extended path → `FileMapping::open`).
   Tests on temp files created in `%TEMP%`: normal file (bytes match), empty file, missing file,
   non-ASCII name (Cyrillic + emoji), a path longer than 260 chars (nested dirs), directory instead of
   file, invalid UTF-8 name.
3. `src/adapters/avif/Decoder.hpp/.cpp` — `avif::Decoder : core::IDecoder` and
   `avif::DecoderFactory : core::IDecoderFactory` per §3.8. `std::unique_ptr<avifDecoder, Destroy>`.
   `create()` parses and fills `ImageMeta` completely (every field). Map `avifResult` → `ErrorCode`:
   `AVIF_RESULT_BMFF_PARSE_FAILED`/`INVALID_FTYP`/`NO_CONTENT`/`NO_YUV_FORMAT_SELECTED`/`TRUNCATED_DATA`/
   `MISSING_IMAGE_ITEM` etc. → `ParseFailed`; `AVIF_RESULT_NO_CODEC_AVAILABLE`/`DECODE_*` → `DecodeFailed`;
   `AVIF_RESULT_INVALID_IMAGE_GRID`/`INVALID_EXIF_PAYLOAD`… → `ParseFailed`; size limit results →
   `TooLarge`; anything else → `Internal`; always `avifResultToString` in `detail`. Keep the mapping in
   one small table-driven function so that every branch is testable with the enum values directly.
   `imir.axis` → `MirrorAxis` per the `avif.h` comment (cite it). `clap` → `CropRect` via
   `avifCropRectFromCleanApertureBox`; failure → `InvalidTransform`.
   `frameTiming`: `avifDecoderNthImageTiming`, `lround(duration * 1000)` clamped to `uint32`.
   `decodeFrame`: buffer-size check (`Internal` if too small), `avifDecoderNthImage`, then
   `avifRGBImageSetDefaults` + depth 8 + `BGRA`/`BGR` + `alphaPremultiplied = AVIF_FALSE` +
   user buffer + `avifImageYUVToRGB`; map failures to `DecodeFailed`/`ConversionFailed`.
   `looksLikeAvif`: `avifPeekCompatibleFileType`; spans shorter than 12 bytes → false without calling.
   `std::string avif::libraryVersions()` → `"libavif X.Y.Z, dav1d A.B.C, libyuv N"` using
   `avifVersion()`, `avifCodecVersions()` (or `dav1d_version()`), `avifLibYUVVersion()`.
   Tests on fixtures: meta for each libavif and synthetic fixture (dimensions, depth, chroma, alpha,
   frame count, animated, transforms present, ICC/EXIF/XMP flags — use the values from `SOURCES.md`);
   `frameTiming` on the animations; `decodeFrame` into a caller buffer for `quad_rgb_lossless.avif`
   with exact BGR values per quadrant and for `alpha_steps.avif` with exact alpha; too-small buffer;
   `truncated.avif` → `ParseFailed` or `DecodeFailed` (assert one of them, no crash); `garbage.bin`,
   `not_avif.png` → `looksLikeAvif == false` and `create` → `ParseFailed`; `strict = true` vs `false`
   on a fixture that differs (find one among the libavif files; if none differs, test that both parse);
   `maxDimension`/`maxPixels` set below a fixture's size → `TooLarge`; `maxThreads` 1 vs 8 give
   identical pixels. Cover every branch of the result mapping by calling it directly with each enum.
4. `src/adapters/CMakeLists.txt`: `avifpvd_adapters` STATIC linking `avif`, `dav1d`, `yuv` (from
   `find_package(libavif CONFIG)` / `find_package(libyuv CONFIG)` — check the exact target names the
   vcpkg ports export) and `avifpvd_core` headers.

## Verify and report
`cmake --preset debug && cmake --build --preset debug --target adapter_tests guard_tests && ctest --preset debug -R "adapter|guard"`,
then `scripts/coverage.ps1` — your gate is `src/adapters/**` at 100 % lines and 100 % branches (quote
the per-file rows; other directories may be incomplete because other agents are still working).
Include exact commands and outputs. `git add` your files; do not commit.
