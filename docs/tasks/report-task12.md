# Task 12 implementation report: RPGMVP alpha/deep-output experiment

Date: 2026-09-13  
Branch: `master`  
Base commit: `89f606b`  
Normal build suffix: `-t12`  
Experiment build suffix: `-t12deep`

## Outcome

Task 12 is complete. The shared decoder contract now represents meaningful alpha separately from
pixel layout and can carry tightly packed BGRA64 pixels with little-endian 16-bit B, G, R and
straight-alpha samples. The PVD shim emits the host's undocumented alpha flag bit 2. RPGMVP can
optionally preserve source depths above eight bits per sample; its normal build remains at the
existing BGR24/BGRA32 output depths, while the experiment build enables BGRA64. AVIF's default
behavior is unchanged and its adapter rejects an unexpected BGRA64 request explicitly.

The experiment DLLs are built and ready for Roma's manual PictureView test. Automated tests prove
the bytes and boundary values supplied to the host; only the host's interpretation of flag bit 2
and `nBPP == 64` remains to be established in Far.

## Implementation

- `PixelFormat` gained `Bgra64`, documented as eight bytes per pixel with little-endian 16-bit
  samples and straight alpha. `DecodedPage` gained defaulted `hasAlpha`, which records whether
  alpha carries information independently of whether the buffer has four channels.
- `PvdApi.hpp` defines undocumented `PVD_IDF_ALPHA` as 2 and records its provenance: BMP.pvd in
  PictureView 2021.4.19 sets that bit for 32-bit BMPs with an alpha mask. `Shim::pageDecode()` now
  maps `DecodedPage::hasAlpha` to `Flags` 2 or 0 and continues to forward 24, 32 or 64 as `nBPP`.
- `DecoderOptions::deepOutput` is last and defaults to false. `FileSession::decodePage()` selects
  BGRA64 only when `deepOutput && meta.depth > 8`; otherwise it retains BGRA32/BGR24 selection.
  Pitch and allocation use 8/4/3 bytes per pixel, and `hasAlpha` always comes from source metadata.
- The byte-oriented transform kernel already handles eight-byte pixels generically. A regression
  test now exercises that path. The AVIF composition explicitly keeps `deepOutput=false`, and the
  AVIF adapter returns `UnsupportedFeature` if BGRA64 is nevertheless requested.
- RPGMVP uses `SPNG_FMT_RGBA16` for BGRA64. libspng expands RGB, greyscale and palette inputs to
  RGBA16; the adapter swaps the two-byte R and B units in place. A BGRA64 request for a source at
  depth 8 or lower returns `UnsupportedFeature` cleanly.
- The first BGRA64 pixel is asserted exactly for both requested fixtures:
  `rgba16_60x20_par.rpgmvp` produces `(FFFF, FFFF, FFFF, 0000)`, and
  `rgb16_88x4a.rpgmvp` produces `(3BF4, 6060, 1B96, FFFF)`.
- `PVDKIT_RPGMVP_DEEP_OUTPUT` is a CMake option defaulting OFF. When ON, the composition receives
  `PVDKIT_RPGMVP_DEEP_OUTPUT=1`, and both runtime and VERSIONINFO comments end with
  ` [experiment: deep output]`. Both experiment build trees contain that generated comment and
  compile definition.
- Default DLL e2e tests assert alpha/opaque `Flags == 2/0`. A separate composition e2e test drives
  `makePlugin(options)` with `deepOutput=true` through `pvd::Shim` and checks BGRA64, pitch
  `width * 8`, and flags 2/0 for the two 16-bit fixtures. The shared sequence validator and its
  tests now accept 24, 32 and 64 bpp with the corresponding minimum pitch.
- `docs/ARCHITECTURE.md`, the RPGMVP design, and fixture provenance were updated to describe the
  shared contract and exact samples.

The coverage build intentionally uses the default OFF polarity. The ON behavior is covered through
the same production `makePlugin(options)` composition with `deepOutput=true`; this keeps the normal
DLL e2e expectations stable while executing every runtime branch at 100/100 coverage. The separate
ON DLLs were built only for the requested host experiment and ABI checks.

## Verified libspng endianness

The installed, locally cached libspng 0.7.4 source was inspected at:

```text
C:\Users\Roma\scoop\apps\vcpkg\current\buildtrees\libspng\src\v0.7.4-e8b3878a48.clean\spng\spng.h
C:\Users\Roma\scoop\apps\vcpkg\current\buildtrees\libspng\src\v0.7.4-e8b3878a48.clean\docs\decode.md
```

`spng.h:167-181` declares channels in byte order, lists `SPNG_FMT_RGBA16`, and states that only
`SPNG_FMT_RAW` is big-endian while every other format is host-endian. `docs/decode.md:192-195`
independently states that decoded 16-bit images are converted to host endianness, and line 216 says
`SPNG_FMT_RGBA16` accepts any PNG format and bit depth. Therefore `SPNG_FMT_RGBA16` returns native
16-bit samples. Windows on both target architectures is little-endian, so the adapter's two-byte
R/B unit swap yields the required little-endian BGRA16 layout without reversing bytes within a
sample. No network access or third-party source modification was involved.

## TDD evidence

Tests for Bgra64 selection, alpha flags, libspng validation/exact pixels, composition, transforms,
and sequence validation were added before production changes. The first focused build failed on the
then-missing `PixelFormat::Bgra64`, establishing the red state. After the adapter implementation,
the first deliberately unresolved RGBA16 expectation failed with the observed first pixel
`(FFFF, FFFF, FFFF, 0000)`; the expectation and fixture provenance were then recorded from that
verified source value. The final focused tests and every repository gate below pass.

## Verification

Executable invocations used the repository-required `rtk` wrapper. Normal gates used
`$env:PVDKIT_BUILD_SUFFIX='-t12'`; experiment builds used `-t12deep`. Builds were sequential and
used at most six jobs. All configure/build commands exited 0 with zero compiler warnings.

### Normal x64 and x86 test matrix

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t12'
rtk cmake --preset debug
rtk cmake --build --preset debug --parallel 6
rtk ctest --preset debug

rtk cmake --preset release
rtk cmake --build --preset release --parallel 6
rtk ctest --preset release

rtk cmake --preset debug-x86
rtk cmake --build --preset debug-x86 --parallel 6
rtk ctest --preset debug-x86

rtk cmake --preset release-x86
rtk cmake --build --preset release-x86 --parallel 6
rtk ctest --preset release-x86
```

Final results:

| Preset | Result | Time | Included gates |
|---|---:|---:|---|
| `debug` | 15/15 passed | 84.32 s | guard, AVIF/RPGMVP e2e, sequence and leak tests |
| `release` | 19/19 passed | 37.30 s | all Debug gates plus import/export checks |
| `debug-x86` | 15/15 passed | 64.19 s | guard, AVIF/RPGMVP e2e, sequence and leak tests |
| `release-x86` | 19/19 passed | 30.32 s | all Debug gates plus import/export checks |

### Coverage

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t12'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '6'
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86
```

Both runs passed all 15 tests, found all 20 executable production source files, and verified 18/18
`Exports.cpp` functions through separate profiles for both AVIF and RPGMVP.

| Preset | Tests | Regions | Functions | Lines | Branches |
|---|---:|---:|---:|---:|---:|
| `coverage` | 15/15 in 79.60 s | 776/776 | 201/201 | 1608/1608 (100%) | 416/416 (100%) |
| `coverage-x86` | 15/15 in 107.22 s | 776/776 | 201/201 | 1608/1608 (100%) | 416/416 (100%) |

### Lint

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t12'
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6
```

The first run identified four cppcheck missing-initializer findings after the new last aggregate
member was added. The existing `DecoderOptions` members were defaulted, preserving aggregate use;
the final rerun was clean:

```text
clang-format:       0 finding(s) in   0.5 s
clang-tidy:         0 finding(s) in 600.0 s
cppcheck:           0 finding(s) in   2.6 s
PSScriptAnalyzer:   0 finding(s) in   6.1 s
BinSkim:            0 finding(s) in   1.3 s
lint: clean
```

No changed file is architecture-specific, so the conditional x86 lint run was not required.

### AddressSanitizer

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t12'
rtk cmake --preset asan
rtk cmake --build --preset asan --parallel 6
rtk ctest --preset asan
```

