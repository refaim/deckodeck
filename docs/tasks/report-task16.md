# Task 16 implementation report: shared colour pipeline and AVIF HDR presentation

Date: 2026-09-14  
Branch: `master`  
Requested starting commit: `a0bfe52`  
Final observed `HEAD`: `80927003299c4106520b76faf98c9d5b9c4b3ca4`  
Build suffix: `-t16`

`HEAD` advanced to the descendant commit `8092700` outside this task while the work was in
progress. I did not commit, stage, reset, switch branches, or use a worktree. The Task 16 changes
remain uncommitted in the working tree.

## Outcome

Task 16 is implemented. The shared core now presents CICP-described RGB as display-referred sRGB,
and AVIF 1.2.0 is its first metadata-aware consumer. PQ and HLG are decoded to linear light and
tone-mapped with the BT.2390/BT.2408 Annex 5 EETF; supported wide-gamut primaries are transformed to
BT.709/sRGB; the result is encoded with the exact sRGB OETF into BGRA64. Existing sRGB-ish AVIF
paths keep their identity short-circuit and their pinned pixel values.

All requested automated gates are green on x64 and x86: four Debug/Release CTest suites, both
100% line-and-branch coverage gates, ASan, guard tests, both full lint passes, and all Release
import/export checks. Builds used clang-cl 19.1.5, C++23, `/W4 /WX`, the static CRT, and at most six
parallel jobs. No compiler warning was emitted.

## Implementation

- Added `src/core/colour/Transfer.hpp/.cpp` with exact sRGB, BT.1886 gamma 2.4, gamma 2.2,
  gamma 2.8, linear, ST 2084 PQ, and ARIB STD-B67/BT.2100 HLG transfer functions. H.273 codes
  1/6/14/15 share BT.1886 when conversion is required; code 2 and unknown values safely fall back
  to sRGB.
- Added `src/core/colour/Primaries.hpp/.cpp`. RGB-to-XYZ matrices are derived at compile time from
  H.273 Table 2 chromaticities, with Bradford white adaptation for P3-DCI and BT.470M. Codes
  1, 2, 4, 5, 6, 7, 9, 11, 12, and 22 are covered; unknown primaries fall back to BT.709.
- Added `src/core/colour/ToneMap.hpp/.cpp` with the BT.2390-10 section 5.4.1 Hermite knee for a
  100-nit, 0.005-nit-black target. The selected chroma policy is the simpler BT.2390 maxRGB form:
  the largest channel drives the EETF and one common scale preserves channel ratios. This keeps
  the policy isolated in `applyMaxRgb`, is deterministic, and avoids hue shifts from independent
  per-channel mapping.
- Added `src/core/colour/Pipeline.hpp/.cpp`. `Presentation` caches a 65,536-entry transfer LUT in a
  `std::unique_ptr`, then performs the primaries matrix, HLG 1000-nit/system-gamma-1.2 OOTF or PQ
  absolute-luminance handling, optional HDR tone map, exact sRGB encoding, clamp, and rounded
  16-bit quantisation. It allocates nothing per pixel and preserves alpha exactly.
- `core::ImageMeta` now carries `std::optional<float> masteringPeakNits`. PQ uses a finite positive
  supplied peak capped at 10,000 nits, otherwise 1,000 nits. HLG always uses its 1,000-nit
  reference display.
- `FileSession::decodePage` requests BGRA64 for any image that needs presentation, even if its
  source depth is 8-bit. It applies colour row by row immediately after `decodeFrame`, before the
  colour-agnostic clap/irot/imir transform. Identity 8-bit sRGB-ish images still request BGR24 when
  opaque or BGRA32 with alpha and bypass the pipeline.
- The AVIF adapter reads non-zero `avifImage::clli.maxCLL` into `masteringPeakNits`. The installed
  libavif 1.4.2 public `avifImage` exposes `clli` but no decoded `mdcv`; its reader also does not
  surface `mdcv`. A custom ISOBMFF parser was therefore not introduced. Missing peak metadata uses
  the specified 1,000-nit fallback.
