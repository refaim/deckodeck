# Task 14 implementation report: deep output by default

Date: 2026-09-13  
Branch: `master`  
Base commit: `b363403`  
Build suffix: `-t14`

## Outcome

Task 14 is complete. AVIF and RPGMVP are version 1.1.0, and both production compositions now set
`DecoderOptions::deepOutput = true` unconditionally. Sources deeper than eight bits are delivered
to PictureView as tightly packed BGRA64: little-endian unsigned 16-bit B, G, R and straight-alpha
samples. Eight-bit-and-shallower sources retain their existing byte-identical BGR24/BGRA32 paths.

The RPGMVP experiment switch, production/test compile definitions, conditional expectations and
comment suffix were removed. AVIF now hands BGRA64 destinations directly to libavif with
`depth = 16`, `AVIF_RGB_FORMAT_BGRA` and `alphaPremultiplied = AVIF_FALSE`. The final x64/x86
Debug and Release matrices, x64/x86 100% coverage gates, AddressSanitizer, guard, both full lint
runs, and all four Release DLL import/export checks pass.

## Implementation

- `plugins/avif/src/adapters/avif/Decoder.cpp` accepts BGRA64 destinations as eight bytes per
  pixel. `rgbTarget()` selects 16-bit BGRA only for BGRA64; the existing 8-bit BGR24 and BGRA32
  setup is unchanged.
- Both `DefaultPlugin.cpp` compositions set `deepOutput` to true. Shared `FileSession` therefore
  selects BGRA64 for source metadata depths above eight and keeps BGR24/BGRA32 for shallower input.
- RPGMVP no longer has `PVDKIT_RPGMVP_DEEP_OUTPUT`, `PVDKIT_TEST_DEEP_OUTPUT`, a generated
  experiment comment, or configuration-dependent tests. Its DLL e2e test asserts only:
  - `rgba16_60x20_par.rpgmvp`: 64 bpp, pitch 480, flags 2, first pixel
    `FF FF FF FF FF FF 00 00`.
  - `rgb16_88x4a.rpgmvp`: 64 bpp, pitch 704, flags 0, first pixel
    `F4 3B 60 60 96 1B FF FF`.
- AVIF DLL e2e expectations select 64-bit output for every accepted fixture whose source page
  depth exceeds eight. Existing exact 8-bit synthetic pixel checks remain unchanged and pass.
- Accepted 10-bit `clop_irot_imor.avif` proves the transformed deep path through the DLL: coded
  12x34 becomes rotated 34x12, decoded as 64 bpp at pitch 272 with alpha flag 2. The direct
  `clap_irot_imir_non_essential.avif` case cannot be positive because libavif deliberately rejects
  known transform properties marked non-essential; its existing adapter rejection test remains.
- Both identities, test literals and VERSIONINFO resources are 1.1.0. README histories, package
  docs, plugin designs and `docs/ARCHITECTURE.md` describe deep output as the default. Obsolete
  8-bit-only limitations were removed.
- Architecture documentation records Roma's 2026-09-13 host result: PictureView accepts
  `nBPP == 64` and renders it like the 8-bit path, while 10-bit output, spatial dithering,
  gamma-correct scaling and auto-levels can use the preserved precision.

## Verified libavif scaling rule

The installed, locally cached libavif 1.4.2 source was inspected at:

```text
C:\Users\Roma\scoop\apps\vcpkg\current\buildtrees\libavif\src\v1.4.2-9f5b6e68dd.clean\include\avif\avif.h
C:\Users\Roma\scoop\apps\vcpkg\current\buildtrees\libavif\src\v1.4.2-9f5b6e68dd.clean\src\reformat.c
C:\Users\Roma\scoop\apps\vcpkg\current\buildtrees\libavif\src\v1.4.2-9f5b6e68dd.clean\src\alpha.c
```

`avif.h:934-936` says YUV-to-RGB performs depth and limited/full-range conversion and RGB buffers
are always full range. `avif.h:1000-1001` permits depth 16 and requires >8-bit pixels to be native
`uint16_t` samples. `reformat.c:573-599` normalizes Y and UV using source range and bias.
`reformat.c:988-1021` then applies the color matrix, clamps channels to [0,1], and stores
`uint16_t(0.5f + channel * 65535)`.

The exact rule is a full-range rescale with rounding, not a bit shift. A direct full-range
component at source depth `d` maps as:

```text
round(value * 65535 / ((1 << d) - 1))
```

Limited-range YUV is normalized using its legal range before color conversion. The
`tenbit_444.avif` test uses `round(clamp((16*x - 64) / 876, 0, 1) * 65535)` for the neutral
ramp and verifies opaque `0xFFFF` alpha. `alpha.c:39-42,83-98` uses the same full-range ratio for
a real alpha plane; `alpha.c:9-20` fills missing >8-bit alpha with the destination maximum.

The 12-bit assertion distinguishes the rules concretely. Frame 0 pixel (0,0) of
`colors-animated-12bpc-keyframes-0-2-3.avif` has source alpha 4094. BGRA64 alpha is 65519,
`round(4094 * 65535 / 4095)`; a left shift would be 65504. The adapter test passed 1/1 with
16 assertions.

## TDD evidence

AVIF BGRA64 adapter/e2e assertions, unconditional RPGMVP expectations and 1.1.0 identity
assertions were written before production/CMake changes. The focused red command was:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t14'
rtk ctest --preset release -R '^(avif|rpgmvp)_(adapter|e2e|sequence)_tests$' --output-on-failure
```

It returned 0/6: the focused target build had not built `avif_sequence_tests`, and all five built
changed suites failed against the old implementation. Meaningful red results were AVIF adapter
33/38 cases, AVIF e2e 11/16, RPGMVP adapter 15/17, RPGMVP e2e 6/9 and RPGMVP sequence 7/8.
Failures showed old 1.0.1 identities, AVIF's BGRA64 rejection, shallow composition output and
experiment-conditioned RPGMVP expectations.

After the minimal production/CMake changes, the explicit focused rerun passed 5/5 in 2.73 s:

```powershell
rtk ctest --test-dir build\release-t14 -R '^(avif_adapter_tests|avif_e2e_tests|rpgmvp_adapter_tests|rpgmvp_e2e_tests|rpgmvp_sequence_tests)$' --output-on-failure
```

## Informational timing

The Release DLL e2e test times five `pvdPageDecode` calls after one warm-up for
`cosmos1650_yuv444_10bpc_p3pq.avif` and has no threshold.

```powershell
rtk proxy .\build\release-t14\plugins\avif\tests\e2e\avif_e2e_tests.exe --test-case="cosmos deep decode timing*" --success=true --no-version=true
```

| Path | Output | Mean |
|---|---:|---:|
| Before adapter/default change | 24 bpp | 1,837 us |
| Final implementation | 64 bpp | 2,297 us |

The observed final sample was 460 us, about 25.0%, slower. This is a short informational sample,
not a benchmark or gate. An earlier post-change sample was 2,149 us, showing normal run-to-run
noise.

## Verification

All executable invocations used the required `rtk` wrapper. Builds were sequential, used
`--parallel 6`, stayed in `-t14` build directories, and emitted zero compiler warnings.

### Debug and Release matrix

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t14'
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

| Preset | Result | Time | Included gates |
|---|---:|---:|---|
| `debug` | 15/15 passed | 65.15 s | guard, e2e, sequence and leak tests |
| `release` | 19/19 passed | 32.77 s | Debug gates plus import/export checks |
| `debug-x86` | 15/15 passed | 55.43 s | guard, e2e, sequence and leak tests |
| `release-x86` | 19/19 passed | 30.15 s | Debug gates plus import/export checks |

One earlier final-source Debug x64 run reported 14/15 because `avif_leak_tests` observed a
transient +624 KiB private-working-set delta while reporting zero heap blocks, heap bytes, handles
and mapped views. Its isolated rerun passed 1/1 in 37.68 s, then the full rerun above passed 15/15.
The final explicit guard rerun passed 1/1 in 2.31 s.

### Coverage

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t14'
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86
```

Both runs passed all 15 tests, found all 20 executable production source files, and verified 18/18
`Exports.cpp` functions through separate profiles for both plugins.

| Preset | Tests | Regions | Functions | Lines | Branches |
|---|---:|---:|---:|---:|---:|
| `coverage` | 15/15 in 70.90 s | 777/777 | 201/201 | 1600/1600 (100%) | 416/416 (100%) |
| `coverage-x86` | 15/15 in 84.52 s | 777/777 | 201/201 | 1600/1600 (100%) | 416/416 (100%) |