Result: 15/15 passed in 97.96 s; no sanitizer failure was reported. The slowest tests were
`avif_leak_tests` 51.77 s, `guard_tests` 19.07 s, and `rpgmvp_leak_tests` 17.74 s.

### Experiment builds and ABI

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t12deep'
rtk cmake --preset release -DPVDKIT_RPGMVP_DEEP_OUTPUT=ON
rtk cmake --build --preset release --parallel 6
rtk ctest --preset release -R '^rpgmvp_check_(imports|exports)$' --output-on-failure

rtk cmake --preset release-x86 -DPVDKIT_RPGMVP_DEEP_OUTPUT=ON
rtk cmake --build --preset release-x86 --parallel 6
rtk ctest --preset release-x86 -R '^rpgmvp_check_(imports|exports)$' --output-on-failure
```

Both architectures passed 2/2 requested checks. Direct verbose checks with
`scripts\check-imports.ps1` and `scripts\check-exports.ps1` identified x64 as `COFF-x86-64` and
x86 as `COFF-i386`. Each imports only `KERNEL32.dll` and exports exactly these eight bare names:
`pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`, `pvdPageDecode`, `pvdPageFree`, `pvdPageInfo`,
and `pvdPluginInfo`.

Experiment artifacts:

| Architecture | Path | SHA-256 |
|---|---|---|
| x64 | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-t12deep\plugins\rpgmvp\RPGMVP.pvd` | `D1B40102FEC355430CBEFA1EA928F380C225B8D0B642BECD38836F0B6AA8B17A` |
| x86 | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-x86-t12deep\plugins\rpgmvp\RPGMVP.pvd` | `1BF5BA587ACE69E266D126ABE0C88A5C8075E59B1FD9FE4D521D0967A5FC7526` |

No zip packages were produced.

### Final source audit

```powershell
rtk git diff --check
rtk git status --short
rtk git diff --stat
```

`git diff --check` produced no output and exited 0. Status contains only the Task 12 source, test,
design/fixture documentation, this report, and the new composition e2e test. Nothing is staged.
The tracked diff is 27 files with 276 insertions and 54 deletions; the untracked report and
composition test are not counted by `git diff --stat`.

## How to run the experiment in Far

Use the DLL whose architecture matches the Far Manager process. Install or select one RPGMVP.pvd
at a time using the normal local PictureView plugin procedure; keep the normal and experiment DLLs
separate so the result is attributable. PictureView's plugin information line should end with
`[experiment: deep output]` only for the experiment DLL.

1. Alpha flag check with the normal `release-t12` DLL: open
   `plugins\rpgmvp\fixtures\rgba8_48x48.rpgmvp` and
   `plugins\rpgmvp\fixtures\rgb8_816x624_gameover.rpgmvp`. The RGBA fixture is emitted as BGRA32
   with `Flags == 2`; transparent pixels/edges should composite correctly. The RGB fixture is BGR24
   with `Flags == 0` and must remain fully opaque. If the first image instead shows formerly
   transparent areas as solid color or halos while the RGB image is normal, the host is not using
   bit 2 as the meaningful-alpha marker assumed by check A.
2. Establish the shallow reference with the normal DLL: open
   `plugins\rpgmvp\fixtures\rgba16_60x20_par.rpgmvp` and
   `plugins\rpgmvp\fixtures\rgb16_88x4a.rpgmvp`. They should render through the established
   BGRA32/BGR24 path.
3. Replace/select the matching `release-t12deep` experiment DLL and reopen those same two 16-bit
   fixtures. Correct check-B behavior is the same geometry, colors and transparency as the shallow
   reference (possibly with visually smoother precision), with neither cropping nor row skew. The
   RGBA fixture is BGRA64 at pitch `60 * 8` with alpha flag 2; the RGB fixture is BGRA64 at pitch
   `88 * 8`, an opaque `FFFF` alpha lane, and flag 0.

Wrong colors, alternating-byte artifacts, stripes, row skew, a blank image, or rejection only with
the deep DLL means PictureView does not implement the proposed `nBPP == 64` BGRA/unsigned-short
contract as expected. A host crash is stronger evidence that PictureView's 64-bpp upload path is
absent or unsafe for this contract; it does not by itself indicate corrupt fixture data or a
libspng byte-order failure, because the automated tests validate exact decoded samples, pitches,
and flags before the host boundary. Record the DLL architecture and fixture that triggered it, then
restore the normal DLL.

## What was not done

Far Manager and `C:\Tools\FarManager` were not run or touched, so capability checks A and B await
Roma's manual observation. No network operation, dependency/compiler installation, source commit,
push, worktree, staging operation, or package zip was performed. The experiment trees ran the
requested RPGMVP import/export checks; their runtime deep path is covered by the normal-build
composition e2e test rather than a duplicate experiment DLL test target.

## Fix round 1

Date: 2026-09-13

### Outcome

Both review findings are fixed. `PVDKIT_RPGMVP_DEEP_OUTPUT` is now a `PRIVATE` definition of
`rpgmvp_composition`. RPGMVP adapter/e2e/sequence test targets receive the separate test-only
`PVDKIT_TEST_DEEP_OUTPUT` definition explicitly from the same CMake option, so no production
usage requirement leaks into unrelated consumers.

The RPGMVP composition identity test now expects the experiment comment suffix according to the
configured option. The DLL e2e test does the same and derives each fixture's expected output layout
from that configuration. In the ON build it verifies, through `LoadLibraryW` and the eight PVD
exports, that `rgba16_60x20_par.rpgmvp` is 64 bpp at pitch 480 with `Flags == 2`, and
`rgb16_88x4a.rpgmvp` is 64 bpp at pitch 704 with `Flags == 0`. In the default build the same test
retains the shallow expectations: respectively 32 bpp/pitch 240/flag 2 and 24 bpp/pitch 264/flag 0.
The exact first RGB16 pixel is also configuration-aware.

The shared host helper now names and tests its layout rule: tight positive 24/32-bit rows remain
accepted, 64-bit rows are accepted when pitch is at least `width * 8`, and unsupported depths,
undersized pitches, zero pitches and negative pitches are rejected. Its new doctest unit cases cover
both acceptance and rejection. The hostile/leak-page validator reuses the helper, so the full ON
suite exercises deep pages without retaining a second 24/32-only rule.

### TDD evidence

The new helper tests and configuration-aware RPGMVP expectations were added before their
implementation/CMake wiring. The first focused build was intentionally red:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t12'
rtk cmake --build --preset debug --target rpgmvp_e2e_tests rpgmvp_adapter_tests --parallel 6
```

