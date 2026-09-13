## Substantive findings

1. `docs/ARCHITECTURE.md:791` — the architecture now defines the shared colour pipeline at lines 396–404 and requires it in `FileSession` at line 441, but the out-of-scope section still excludes “Colour management of any kind.” These are mutually incompatible statements in the controlling design, and `AGENTS.md` explicitly forbids choosing between conflicting architecture requirements. Remove the obsolete exclusion or narrow it to the unsupported host/ICC/configuration facilities.

2. `src/core/colour/Transfer.cpp:89` (and `src/core/colour/ToneMap.cpp:11`) — `nitsToPq(0)` is special-cased to zero, whereas the specified ST 2084 inverse EOTF and the named reference implementation evaluate the equation and return `c1^m2 = 7.309559e-7`. `ToneMap` then relies on that non-standard zero to replace BT.2390's `(PQ(L)-pqLb)/(pqLw-pqLb)` normalization with `PQ(L)/pqLw`. The numerical effect is tiny, but this is an exact-math contract and it makes both the transfer function and EETF differ from the reference at the black endpoint. Remove the special case, retain `pqLb` and the source PQ span in the EETF, and replace the test that pins the incorrect zero result with independent endpoint/normalization checks.

3. `tests/core/colour/PipelineTests.cpp:140` — the ffmpeg comparison does not enforce the reported absolute `2/255` tolerance. `doctest::Approx(...).epsilon(...)` is scaled/relative (with default scale 1); at the red reference value 1.735030 it permits about 0.02145. My independent pipeline result is 1.726013, an error of 0.009017, which exceeds `2/255 = 0.007843` but passes this assertion. The chosen red and green reference values are also above 1, so this is not an SDR-range comparison as required. Consequently, `docs/tasks/report-task16.md:121` incorrectly says that the reported numbers pass the required tolerance. Use an explicit absolute-difference assertion, compare genuinely SDR-range samples (with tone mapping excluded), and update the report from the resulting measurements.

4. `plugins/avif/README.md:13` — the format description still says that 8-bit alpha images are handed off as BGRA32 and opaque images as BGR24. Task 16 deliberately routes an 8-bit HDR/wide-gamut image through BGRA64 whenever presentation is active, irrespective of source depth. This is user-facing documentation for the changed contract and is now false. Qualify the sentence as the identity/SDR path and document BGRA64 for presentation-required 8-bit inputs.

## Nits

None.

## Verified