- The AVIF description appends a concise conversion note for PQ/HLG, named wide-gamut primaries,
  or safe unknown-code fallbacks. Identity images receive no note. The two HDR integration cases
  report `→ sRGB (BT.2390 tone map from PQ 470 nit)` and
  `→ sRGB (BT.2390 tone map from PQ 1000 nit)`.
- libavif/libyuv remain responsible for YUV matrix coefficients and range expansion. The shared
  module accepts RGB only; `Cicp::matrix` and `fullRange` remain informational.
- AVIF was bumped to 1.2.0. Its English/Russian readmes and Russian ChangeLog describe HDR/P3/
  Rec.2020 presentation and still state that ICC profiles are not applied. RPGMVP documentation
  now explicitly says PNG `gAMA`, `cHRM`, and ICC data are ignored; its pixels remain unaffected.
- Updated architecture, AVIF design/fixture provenance, and plugin documentation to record the
  placement, lifetime, fallback, identity, and 64-bpp contracts.

## TDD and numerical checks

Tests were added in the requested order: transfer functions, primaries, tone map, pipeline,
`FileSession`, AVIF metadata/describer, then DLL integration. Each unit began red: the new module
tests first failed to compile because their APIs did not exist; the `FileSession` test then exposed
the old BGRA32/no-presentation path; AVIF tests exposed the missing CLLI and description fields;
and the e2e test started with deliberately wrong HDR pixel expectations. Minimal implementations
were added between those stages.

The tests cover:

- published transfer anchors and 4,096-sample round trips, including PQ 0.5080784215 → 100 nits,
  HLG 0.5 → 1/12 scene light, sRGB 0.5 → about 0.214, and BT.1886 0.5 → about 0.189;
- derived BT.2020→BT.709 and P3-D65→BT.709 matrices to `1e-4`, plus white preservation for every
  supported primary set;
- BT.2390 1000→100-nit anchors after full `PQ(Lb)`/source-span normalization: 0, 10, 100,
  400, and 1000 source nits map to approximately 0.00005000, 0.10252768, 0.69660405,
  0.97767375, and 1.0 after the specified output clamp, with monotonicity over 10,000 samples;
- maxRGB ratio preservation, exact black behavior, source-peak validation/capping, HLG OOTF,
  unknown-code fallbacks, alpha preservation, byte-span bridging, and the identity bypass;
- BGRA64 selection and colour-before-transform/progress ordering in `FileSession`;
- real AVIF CLLI (`colors_hdr_rec2020.avif` has maxCLL 470), absent-CLLI fallback, descriptions,
  and exact DLL pixels.

The existing SDR exact-pixel expectations were not changed. One expectation-only metadata flag was
added for `draw_points_idat_progressive.avif`, whose unknown primaries now invoke the documented
safe fallback and therefore require BGRA64.

Exact HDR DLL samples, in BGRA16 order, are:

| Fixture and point | BGRA16 |
|---|---|
| `colors_hdr_rec2020.avif` (0,0) | `(0, 1014, 65535, 65535)` |
| `colors_hdr_rec2020.avif` (100,100) | `(7854, 53351, 63663, 65535)` |
| `colors_hdr_rec2020.avif` (199,199) | `(65535, 65535, 65535, 65535)` |
| `cosmos1650_yuv444_10bpc_p3pq.avif` (0,0) | `(48085, 35561, 25307, 65535)` |
| `cosmos1650_yuv444_10bpc_p3pq.avif` (512,214) | `(2682, 20499, 42409, 65535)` |
| `cosmos1650_yuv444_10bpc_p3pq.avif` (1023,427) | `(0, 50080, 49456, 65535)` |

## Independent FFmpeg/zscale comparison

Tone mapping remains deliberately excluded because FFmpeg's tone mapper is not this BT.2390
operator. Fix round 1 also made the input side use zscale explicitly: the former plain `format`
conversion used FFmpeg's auto-scale/swscale YUV→RGB path while the reference used zscale's path,
so the test mixed two YUV conversions before it compared the transfer-plus-primaries stage. The
reviewer's `0.009017` red-channel difference at the HDR-range `(100,100)` sample came from that
mismatch, not from this pipeline's float/LUT precision.

