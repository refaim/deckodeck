# Task 8 implementation report: RPGMVP

Date: 2026-09-13  
Branch: `master`  
Build suffix: `-t8`

## What was built

Task 8 is complete. The repository now builds `RPGMVP.pvd` 1.0.0 for x64 and x86. It recognizes
RPG Maker MV/MZ encrypted PNG files by the eight-byte RPGMV signature and the reconstructed IHDR
CRC, exposes the shared PVD decoder interface, and decodes through statically linked libspng 0.7.4
and zlib 1.3.2. The Release DLLs import only `KERNEL32.dll` and export exactly the eight bare PVD
entry points.

The implementation consists of:

- format recognition and IHDR CRC validation in the RPGMVP core;
- a metadata-only describer producing the resolved comments contract;
- a libspng adapter whose stream callback supplies the reconstructed 16-byte PNG prefix and then
  the encrypted file from offset 32, without constructing a decrypted full-file copy;
- direct whole-image decode into the tightly packed shared `PixelBuffer`, followed by an in-place
  red/blue swap to BGR24 or BGRA32;
- strict libspng image, chunk, and CRC limits and exception-safe `unique_ptr` context ownership;
- composition, identity/version resources, package documentation, fixture provenance, a pure
  PowerShell decrypt/wrap utility, and a reproducible synthetic-fixture script;
- unit tests for detection/CRC and descriptions, adapter tests for the stream and decoder,
  composition tests, DLL-level e2e tests, shared leak/hostile-corpus tests, and release ABI-policy
  tests.

The resolved shared contract was implemented exactly in the authorized files: `ImageMeta` gained
defaulted `indexed` and `interlaced` fields, and `FileSession::pageInfo` now reports source bpp as
`indexed ? depth : depth * (hasAlpha ? 4 : 3)`. Both indexed polarities were added to the shared
tests, and the architecture document was updated. AVIF continues to use the defaults and all AVIF
tests remain green.

TDD was used for each requested unit: detection/CRC, stream callback, describer, composition, and
e2e behavior were first expressed as failing doctest cases, then the minimum implementation was
added and refactored. No production source was left without line and branch coverage.

All four carried-over Task 7 nits were completed:

1. `pvdkit_add_plugin` now rejects a plugin id that differs from its directory name.
2. Coverage profile parsing restricts plugin ids to `[A-Za-z0-9_]+`.
3. The guard rejects plugin headers shadowing a root-relative shared header and self-tests both
   positive and negative cases; `spng.h` was also added to the foreign-codec header gate.
4. AVIF's `PictureView (Far Manager)` description was not changed, and RPGMVP uses the same wording.

## Decisions and deviations

- Whole-image `spng_decode_image` was selected instead of progressive row decode because the shared
  `PixelBuffer` guarantees the exact tight pitch required here. It writes directly to the final
  allocation and is the simpler libspng path; only the in-place R/B swap follows.
- A local `ports/libspng` overlay was necessary. The stock libspng 0.7.4 port's old CMake policy
  level ignores `CMAKE_MSVC_RUNTIME_LIBRARY`, produces `/MD`, and then fails the required static-CRT
  link. The overlay preserves the upstream source and official config-package patch and adds only
  `-DCMAKE_POLICY_DEFAULT_CMP0091=NEW`; both triplets then build `/MT`/`/MTd`. No dependency was
  downloaded and no third-party codec source was modified.
- `truncated_idat.rpgmvp` lives in `fixtures/decode-failures/`. It must pass open and fail decode,
  while the shared leak fixture discovery treats every openable file in the fixture root as a
  successful-decode candidate and is non-recursive. The plugin's adapter and e2e tests explicitly
  exercise this nested fixture. The added corrupt-sRGB-CRC decode failure is colocated there.
- A reliable 2-bit indexed PNG could not be generated with the available ffmpeg encoder: `pal8`
  remained 8-bit without pngquant. No tool was installed or downloaded. Palette sub-byte behavior
  is exercised by the real 4-bit indexed fixture, and 1-bit source-depth behavior by the greyscale
  fixture.