It failed with `PVDKIT_TEST_DEEP_OUTPUT` undeclared in both configuration-aware test translation
units and ten `isSupportedDecodeLayout` undeclared diagnostics from the new positive/negative unit
tests. After the helper, private production definition and explicit test definitions were added,
the focused default and ON checks passed:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t12'
rtk cmake --build --preset debug --target rpgmvp_e2e_tests rpgmvp_adapter_tests rpgmvp_sequence_tests rpgmvp_leak_tests --parallel 6
rtk ctest --preset debug -R '^rpgmvp_(adapter|e2e|sequence|leak)_tests$' --output-on-failure
# 4/4 passed in 29.16 s

$env:PVDKIT_BUILD_SUFFIX = '-t12deep'
rtk cmake --preset debug -DPVDKIT_RPGMVP_DEEP_OUTPUT=ON
rtk cmake --build --preset debug --target rpgmvp_e2e_tests rpgmvp_adapter_tests rpgmvp_sequence_tests rpgmvp_leak_tests --parallel 6
rtk ctest --preset debug -R '^rpgmvp_(adapter|e2e|sequence|leak)_tests$' --output-on-failure
# 4/4 passed in 29.51 s
```

### Required verification

All builds were sequential, used at most six jobs, and emitted no compiler warnings.

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t12'
rtk cmake --build --preset debug --parallel 6
rtk ctest --preset debug
# 15/15 passed in 85.01 s; guard_tests passed

$env:PVDKIT_BUILD_SUFFIX = '-t12'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '6'
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
# 15/15 passed in 80.07 s
# source completeness: 20 executable production files
# AVIF and RPGMVP DLL profile checks: 18/18 Exports.cpp functions each
# TOTAL: 776/776 regions, 201/201 functions, 1608/1608 lines (100%), 416/416 branches (100%)

$env:PVDKIT_BUILD_SUFFIX = '-t12'
rtk cmake --build --preset release --parallel 6
rtk ctest --preset release
# 19/19 passed in 21.62 s, including every import/export test

$env:PVDKIT_BUILD_SUFFIX = '-t12deep'
rtk cmake --preset debug -DPVDKIT_RPGMVP_DEEP_OUTPUT=ON
rtk cmake --build --preset debug --parallel 6
rtk ctest --preset debug
# 15/15 passed in 49.66 s; this is the full ON suite against the option-built DLL
```