The corrected commands for `colors_hdr_rec2020.avif` are:

```powershell
rtk "C:\Users\Roma\scoop\apps\ffmpeg-shared\current\bin\ffmpeg.exe" -hide_banner -loglevel warning -i plugins\avif\fixtures\colors_hdr_rec2020.avif -vf "zscale=min=bt2020nc:m=gbr:pin=bt2020:p=bt2020:tin=smpte2084:t=smpte2084:npl=100,format=gbrpf32le" -frames:v 1 -f rawvideo -y C:\Users\Roma\AppData\Local\Temp\pvdkit-task16-fix-colors-zscale-encoded-gbrpf32le.raw

rtk "C:\Users\Roma\scoop\apps\ffmpeg-shared\current\bin\ffmpeg.exe" -hide_banner -loglevel warning -i plugins\avif\fixtures\colors_hdr_rec2020.avif -vf "zscale=t=linear:p=bt709:m=gbr:npl=100,format=gbrpf32le" -frames:v 1 -f rawvideo -y C:\Users\Roma\AppData\Local\Temp\pvdkit-task16-fix-colors-linear709-gbrpf32le.raw
```

At `(100,100)`, encoded raw G/B/R is
`0.525673628 / 0.334289908 / 0.547426581`; zscale linear-BT.709 raw G/B/R is
`1.16382432 / 0.0248015784 / 1.73502994`. Reordered to RGB, this implementation gives
`(1.73503708, 1.16390387, 0.0247946761)`, for absolute errors
`(0.00000714, 0.00007955, 0.00000690)`. This diagnoses the old discrepancy, but the sample is
HDR-range and is not used to satisfy the SDR-range tolerance.

An exhaustive scan of all 40,000 pixels in `colors_hdr_rec2020.avif` found no untone-mapped
linear-BT.709 pixel wholly in `[0,1]`: every pixel has at least one channel above 1.0. Among pixels
with no negative channel, the lowest maximum channel is `1.37781942`. The requested genuine
SDR-range comparison therefore uses the P3/PQ cosmos fixture, which FFmpeg can convert when its
`chroma-derived-nc` input matrix is given to zscale explicitly:

```powershell
rtk "C:\Users\Roma\scoop\apps\ffmpeg-shared\current\bin\ffmpeg.exe" -hide_banner -loglevel warning -i plugins\avif\fixtures\cosmos1650_yuv444_10bpc_p3pq.avif -vf "zscale=min=chroma-derived-nc:m=gbr:pin=smpte432:p=smpte432:tin=smpte2084:t=smpte2084:npl=100,format=gbrpf32le" -frames:v 1 -f rawvideo -y C:\Users\Roma\AppData\Local\Temp\pvdkit-task16-fix-cosmos-encoded-gbrpf32le.raw

rtk "C:\Users\Roma\scoop\apps\ffmpeg-shared\current\bin\ffmpeg.exe" -hide_banner -loglevel warning -i plugins\avif\fixtures\cosmos1650_yuv444_10bpc_p3pq.avif -vf "zscale=t=linear:p=bt709:m=gbr:npl=100,format=gbrpf32le" -frames:v 1 -f rawvideo -y C:\Users\Roma\AppData\Local\Temp\pvdkit-task16-fix-cosmos-linear709-gbrpf32le.raw
```

Raw samples and the independently calculated RGB results are:

| Pixel | Encoded raw G/B/R | zscale linear raw G/B/R | Our linear RGB | Max absolute error |
|---|---|---|---|---:|
| `(512,214)` | `0.292456090 / 0.172915980 / 0.401375532` | `0.0811400041 / 0.00322370953 / 0.382961124` | `(0.382996665, 0.0811467920, 0.00322357413)` | `0.00003555` |
| `(752,128)` | `0.439880162 / 0.320413500 / 0.494066209` | `0.482266605 / 0.0859083757 / 0.952770710` | `(0.952604290, 0.482280725, 0.0859279534)` | `0.00016642` |