- Exact RGB16 expectations use libspng 0.7.4's high-byte 16-to-8 reduction (`BGR (59,96,27)`), not
  ffmpeg's independently rounded conversion.
- A synthetic bad-sRGB-CRC input was added beyond the minimum negative list so the adapter's sRGB
  query-error path is tested rather than hidden from branch coverage.

## Fixtures

Paths below are relative to `plugins/rpgmvp/fixtures`. “Accept” means the plugin opens and decodes
the default image; “reject” means open fails unless the expectation explicitly says decode failure.

| Name | Source | Kind | Bytes | Expected values |
|---|---|---|---:|---|
| `rgba8_36x16_shadow2.rpgmvp` | `Awakening to Lust - RA26073439/www/img/system/Shadow2.rpgmvp` | accept, RGBA8 | 400 | 36x16, depth 8, alpha, non-Adam7, BGRA32 |
| `indexed4_trns_144x192_cursor.rpgmvp` | `Awakening to Lust - RA26073439/www/img/characters/!$cursor_small.rpgmvp` | accept, indexed4+tRNS | 529 | 144x192, indexed bpp 4, alpha; BGRA pixels (0,0)=`(0,0,0,0)`, (17,0)=`(78,224,255,222)` |
| `indexed8_trns_288x384_weapons3.rpgmvp` | `Awakening to Lust - RA26073439/www/img/system/Weapons3.rpgmvp` | accept, indexed8+tRNS | 2,263 | 288x384, indexed bpp 8, alpha, non-Adam7 |
| `rgb8_816x624_gameover.rpgmvp` | `Awakening to Lust - RA26073439/www/img/system/GameOver.rpgmvp` | accept, RGB8 | 6,989 | 816x624, source bpp 24, no alpha, BGR24 |
| `rgba16_60x20_par.rpgmvp` | `Awakening to Lust - RA26073439/www/img/menus/equip/Par.rpgmvp` | accept, RGBA16 | 21,981 | 60x20, source bpp 64, alpha, BGRA32 |
| `indexed8_trns_82x38_shadow2.png_` | `SpyBreak_Win_1.5/img/system/Shadow2.png_` | accept, MZ indexed8+tRNS | 463 | 82x38, indexed bpp 8, alpha; alternate extension/key variant |
| `rgba8_576x384_lolded.png_` | `SpyBreak_Win_1.5/img/characters/lolded.png_` | accept, MZ RGBA8 | 3,727 | 576x384, source bpp 32, BGRA32 |
| `rgba8_adam7_700x700_kamen.png_` | `Story by Story - Demo (PC)/img/pictures/Charakter/Ters/Kamen.png_` | accept, Adam7 RGBA8 | 25,882 | 700x700, `interlaced=true`; pixels equal the synthetic non-Adam7 twin |
| `rgb16_88x4a.rpgmvp` | reference fixture | accept, RGB16 | 19,537 | 88x4, source bpp 48, BGR24; first source samples R=`0x1B96`, G=`0x6060`, B=`0x3BF4`, output `(59,96,27)` |
| `rgb16_88x4a.png` | reference fixture twin | reject, plain PNG | 19,521 | decrypted encrypted twin is byte-identical to this PNG; plain PNG is not recognized |
| `rgba8_48x48.rpgmvp` | reference fixture | accept, RGBA8 | 744 | 48x48, source bpp 32; decrypted bytes equal PNG twin |
| `rgba8_48x48.png` | reference fixture twin | reject, plain PNG | 728 | plain PNG is not recognized |
| `gray8_16x8.rpgmvp` | synthetic script | accept, greyscale8 | 112 | Yuv400 metadata, source bpp 24 after RGB expansion; BGR x=0/1/15 is `(0,0,0)`/`(16,16,16)`/`(240,240,240)` |
| `graya8_16x8.rpgmvp` | synthetic script | accept, greyscale+alpha8 | 128 | Yuv400, source bpp 32; BGRA (2,1)=`(19,19,19,32)` |
| `gray1_16x8.rpgmvp` | synthetic script | accept, greyscale1 | 108 | depth 1, reported bpp 3 by the resolved non-indexed rule; x=0 black, x=8 white |
| `rgba8_noninterlaced_700x700_kamen.rpgmvp` | synthetic re-encoding | accept, RGBA8 | 17,092 | 700x700, non-Adam7; decoded pixels equal Adam7 Kamen |
| `rgba8_srgb_48x48.rpgmvp` | synthetic from reference | accept, RGBA8+sRGB | 757 | cICP `{1,13,0,true}`; canonical intent-0 sRGB chunk |
| `apng_4x4.rpgmvp` | synthetic script | accept, APNG | 245 | 4x4 RGB8; reports one non-animated page and decodes the default image |
| `stub_31.bin` | derived negative | reject, short | 31 | fails minimum-size detection |
| `stub_48.bin` | derived negative | reject, short | 48 | RPGMV prefix present but still below 49-byte minimum |
| `bad_ihdr_crc.rpgmvp` | derived negative | reject, CRC | 744 | reconstructed IHDR CRC mismatch |
| `too_large_100000x100000.rpgmvp` | derived negative | reject, limit | 744 | valid recomputed IHDR CRC; 100000x100000 exceeds dimension limit before allocation |
| `decode-failures/truncated_idat.rpgmvp` | derived negative | open then decode failure | 200 | valid IHDR and truncated IDAT; fails cleanly with libspng detail |
| `decode-failures/bad_srgb_crc.rpgmvp` | derived negative | open then metadata/decode failure | 757 | valid IHDR, corrupt ancillary sRGB CRC; strict CRC error |