- `git status --short`, all scoped diffs, every new `src/core/colour/**` and `tests/core/colour/**` file, the task contract, implementer report, architecture, PVD references, and the named scratch reference implementations were inspected. `rtk git diff --check` exited 0 with no output.
- PQ constants independently evaluate to `m1=0.1593017578125`, `m2=78.84375`, `c1=0.8359375`, `c2=18.8515625`, `c3=18.6875`; `PQ EOTF(0.5080784)=99.9999786 nits` and `PQ EOTF(1)=10000 nits`. The zero-code issue is finding 2.
- HLG inverse OETF gives `0.5 -> 1/12` exactly and `0.75 -> 0.2649625605`; with the implemented BT.2100 gamma-1.2 OOTF at 1000 nits, the latter becomes `203.152146 nits`. The requested `0.75 -> 0.25` is not an anchor of the BT.2100 equation: scene light 0.25 maps to code about 0.73855. The code's constants and high branch are correct.
- The exact sRGB inverse pieces and BT.1886 exponent were checked independently (`sRGB^-1(0.5)=0.2140411405`, `BT.1886(0.5)=0.1894645708`).
- Independent double-precision derivation produced BT.2020→BT.709 rows `(1.660491, -0.587641, -0.072850)`, `(-0.124550, 1.132900, -0.008349)`, `(-0.018151, -0.100579, 1.118730)`, agreeing with BT.2087 within `1e-3`. P3-D65→709 was `(1.224940, -0.224940, 0)`, `(-0.042057, 1.042057, 0)`, `(-0.019638, -0.078636, 1.098274)`. The P3-DCI and BT.470M paths correctly apply Bradford adaptation; independently derived adaptation rows were DCI→D65 `(1.024497, 0.015164, 0.019689)`, `(0.025612, 0.972586, 0.004716)`, `(0.006384, -0.012268, 1.147942)` and C→D65 `(0.990374, -0.007231, -0.011730)`, `(-0.012480, 1.015744, -0.002958)`, `(-0.003598, 0.006834, 0.917374)`.
- With the implementation's zero convention, the BT.2390 calculations reproduce the report: `maxLum=0.6757915803`, `KS=0.5136873705`, knee `27.8584651 nits`, and 1000→100-nit outputs `0 -> 0.00005`, `10 -> 0.1025277921`, `100 -> 0.6966041389`, `400 -> 0.9776738255`, `1000 -> 1`. The Hermite polynomial, `b=minLum`, and fourth-power black lift are otherwise implemented as specified.
- maxRGB uses one common ratio for all channels. At source peak, transformed BT.2020 red/green/blue are scaled to `(1,-0.075008,-0.010931)`, `(-0.518705,1,-0.088780)`, and `(-0.065118,-0.007463,1)` before the final output-gamut clamp, preserving ratios and producing the expected saturated primaries.
- Identity signalling is exactly primaries `{1,2}` plus transfer `{1,2,6,13,14,15}`. The pre-existing SDR exact-pixel expectations were not changed. The 8-bit presentation test proves that `FileSession` requests BGRA64, applies colour per row before mirror/rotation/crop, and converts back correctly. The libavif adapter supplies a 16-bit user buffer only after `NthImage`; its target layout, row bytes, alpha handling, and `YUVToRGB` call order are correct.
- `Presentation` owns one 65,536-entry LUT through `std::unique_ptr`, allocates nothing per pixel, rounds with `std::lround` before `uint16_t` conversion, and leaves alpha unchanged. On x86, the largest rounded value is only 65,535 (safe in 32-bit `long`); row/byte arithmetic uses `std::size_t`. No owning raw pointers, forbidden allocation APIs, foreign headers in core, or C translation units were introduced.
- Re-running the report's ffmpeg method with tone mapping excluded produced encoded GBR `(0.5251697, 0.3339742, 0.5468986)` and zscale linear BT.709 RGB `(1.735030, 1.163824, 0.02480158)`, exactly matching the report. Independent PQ decoding plus the derived matrix produced `(1.726013, 1.158168, 0.02487921)`; the red difference exposes finding 3.
- `plugins/avif/package/ChangeLog:1` is in the required Roma-style form: `AVIF 1.2.0 13.09.2026`. CLLI is read only after successful decode, zero `maxCLL` is treated as absent, and the describer note/fallback behavior is covered.

Commands and key output:

```text
$env:PVDKIT_BUILD_SUFFIX='-review'; $env:CMAKE_BUILD_PARALLEL_LEVEL='6'; rtk cmake --preset debug
-- Build files have been written to: ...\build\debug-review

rtk cmake --build --preset debug --parallel 6
exit 0; build completed with no warnings

rtk ctest --preset debug --output-on-failure
100% tests passed, 0 tests failed out of 17
Total Test time (real) = 80.61 sec

rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
100% tests passed, 0 tests failed out of 17
TOTAL: regions 1027/1027, functions 255/255, lines 2070/2070, branches 650/650
Coverage completeness: 24/24 expected source files present
Coverage gate passed: every first-party file is at 100% lines and 100% branches.
```

Scoped coverage rows:

| File | Lines | Branches |
|---|---:|---:|
| `src/core/FileSession.cpp` | 93/93 (100%) | 44/44 (100%) |
| `src/core/IDecoder.hpp` | 2/2 (100%) | 0/0 |
| `src/core/colour/Pipeline.cpp` | 96/96 (100%) | 38/38 (100%) |
| `src/core/colour/Primaries.cpp` | 78/78 (100%) | 74/74 (100%) |
| `src/core/colour/ToneMap.cpp` | 39/39 (100%) | 6/6 (100%) |
| `src/core/colour/Transfer.cpp` | 121/121 (100%) | 86/86 (100%) |
| `plugins/avif/src/adapters/avif/Decoder.cpp` | 274/274 (100%) | 58/58 (100%) |
| `plugins/avif/src/core/Describe.cpp` | 117/117 (100%) | 56/56 (100%) |
| `src/pvd/Shim.cpp` | 284/284 (100%) | 48/48 (100%) |

The guard test passed as part of both test runs. Per the review instructions, x86, ASan, lint, and release import/export checks were not rerun; therefore the corresponding historical claims in the implementer report are not independently asserted here.

## Verdict

REJECT

## Round 2

## Substantive findings

## Nits

None.

## Verified

- Inspected only the requested fix scope in `src/core/colour/Transfer.cpp`,
  `src/core/colour/ToneMap.cpp`, `tests/core/colour/*`, the architecture out-of-scope section,
  the AVIF README/design/package readmes, and `plugins/rpgmvp/DESIGN.md`; the relevant existing
  AVIF test diff was read only to check the SDR exact-pixel constraint.