### AddressSanitizer

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t14'
rtk cmake --preset asan
rtk cmake --build --preset asan --parallel 6
rtk ctest --preset asan --output-on-failure
```

Final result: 15/15 passed in 58.80 s. Slowest were `avif_leak_tests` 32.67 s,
`rpgmvp_leak_tests` 10.98 s and `guard_tests` 10.51 s. An earlier post-rebuild full run tripped
the leak-gate self-test's transient mapped-view accounting case; its isolated rerun passed 1/1, a
full test-directory rerun passed 15/15, then the exact preset invocation above passed 15/15.

### Lint

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t14'
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -BuildDir build\debug-x86-t14 -ReleaseDir build\release-x86-t14 -Jobs 6
```

The first x64 lint pass found four test-only clang-tidy diagnostics: one implicit widening and
three floating-to-integer rounding conversions. They were fixed with explicit widening and
`std::lround`. A later format check found one layout change, applied before both full clean runs.

| Tool | x64 | x86 |
|---|---:|---:|
| clang-format | 0 findings (1.3 s) | 0 findings (0.5 s) |
| clang-tidy | 0 findings (317.5 s) | 0 findings (282.3 s) |
| cppcheck | 0 findings (1.2 s) | 0 findings (1.1 s) |
| PSScriptAnalyzer | 0 findings (3.5 s) | 0 findings (3.6 s) |
| BinSkim | 0 findings (0.9 s) | 0 findings (1.0 s) |

Both scripts ended with `lint: clean`.

### Release DLLs, versions and ABI

For every DLL, direct `scripts\check-imports.ps1` and `scripts\check-exports.ps1` calls exited 0.
`llvm-readobj` identified x64 as `COFF-x86-64` and x86 as `COFF-i386`. Every image imports only
`KERNEL32.dll` and exports exactly `pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`,
`pvdPageDecode`, `pvdPageFree`, `pvdPageInfo` and `pvdPluginInfo`.

```powershell
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-imports.ps1 -Path <dll>
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-exports.ps1 -Path <dll>
rtk proxy powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '<dll>').Hash"
rtk proxy powershell -NoProfile -Command "[Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path -LiteralPath '<dll>')) | Select-Object FileVersion,ProductVersion"
```

| Architecture/plugin | Path | File/Product version | SHA-256 |
|---|---|---:|---|
| x64 AVIF | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-t14\plugins\avif\AVIF.pvd` | 1.1.0 / 1.1.0 | `C29ABCB56ADC511EDD9DCECBB4BB0783DDE052C42D195FC5BC1A5588F20C638B` |
| x64 RPGMVP | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-t14\plugins\rpgmvp\RPGMVP.pvd` | 1.1.0 / 1.1.0 | `F204C89DED8A5B231883A9273FE944AEAF5677DD7752CD7760948DEF2C9B8686` |
| x86 AVIF | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-x86-t14\plugins\avif\AVIF.pvd` | 1.1.0 / 1.1.0 | `DA5CFFFCF921926748D2B8448A8B890B5CD402FC2C2C3A9C71959CA98518A48C` |
| x86 RPGMVP | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-x86-t14\plugins\rpgmvp\RPGMVP.pvd` | 1.1.0 / 1.1.0 | `6202693055D98331A249BB607F3E2C9042B15417E6491B7A810A2D10AB999ED7` |

### Final source audit

```powershell
rtk rg -n -i "PVDKIT_RPGMVP_DEEP_OUTPUT|PVDKIT_TEST_DEEP_OUTPUT|experiment" . --glob "!build/**" --glob "!docs/tasks/**"
rtk git diff --check
rtk ctest --test-dir build\debug-t14 -R ^guard_tests$ --output-on-failure
rtk git status --short
rtk git diff --stat
```

The obsolete-symbol/word search had no matches outside task-history docs (exit 1).
`git diff --check` had no output and exited 0. Guard passed. `git status --short` lists 28 modified
tracked files plus this untracked report; nothing is staged. The tracked `git diff --stat` is 28
files, 310 insertions and 121 deletions; as an untracked file, this report is not included in that
statistic.

## What was not done

Far Manager and `C:\Tools\FarManager` were not run or touched. No network operation, dependency or
compiler installation, third-party codec source modification, source commit, push, worktree,
staging operation or package archive was performed. No performance threshold was introduced.