Fixture expectations were checked on decrypted PNGs with ffprobe 9.0.1 and ffmpeg rawvideo output.
Both known-good encrypted/PNG twin pairs were compared byte-for-byte after decryption.

## Verification

Every invocation in this section ran with:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t8'
```

The mandatory repository `rtk` command prefix was used for executable invocations. Coverage runs
also set `CMAKE_BUILD_PARALLEL_LEVEL=6`, because the coverage script performs its own build. No x64
and x86 builds or lint runs overlapped.

### x64 Debug

```powershell
rtk cmake --preset debug
rtk cmake --build --preset debug --parallel 6
rtk ctest --preset debug --output-on-failure
```

Result: configure and build exited 0 with zero warnings. `ctest: 13/13 passed (47.76 sec)`;
`avif_leak_tests` passed in 22.71 s, `rpgmvp_leak_tests` in 18.45 s, and `guard_tests` in 2.22 s.

### x64 Release

```powershell
rtk cmake --preset release
rtk cmake --build --preset release --parallel 6
rtk ctest --preset release --output-on-failure
```

Result: configure and build exited 0 with zero warnings. `ctest: 17/17 passed (20.71 sec)`;
`avif_leak_tests` passed in 13.95 s and `rpgmvp_leak_tests` in 2.75 s. The 17 tests include both
plugins' Release-only import and export gates.

### x64 coverage

```powershell
$env:CMAKE_BUILD_PARALLEL_LEVEL = '6'
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
```

Result: `13/13` tests passed in 49.95 s. Separate AVIF and RPGMVP DLL profiles were found; each
reported `Exports.cpp: 18/18` regions. Totals were 725/725 regions, 190/190 functions, 1467/1467
lines, and 388/388 branches:

```text
TOTAL ... 1467 0 100.00% ... 388 0 100.00%
Coverage gate passed: lines 100%, branches 100%
```

### x86 Debug

```powershell
rtk cmake --preset debug-x86
rtk cmake --build --preset debug-x86 --parallel 6
rtk ctest --preset debug-x86 --output-on-failure
```

Result: configure and build exited 0 with zero warnings. `ctest: 13/13 passed (52.67 sec)`;
`avif_leak_tests` passed in 25.88 s, `rpgmvp_leak_tests` in 17.56 s, and `guard_tests` in 3.71 s.

### x86 Release

```powershell
rtk cmake --preset release-x86
rtk cmake --build --preset release-x86 --parallel 6
rtk ctest --preset release-x86 --output-on-failure
```

Result: configure and build exited 0 with zero warnings. `ctest: 17/17 passed (25.37 sec)`;
`avif_leak_tests` passed in 16.01 s and `rpgmvp_leak_tests` in 3.06 s. The Release-only import and
export gates passed for both plugins.

### x86 coverage

```powershell
$env:CMAKE_BUILD_PARALLEL_LEVEL = '6'
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86
```

Result: `13/13` tests passed in 62.94 s. Separate AVIF and RPGMVP DLL profiles were found; each
reported `Exports.cpp: 18/18` regions. Totals again were 725/725 regions, 190/190 functions,
1467/1467 lines, and 388/388 branches:

```text
TOTAL ... 1467 0 100.00% ... 388 0 100.00%
Coverage gate passed: lines 100%, branches 100%
```

### x64 AddressSanitizer

The ASan tree was configured and built at six-way parallelism before the requested test command:

```powershell
rtk cmake --preset asan
rtk cmake --build --preset asan --parallel 6
rtk ctest --preset asan --output-on-failure
```

Result: build exited 0 with zero warnings; `ctest: 13/13 passed (56.08 sec)`. The slowest tests were
`avif_leak_tests` 30.15 s, `rpgmvp_leak_tests` 10.42 s, and `guard_tests` 9.60 s. No sanitizer
failure was reported.

### Import and export tables

The Release preset already ran these checks. They were repeated verbosely to capture the evidence:

```powershell
rtk ctest --test-dir build\release-t8 -C Release -R 'rpgmvp_check_(imports|exports)' -V
rtk ctest --test-dir build\release-x86-t8 -C Release -R 'rpgmvp_check_(imports|exports)' -V
```

Both architectures printed:

```text
Import policy passed: KERNEL32.dll is the only imported module.
Export policy passed: the eight PVD entry points are exported under their bare names.
100% tests passed out of 2
```

x64 was identified as `COFF-x86-64`; x86 as `COFF-i386`. The exact export names on both were
`pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`, `pvdPageDecode`, `pvdPageFree`, `pvdPageInfo`,
and `pvdPluginInfo`.

### Lint

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -BuildDir build\debug-t8 -ReleaseDir build\release-t8 -Jobs 6
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -BuildDir build\debug-x86-t8 -ReleaseDir build\release-x86-t8 -Jobs 6
```