- Independent double-precision evaluation gives `nitsToPq(0) = c1^m2 =
  7.30955902578397e-7`. The EETF now normalizes input as
  `(PQ(L) - PQ(Lb)) / (PQ(Lw) - PQ(Lb))`, uses the same source span for target black/white and
  reconstruction, and retains the BT.2390 fourth-power black lift. For 1000 -> 100 nits I obtain
  `0.0000500000 / 0.1025276781 / 0.6966040470 / 0.9776737501 / 1.0` at
  `0 / 10 / 100 / 400 / 1000` nits. The source-white result is `1.0016608728` before the final
  `[0,1]` clamp, so the clamp is exercised and returns exactly `1.0`. These values agree with the
  Fix round 1 report.
- Re-ran both quoted cosmos ffmpeg commands with the installed ffmpeg:

```text
rtk "C:\Users\Roma\scoop\apps\ffmpeg-shared\current\bin\ffmpeg.exe" -hide_banner -loglevel warning -i plugins\avif\fixtures\cosmos1650_yuv444_10bpc_p3pq.avif -vf "zscale=min=chroma-derived-nc:m=gbr:pin=smpte432:p=smpte432:tin=smpte2084:t=smpte2084:npl=100,format=gbrpf32le" -frames:v 1 -f rawvideo -y C:\Users\Roma\AppData\Local\Temp\pvdkit-task16-review2-cosmos-encoded.raw
rtk "C:\Users\Roma\scoop\apps\ffmpeg-shared\current\bin\ffmpeg.exe" -hide_banner -loglevel warning -i plugins\avif\fixtures\cosmos1650_yuv444_10bpc_p3pq.avif -vf "zscale=t=linear:p=bt709:m=gbr:npl=100,format=gbrpf32le" -frames:v 1 -f rawvideo -y C:\Users\Roma\AppData\Local\Temp\pvdkit-task16-review2-cosmos-linear709.raw
```

  Both exited 0 with no diagnostic output. At `(512,214)` the encoded
  G/B/R values were `0.292456100 / 0.172916000 / 0.401375500` and zscale linear-709 G/B/R was
  `0.081140000 / 0.003223710 / 0.382961100`; at `(752,128)` they were
  `0.439880200 / 0.320413500 / 0.494066200` and
  `0.482266600 / 0.085908380 / 0.952770700`. Reordering to RGB and independently applying PQ plus
  the P3-D65-to-BT.709 matrix reproduces the report's implementation values and maximum absolute
  errors `0.00003554` and `0.00016641`. Every reference linear-709 channel is in `[0,1]`, and
  `PipelineTests.cpp` now uses the literal absolute condition
  `std::abs(actual - zscale) <= 2.0F / 255.0F` rather than `doctest::Approx`.
- The architecture now distinguishes implemented CICP presentation from unapplied ICC profiles,
  unavailable user controls, and unsupported gain maps. The AVIF documents consistently state
  that identity 8-bit SDR uses BGR24/BGRA32, while presentation-required inputs at any depth and
  every source deeper than 8 bits use BGRA64; the RPGMVP exclusions are narrowed to the PNG colour
  metadata it ignores. I found no remaining scoped documentation contradiction.
- The AVIF e2e diff adds new HDR BGRA16 expectations but does not edit any pre-existing SDR
  exact-pixel expectation. The Fix round 1 changes did not alter the existing HDR quantized
  expectations either.
- Requested x64 Debug verification completed without compiler warnings. Exact commands and key
  results:

```text
$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --preset debug
-- Build files have been written to: ...\build\debug-review

$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --build --preset debug --parallel 6
exit 0; build completed with no warnings

$env:PVDKIT_BUILD_SUFFIX='-review'; rtk ctest --preset debug --output-on-failure
ctest: 17/17 passed (98.24 sec)

rtk proxy build\debug-review\tests\core\core_tests.exe --source-file=*colour*
[doctest] test cases:    22 |    22 passed | 0 failed | 57 skipped
[doctest] assertions: 69396 | 69396 passed | 0 failed |
[doctest] Status: SUCCESS!
```

- Per the review instructions, coverage, x86, ASan, lint, Release import/export checks, network
  access, and Far Manager were not run. The two temporary ffmpeg raw frames were removed after
  their samples were checked.

## Verdict

ACCEPT