Every zscale output channel above is in `[0,1]`. `PipelineTests.cpp` uses plain absolute checks
`std::abs(ours - zscale) <= 2/255`; the observed worst error is about 47 times below that bound.

`ffprobe` confirmed `colors_hdr_rec2020.avif` as 10-bit BT.2020/PQ/BT.2020-NCL,
`cosmos1650_yuv444_10bpc_p3pq.avif` as 10-bit P3-D65/PQ/chroma-derived-NCL, and
`weld_sato_12B_8B_q0.avif` as 12-bit with unknown transfer/primaries/matrix metadata.

## Verification

All commands below were run sequentially with:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t16'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '6'
```

The four normal configurations were configured and built with these commands:

```powershell
rtk cmake --preset debug
rtk cmake --build --preset debug --parallel 6
rtk ctest --preset debug --output-on-failure

rtk cmake --preset release
rtk cmake --build --preset release --parallel 6
rtk ctest --preset release --output-on-failure

rtk cmake --preset debug-x86
rtk cmake --build --preset debug-x86 --parallel 6
rtk ctest --preset debug-x86 --output-on-failure

rtk cmake --preset release-x86
rtk cmake --build --preset release-x86 --parallel 6
rtk ctest --preset release-x86 --output-on-failure
```

Final full-suite results after the last source change:

| Preset | Result | Time |
|---|---:|---:|
| `debug` x64 | 17/17 passed | 77.81 s |
| `release` x64 | 21/21 passed | 32.28 s |
| `debug-x86` | 17/17 passed | 111.55 s |
| `release-x86` | 21/21 passed | 47.23 s |

The Release totals include both plugins' import and export tests. Guard, package-document,
unit, adapter, e2e, sequence, and leak tests passed on both architectures. During one earlier final
`debug-x86` attempt, `leakcheck_tests` transiently observed 24 KiB of Windows view-accounting noise
in its deliberately failing self-test and the run was 16/17. The isolated test immediately passed
1/1, and the complete final rerun above passed 17/17; no production/plugin test failed.

Coverage commands and final results:

```powershell
$env:CMAKE_BUILD_PARALLEL_LEVEL = '6'
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86
```

| Preset | Tests | Regions | Functions | Lines | Branches |
|---|---:|---:|---:|---:|---:|
| `coverage` x64 | 17/17 in 89.87 s | 1027/1027 (100%) | 255/255 (100%) | 2070/2070 (100%) | 650/650 (100%) |
| `coverage-x86` | 17/17 in 131.65 s | 1024/1024 (100%) | 255/255 (100%) | 2065/2065 (100%) | 648/648 (100%) |

Both runs found all 24 executable production source files. Both loaded plugins emitted runtime
profiles and reported 18/18 `Exports.cpp` functions executed. The first x64 coverage run was red at
98.51% lines and 99.55% branches, which drove missing edge-case tests and compile-time-only matrix
derivation cleanup. The first x86 run was red at 99.90% lines and 99.85% branches because a
`size_t > UINT32_MAX` ICC-size failure is impossible on x86; the narrowing check is now compiled
only when `size_t` is actually wider. Both final gates are 100/100 without exclusions.

ASan commands and result:

```powershell
rtk cmake --preset asan
rtk cmake --build --preset asan --parallel 6
rtk ctest --preset asan --output-on-failure
```

The ASan build completed without compiler warnings, and CTest passed 17/17 in 109.33 s.

Lint commands:

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6 -BuildDir build\debug-x86-t16 -ReleaseDir build\release-x86-t16
```

| Architecture | clang-format | clang-tidy | cppcheck | PSScriptAnalyzer | BinSkim |
|---|---:|---:|---:|---:|---:|
| x64 | 0 in 0.6 s | 0 in 373.3 s | 0 in 1.9 s | 0 in 4.8 s | 0 in 1.1 s |
| x86 | 0 in 0.6 s | 0 in 305.1 s | 0 in 2.0 s | 0 in 4.7 s | 0 in 1.2 s |