x64 result:

```text
clang-format: 0 finding(s) in 0.5 s
clang-tidy: 0 finding(s) in 246.6 s
cppcheck: 0 finding(s) in 1.1 s
PSScriptAnalyzer: 0 finding(s) in 3.6 s
BinSkim: 0 finding(s) in 1.0 s
lint: clean
```

x86 result:

```text
clang-format: 0 finding(s) in 0.5 s
clang-tidy: 0 finding(s) in 249.2 s
cppcheck: 0 finding(s) in 1.2 s
PSScriptAnalyzer: 0 finding(s) in 3.6 s
BinSkim: 0 finding(s) in 1.0 s
lint: clean
```

### Final repository state

```powershell
rtk git status --short
rtk git diff --stat
```

Final status:

```text
 M cmake/pvdkit-plugin.cmake
 M docs/ARCHITECTURE.md
 M scripts/coverage.ps1
 M src/core/FileSession.cpp
 M src/core/IDecoder.hpp
 M tests/core/FileSessionTests.cpp
 M tests/guard/GuardTests.cpp
 M vcpkg.json
?? docs/tasks/report-task8.md
?? plugins/rpgmvp/
?? ports/libspng/
```

Tracked-file diff stat (`git diff --stat` does not include the untracked plugin, port, or report):