The requested default compile-database grep returned exactly one full match, the composition
translation unit. A concise same-line confirmation was:

```powershell
rtk rg -n --pcre2 -o 'PVDKIT_RPGMVP_DEEP_OUTPUT=0(?=.*rpgmvp_composition.*DefaultPlugin\.cpp)' build/debug-t12/compile_commands.json
382:PVDKIT_RPGMVP_DEEP_OUTPUT=0
```

The preceding fixed-string grep for `PVDKIT_RPGMVP_DEEP_OUTPUT` also returned only line 382, whose
object and source were `rpgmvp_composition.dir\src\DefaultPlugin.cpp.obj` and
`plugins\rpgmvp\src\DefaultPlugin.cpp`. There was no match for `Exports.cpp`, `Plugin.rc`, adapter
tests, e2e tests or sequence tests. Those configuration-aware tests contain only the explicitly
test-scoped `PVDKIT_TEST_DEEP_OUTPUT=0` definition.

### Experiment Release DLLs and ABI

The experiment DLLs were reconfigured and rebuilt after the fix:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t12deep'
rtk cmake --preset release -DPVDKIT_RPGMVP_DEEP_OUTPUT=ON
rtk cmake --build --preset release --target rpgmvp_plugin --parallel 6
rtk cmake --preset release-x86 -DPVDKIT_RPGMVP_DEEP_OUTPUT=ON
rtk cmake --build --preset release-x86 --target rpgmvp_plugin --parallel 6
```

Direct `scripts\check-imports.ps1` and `scripts\check-exports.ps1` runs passed for both files. The
x64 image is `COFF-x86-64`, the x86 image is `COFF-i386`, and each imports only `KERNEL32.dll` and
exports exactly `pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`, `pvdPageDecode`,
`pvdPageFree`, `pvdPageInfo`, and `pvdPluginInfo`.

| Architecture | Path | New SHA-256 |
|---|---|---|
| x64 | `build\release-t12deep\plugins\rpgmvp\RPGMVP.pvd` | `D3A6522E135FF82582EF3FB1601C2771B11DAEF791764FA931D96417B55D2801` |
| x86 | `build\release-x86-t12deep\plugins\rpgmvp\RPGMVP.pvd` | `66B60B1F5056ADE8B091319C3438A1AF07FFD53B16C0637EFEBB15E3B8E51404` |

### Lint and final audit

The first full lint run found only two clang-format diagnostics on the new conditional pixel
expectation; clang-tidy, cppcheck, PSScriptAnalyzer and BinSkim were already at zero. After that
formatting-only correction, the focused format check and required full rerun were clean:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t12'
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Tools clang-format -Jobs 6
# clang-format: 0 findings in 0.5 s; lint: clean

rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6
# clang-format:       0 findings in   0.5 s
# clang-tidy:         0 findings in 272.8 s
# cppcheck:           0 findings in   1.1 s
# PSScriptAnalyzer:   0 findings in   3.5 s
# BinSkim:            0 findings in   0.9 s
# lint: clean
```

`rtk git diff --check` exited 0 with no output. Nothing was staged or committed.

### What was not done

No network download or compiler installation was performed. CMake's manifest step populated the
build-local experiment dependency tree from installed/local cached artifacts and rebuilt libspng
once from its cached source archive. Far Manager and `C:\Tools\FarManager` were not run or touched.
Per the requested fix-round matrix, x86 lint, x86 coverage, ASan and packaging were not rerun. No
commit, push, reset, checkout, clean, worktree, staging operation or package zip was performed.