Both ended with `lint: clean`. A cold vcpkg configure emitted a non-compiler warning that its
sandboxed binary-cache submission under `%LOCALAPPDATA%\vcpkg\archives` was denied; dependency
installation itself completed successfully. All project compiles and all five analysis tools were
warning/finding-free.

`rtk git diff --check` exited 0 with no output after the final report update.

## Release binaries and ABI policy

Hashes were produced with PowerShell `Get-FileHash -Algorithm SHA256` after the final Release
rebuilds:

| Architecture/plugin | Absolute path | SHA-256 |
|---|---|---|
| x64 AVIF 1.2.0 | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-t16\plugins\avif\AVIF.pvd` | `123BF2A9043ABE40A97B67AEB00F10B20D6AD3E6FD2752E2DF90D1084C9238D0` |
| x64 RPGMVP | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-t16\plugins\rpgmvp\RPGMVP.pvd` | `E0BEEABBAC765A155A92672AD3618AA5117144A1B156D18CE310EC7B9EFA59DF` |
| x86 AVIF 1.2.0 | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-x86-t16\plugins\avif\AVIF.pvd` | `0CED0FE0E049F3E020A4213A91849DFF52154C3854B3F9078E27CD4D99E707D8` |
| x86 RPGMVP | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-x86-t16\plugins\rpgmvp\RPGMVP.pvd` | `5981BEF7E62DC663FF9D4396FA65B897860E816E4ACC498706539C5827BF330C` |

The direct verbose checks were:

```powershell
rtk ctest --preset release -R "_check_(imports|exports)$" -V
rtk ctest --preset release-x86 -R "_check_(imports|exports)$" -V
```

Both passed 4/4. Every x64 file was reported as `COFF-x86-64`; every x86 file as `COFF-i386`.
Each of the four import tables contains exactly one module: `KERNEL32.dll`. Each export table
contains exactly these eight bare names:

1. `pvdExit`
2. `pvdFileClose`
3. `pvdFileOpen`
4. `pvdInit`
5. `pvdPageDecode`
6. `pvdPageFree`
7. `pvdPageInfo`
8. `pvdPluginInfo`

## What Roma should look at in Far

Far Manager was not run. With the appropriate x64 or x86 AVIF 1.2.0 DLL installed through the
normal local PictureView procedure, inspect:

1. `plugins\avif\fixtures\colors_hdr_rec2020.avif` — the 10-bit Rec.2020/PQ chart should no longer
   appear dark or incorrectly saturated. Mid-tones should be substantially brighter, highlights
   should roll into the 100-nit display range rather than hard clipping, and the info line should
   say `BT.2390 tone map from PQ 470 nit`.
2. `plugins\avif\fixtures\cosmos1650_yuv444_10bpc_p3pq.avif` — the P3/PQ image should have normal
   SDR display brightness and less oversaturated wide-gamut colour. Its info line should say
   `BT.2390 tone map from PQ 1000 nit` because the file has no exposed CLLI.
3. `plugins\avif\fixtures\colors_sdr_srgb.avif` and the ordinary SDR/alpha fixtures — compare with
   AVIF 1.1.0. They should be visually and pixel-for-pixel unchanged; their info lines must not gain
   a conversion note.
4. `plugins\avif\fixtures\weld_sato_12B_8B_q0.avif` — it is 12-bit but carries unknown CICP in the
   available metadata. It should still decode at 64 bpp, and the info line should explicitly record
   the safe sRGB/BT.709 fallbacks rather than claiming a PQ tone map.
5. Open representative RPGMVP files as a control. Their appearance should be unchanged.

## What was not done

- Far Manager was not run and `C:\Tools\FarManager` was not touched, so visual host confirmation is
  intentionally left to Roma.
- ICC profiles are still not applied. Their existing extraction/experimental transport remains
  separate from this CICP pipeline.