```text
 cmake/pvdkit-plugin.cmake       |  6 ++++++
 docs/ARCHITECTURE.md            |  7 +++++--
 scripts/coverage.ps1            |  6 +++---
 src/core/FileSession.cpp        |  4 +++-
 src/core/IDecoder.hpp           |  2 ++
 tests/core/FileSessionTests.cpp | 15 +++++++++++++++
 tests/guard/GuardTests.cpp      | 26 +++++++++++++++++++++++---
 vcpkg.json                      |  9 ++++++++-
 8 files changed, 65 insertions(+), 10 deletions(-)
```

## Known limitations and open questions

- APNG animation is intentionally unsupported: the default image is decoded and reported as one
  non-animated page.
- PNG gamma correction and color-profile conversion are intentionally not applied. An sRGB chunk
  affects the reported cICP metadata only.
- The adapter cannot observe the original filename extension, so the format name is always RPGMVP.
- There is no dedicated 2-bit indexed fixture for the tooling reason recorded above.
- Packaging zips were intentionally not produced; the orchestrator owns packaging after review.

Open questions: none. Nothing was installed, Far Manager was not run or touched, and no commit,
branch, worktree, staging operation, or network download was performed.

## Fix round 1 (review-task8.md)

### What changed

- `plugins/rpgmvp/src/adapters/spng/Decoder.cpp` now installs a 16 MiB maximum individual chunk
  and a 64 MiB aggregate chunk-cache limit. A 16 MiB chunk ceiling is generous for RPG Maker
  sprites and tilesets while rejecting pathological single ancillary chunks; the 64 MiB cache
  leaves room for several retained PNG metadata chunks without allowing unbounded aggregate cache
  growth. Compile-time assertions prove `chunkBytes <= cachedChunkBytes` and that the chunk ceiling
  fits libspng's signed-32-bit ceiling.
- Context configuration now returns a checked `Result`. The return values of
  `spng_set_image_limits`, `spng_set_chunk_limits`, `spng_set_crc_action`, and
  `spng_set_png_stream` all pass through the same check; a rejection becomes an `Internal` error,
  because it indicates invalid adapter configuration. This gives the option-dependent image limit
  a real error path while the constant chunk relationship is impossible to violate at compile
  time.
- `readCallback` is explicitly `noexcept`. Its only called adapter operation, `Stream::read`, was
  already `noexcept`; a `static_assert(noexcept(stream.read(bytes)))` now pins that property at the
  C callback boundary.
- `Decoder.hpp` exposes only a `detail::configuredLimits()` adapter test seam. It creates the
  context through the production path and observes the installed values with
  `spng_get_image_limits` and `spng_get_chunk_limits`; no core or public decoder contract was
  widened.

### Verified libspng 0.7.4 rule and defaults

The installed source was inspected at
`C:\Users\Roma\scoop\apps\vcpkg\current\buildtrees\libspng\src\v0.7.4-e8b3878a48.clean\spng\spng.c`.
It defines `spng_u32max` as `INT32_MAX`. A new context initializes `max_width`, `max_height`, and
`max_chunk_size` to `spng_u32max`, and `chunk_cache_limit` to `SIZE_MAX`. The exact rejection in
`spng_set_chunk_limits()` is:

```c
if(ctx == NULL || chunk_size > spng_u32max || chunk_size > cache_limit) return 1;
```

The assignments occur only after that condition. The previous 64 MiB chunk / 8 MiB cache call
therefore returned 1 because `chunk_size > cache_limit`, leaving the effective defaults at
`INT32_MAX` per chunk and `SIZE_MAX` for the cache. `spng_set_image_limits()` likewise returns 1
for a null context or a width/height above `spng_u32max`, which is why its option-dependent result
is now propagated rather than discarded.

### TDD regression tests

The new test, `adapter contexts install bounded image and chunk limits`, first landed with the
production setters unchanged. After rebuilding only `rpgmvp_adapter_tests`, it failed as intended:
`0/1` CTest tests passed; doctest reported `13` cases, `550` assertions, and two failed assertions.
The observed cache value was `18446744073709551615` (`SIZE_MAX` on x64), rather than 64 MiB; the
individual chunk assertion also failed because the old invalid call had left the default in place.