- `mdcv` is not consumed because libavif 1.4.2 does not expose decoded mastering-display metadata;
  only non-zero `clli.maxCLL` is available through its public decoded image. Missing data follows the
  required 1,000-nit fallback. No private libavif patch or custom container parser was added.
- No SIMD optimization was added; the specified LUT plus scalar per-pixel matrix/tone-map path is
  sufficient and performs no per-pixel allocation.
- No network access, compiler/dependency installation, commit, push, reset, worktree, or Far/plugin
  installation was performed.

## Fix round 1

Date: 2026-09-14  
Review: `docs/tasks/review-task16.md` (four findings)

All four review findings were addressed:

- `docs/ARCHITECTURE.md` now distinguishes the implemented CICP presentation pipeline from the
  remaining exclusions: ICC profiles are not applied because PictureView ignores them and pvdkit
  has no CMS yet; the PVD interface has no user exposure/tone controls; gain maps are unsupported.
- `Transfer::nitsToPq` evaluates the complete ST 2084 inverse EOTF at zero, producing
  `c1^m2 ≈ 7.3095590e-7`. `ToneMap::Eetf` retains `PQ(Lb)` and the source span
  `PQ(Lw) - PQ(Lb)` for input normalization, target normalization, reconstruction, and knee
  reporting. The BT.2390 fourth-power black lift remains unchanged.
- The FFmpeg comparison now uses explicit zscale YUV→RGB conversion on both sides of the stage
  comparison and plain absolute assertions. Two real cosmos P3/PQ pixels whose untone-mapped
  linear-BT.709 channels are all in `[0,1]` pass `std::abs(ours - zscale) <= 2/255`; the maximum
  observed difference is `0.00016642`.
- AVIF's README, package readmes, and design now state the complete layout contract: identity 8-bit
  SDR is BGR24/BGRA32; every presentation-required HDR/wide-gamut image at any depth and every
  source deeper than 8 bits is BGRA64. The RPGMVP design's obsolete generic colour-management
  exclusion was narrowed to the PNG metadata it actually ignores.

### ST 2084 and BT.2390 numbers

The full standard equations were independently re-derived in double precision. For a zero-nit
source black, 1000-nit source white, and 100-nit/0.005-nit-black target:

| Quantity | Value |
|---|---:|
| `PQ(0)` | `0.00000073095590257839665` |
| `PQ(1000) - PQ(0)` | `0.75182636529113844` |
| normalized target white | `0.67579126513440058` |
| `KS` | `0.51368689770160092` |
| knee luminance | `27.858465100845265 nit` |

The re-derived 1000→100-nit anchors are:

| Source nits | Output before final clamp | Final normalized output |
|---:|---:|---:|
| 0 | `0.0000500000000000` | `0.0000500000000000` |
| 10 | `0.102527678140590` | `0.102527678140590` |
| 100 | `0.696604046951201` | `0.696604046951201` |
| 400 | `0.977673750067332` | `0.977673750067332` |
| 1000 | `1.00166087277378` | `1.0` |

The slight pre-clamp overshoot at source white is produced by the specified black-lift term; the
existing final target-range clamp maps the endpoint to exactly 1.0. The BGRA16 e2e samples listed
earlier did not move at quantized precision and their existing exact assertions remained green.

### TDD evidence

The review regressions were written before the production fix. The first targeted run was red:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t16'
$env:CMAKE_BUILD_PARALLEL_LEVEL='6'
rtk cmake --build --preset debug --target core_tests --parallel 6
rtk ctest --preset debug -R "^core_tests$" --output-on-failure
```

Result: `0/1` CTest entries passed; doctest reported 79 cases with 77 passed / 2 failed and 70,263
assertions with 70,259 passed / 4 failed. The failures were the non-zero ST 2084 black endpoint and
the full-span BT.2390 normalization. After the minimal implementation and float-precision
calibration of the independently derived constants, the same focused command passed `1/1` in
0.32 s.

### FFmpeg/zscale regeneration

The exact four successful commands, raw values, selected coordinates, and measured absolute errors
are recorded in “Independent FFmpeg/zscale comparison” above. One additional plain-format attempt
on the cosmos fixture failed because FFmpeg's auto-scale conversion does not accept
`chroma-derived-nc`; passing that input matrix to zscale explicitly succeeded.

The original `colors_hdr_rec2020.avif` requirement could not literally supply an SDR-range pixel:
an exhaustive scan of the exact 200×200 linear-BT.709 raw frame found zero pixels wholly in
`[0,1]`. This is why the corrected automated SDR-range comparison uses the decodable cosmos
fixture, while the Rec.2020 `(100,100)` sample remains in the report only to diagnose the old
`0.009017` discrepancy.

### Verification rerun

All commands ran sequentially with `PVDKIT_BUILD_SUFFIX=-t16`, at most six build jobs, and no
network access:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t16'
$env:CMAKE_BUILD_PARALLEL_LEVEL='6'

rtk cmake --build --preset debug --parallel 6
rtk ctest --preset debug --output-on-failure

rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage

rtk cmake --build --preset release --parallel 6
rtk ctest --preset release --output-on-failure

rtk cmake --build --preset debug-x86 --parallel 6
rtk ctest --preset debug-x86 --output-on-failure

rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86

rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6
```

Results:

| Gate | Result |
|---|---:|
| Debug x64 | 17/17 passed in 90.61 s |
| Coverage x64 tests | 17/17 passed in 98.89 s |
| Coverage x64 | 1025/1025 regions, 255/255 functions, 2069/2069 lines, 648/648 branches |
| Release x64 | 21/21 passed in 38.65 s |
| Debug x86 | 17/17 passed in 132.06 s |
| Coverage x86 tests | 17/17 passed in 152.46 s |
| Coverage x86 | 1022/1022 regions, 255/255 functions, 2064/2064 lines, 646/646 branches |

Both coverage scripts found all 24 executable production source files and reported 100% lines and
100% branches. Guard and package-document tests passed in every listed CTest run. All x64/x86
builds completed with zero compiler warnings.

The x64 lint command finished clean:

| Tool | Findings | Time |
|---|---:|---:|
| clang-format | 0 | 0.7 s |
| clang-tidy | 0 | 340.9 s |
| cppcheck | 0 | 1.8 s |
| PSScriptAnalyzer | 0 | 4.8 s |
| BinSkim | 0 | 1.2 s |

The Release ABI checks were also rerun verbosely:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t16'
rtk ctest --preset release -R "_check_(imports|exports)$" -V
```

Result: 4/4 passed in 1.31 s. Both `AVIF.pvd` and `RPGMVP.pvd` are `COFF-x86-64`; each imports
only `KERNEL32.dll` and exports exactly the eight bare names `pvdExit`, `pvdFileClose`,
`pvdFileOpen`, `pvdInit`, `pvdPageDecode`, `pvdPageFree`, `pvdPageInfo`, and `pvdPluginInfo`.

Updated x64 Release hashes:

| Plugin | SHA-256 |
|---|---|
| AVIF 1.2.0 | `123BF2A9043ABE40A97B67AEB00F10B20D6AD3E6FD2752E2DF90D1084C9238D0` |
| RPGMVP | `E0BEEABBAC765A155A92672AD3618AA5117144A1B156D18CE310EC7B9EFA59DF` |

Two intermediate build attempts correctly failed the package-document checks after editing the
distribution readmes: first on LF line endings and then on the missing Russian UTF-8 BOM. The files
were normalized back to their required encodings and CRLF endings; all final configure, package-doc,
coverage, and lint gates passed.

### Not rerun / not done

- Far Manager was not run and `C:\Tools\FarManager` was not touched.
- The fix-round command list did not request Release x86, ASan, or x86 lint, so those historical
  initial-implementation results were not rerun. The changed C++ compiled warning-free and passed
  full tests plus 100/100 coverage on x86.
- No dependency/compiler installation, network access, commit, push, reset, worktree, staging, or
  plugin installation was performed.