After the fix, the test observes image limits equal to `DecoderOptions::maxDimension`, a 16 MiB
chunk ceiling, and a 64 MiB cache ceiling on the same production-created context. It also passes
`UINT32_MAX` as `maxDimension` and verifies a checked `Internal` error naming
`spng_set_image_limits`. The focused adapter CTest then passed `1/1`; the full adapter executable
has `13` cases and `554` assertions, all green.

### Gate results

Every command below used `$env:PVDKIT_BUILD_SUFFIX='-t8'`; builds used `--parallel 6`, coverage
also used `$env:CMAKE_BUILD_PARALLEL_LEVEL='6'`, and no builds or lint runs overlapped.

1. `rtk cmake --build --preset debug --parallel 6` completed with zero warnings, then
   `rtk ctest --preset debug --output-on-failure` passed `13/13` in `44.55 sec`. This includes the
   guard and RPGMVP leak/hostile-corpus tests.
2. `rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage`
   passed `13/13` tests in `48.59 sec`; plugin profile checks passed at `18/18 Exports.cpp`
   functions for both plugins. Coverage was `1555/1555` lines and `390/390` branches
   (`100.00%`/`100.00%`); RPGMVP `Decoder.cpp` was `305/305` lines and `72/72` branches.
3. `rtk cmake --build --preset release --parallel 6` completed with zero warnings, then
   `rtk ctest --preset release --output-on-failure` passed `17/17` in `20.99 sec`. The verbose
   `rtk ctest --test-dir build\release-t8 -C Release -R 'rpgmvp_check_(imports|exports)' -V`
   rerun passed `2/2`: the output is `COFF-x86-64`, imports only `KERNEL32.dll`, and exports exactly
   `pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`, `pvdPageDecode`, `pvdPageFree`,
   `pvdPageInfo`, and `pvdPluginInfo` under their bare names.
4. `rtk cmake --build --preset asan --parallel 6` completed with zero warnings. The first
   `rtk ctest --preset asan --output-on-failure` run passed every RPGMVP test, including the
   206-file hostile corpus, but finished `12/13` because the unrelated AVIF load/unload leak
   scenario transiently measured two mapped views on its first pass and zero on its own second
   pass; there was no AddressSanitizer report. An unchanged rerun of the same CTest command passed
   `13/13` in `109.55 sec`; `rpgmvp_leak_tests` passed in `10.22 sec`.
5. `rtk cmake --build --preset release-x86 --parallel 6` completed with zero warnings, then
   `rtk ctest --preset release-x86 --output-on-failure` passed `17/17` in `23.70 sec`. The verbose
   x86 import/export rerun passed `2/2`: the output is `COFF-i386`, imports only `KERNEL32.dll`,
   and exports the same exact eight bare names.
6. `rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86`
   passed `13/13` in `61.25 sec`, with both plugin profile checks at `18/18`; coverage again was
   `1555/1555` lines and `390/390` branches (`100.00%`/`100.00%`), including `305/305` lines and
   `72/72` branches in RPGMVP `Decoder.cpp`.
7. `rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6 -BuildDir
   build\debug-t8 -ReleaseDir build\release-t8` was clean: clang-format `0` findings (`0.5 s`),
   clang-tidy `0` (`245.7 s`), cppcheck `0` (`1.2 s`), PSScriptAnalyzer `0` (`3.6 s`), and BinSkim
   `0` (`1.2 s`).

This fix round changed only `plugins/rpgmvp/src/adapters/spng/Decoder.cpp`,
`plugins/rpgmvp/src/adapters/spng/Decoder.hpp`, `plugins/rpgmvp/tests/adapters/DecoderTests.cpp`,
and this report. The changed code is architecture-neutral, so the review instruction did not call
for a second x86 lint run. Nothing was installed; no packaging was performed; Far Manager was not
run or touched; and no commit, branch, worktree, staging, or network operation was performed.
