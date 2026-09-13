# Task 15 implementation report: PictureView ICC-profile experiment

Date: 2026-09-13
Branch: `master`
Base commit: `efea269`
Normal build suffix: `-t15`
Experiment build suffix: `-t15icc`

## Outcome

Task 15 is complete. Both decoders now expose their embedded ICC bytes through the shared decoder
contract, `FileSession` carries the decoder-owned span to the PVD boundary, and the default build
continues to write only the documented `pvdInfoDecode` fields. With `PVDKIT_EXPERIMENT_ICC=ON`, the
x64 shim sets undocumented decode flag 4 and writes the profile pointer and `UINT32` byte count at
the offsets observed in PictureView's bundled 2021.4.19 BMP decoder. The x86 layout remains
unverified and is never written.

The reproducible RPGMVP experiment fixtures, automated checks, documentation, and x64 experiment
DLL are ready. The remaining question—whether PictureView actually colour-manages the supplied
profile—can only be answered by Roma's visual test in Far.

## Implementation

- `src/pvd/PvdApi.hpp` now declares undocumented `PVD_IDF_ICC_PROFILE` as flag 4 and defines
  `pvdInfoDecodeEx` as the documented field sequence followed by `const BYTE *pIccProfile` and
  `UINT32 cbIccProfile`. x64 `static_assert`s pin the pointer to offset `0x20` and the count to
  offset `0x28`; the adjacent comment records the BMP.pvd provenance and the unverified x86 status.
- `core::IDecoder::iccProfile()` returns `std::span<const std::byte>`, and `pvd::DecodedPage` carries
  that span. `FileSession::decodePage()` forwards it without taking ownership. Tests require
  `ImageMeta::hasIcc == !iccProfile().empty()`.
- The AVIF adapter views `avifImage::icc`; libavif's image owns those bytes for the decoder lifetime.
- The RPGMVP adapter calls `spng_get_iccp` during open and copies its result into a decoder-owned
  `std::vector<std::byte>`. Inspection of libspng 0.7.4 confirmed that `spng_get_iccp` shallow-copies
  the context's `spng_iccp`, while `spng_ctx_free` frees `ctx->iccp.profile`, so a copy is required
  before the temporary parse context is destroyed.
- `pvd::detail::writeIccExtension()` is present and directly testable in every configuration.
  `Shim::pageDecode()` calls it only when the option is enabled, `_WIN64` is defined, and the span
  is non-empty. Default and x86 builds never access memory past the public structure.
- `PVDKIT_EXPERIMENT_ICC` defaults to `OFF` and is a `PRIVATE` compile definition of
  `pvdkit_pvd`. The experiment compile database contains exactly three occurrences, all in that
  target (`ContextHandle.cpp`, `Progress.cpp`, and `Shim.cpp`); no plugin, adapter, core, export, or
  test compilation receives it.
- Host-side e2e/sequence storage includes the observed extension bytes so the experiment DLL can be
  exercised without overrunning a public-size test structure. Tests validate flag/pointer/count
  consistency in both experiment-off and experiment-on builds.
- `docs/ARCHITECTURE.md` section 3 documents the decoder/profile lifetime contract, forwarding,
  x64 ABI extension, private option, and x86 no-write rule. No plugin README was changed.

## Fixtures

`plugins/rpgmvp/scripts/make-synthetic-fixtures.ps1` now generates a 64x64 RGB24 image whose
quadrants are exact red, green, blue, and `(128,128,128)` grey. It reads
`C:\Windows\System32\spool\drivers\color\sRGB Color Space Profile.icm`, verifies that the profile
ID/MD5 field is zero, finds the ICC tag table, and exchanges only the equal-sized `rXYZ` and
`bXYZ` payload bytes. ExifTool embeds and reports each profile before the existing inverse RPGMVP
wrapper is applied.

Generated artifacts:

| File | Bytes | SHA-256 |
|---|---:|---|
| `plugins/rpgmvp/fixtures/icc_swapped_rb_64x64.png` | 2,826 | `8973988879f5df020c35b2c6652e9b4a2924d297678362b6439abd8a681f87f7` |
| `plugins/rpgmvp/fixtures/icc_swapped_rb_64x64.rpgmvp` | 2,842 | `ff0d42169b959946d2613f22bdf4b923292ec4b589d8b7763d65eef679573de5` |
| `plugins/rpgmvp/fixtures/icc_srgb_64x64.rpgmvp` | 2,842 | `97522a68c59d4dcbeed7ec72d290661b4cf86a5edd4fbd82c2f268e85a79c700` |

The generator's `exiftool -ICC_Profile:all` output reported `acsp`, profile ID `0`, and these matrix
columns:

- patched profile: red `(0.14307, 0.06061, 0.7141)`, blue `(0.43607, 0.22249, 0.01392)`;
- unmodified profile: red `(0.43607, 0.22249, 0.01392)`, blue `(0.14307, 0.06061, 0.7141)`.

RPGMVP adapter tests check both embedded profiles are 3,144 bytes, begin with big-endian length
bytes `00 00 0C 48`, contain `acsp` at offset 36, and decode without colour conversion to exact BGR
quadrants: red `{0,0,255}`, green `{0,255,0}`, blue `{255,0,0}`, grey `{128,128,128}`. AVIF adapter
tests likewise verify its real ICC fixture and the `hasIcc` invariant. Fixture provenance and the
Windows profile's embedded copyright are recorded in `plugins/rpgmvp/fixtures/SOURCES.md`.

The generator command completed successfully and reproduced the three repository artifacts:

```powershell
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File plugins\rpgmvp\scripts\make-synthetic-fixtures.ps1
```

ExifTool emitted locale fallback warnings for its Perl runtime, but both profiled PNG inputs were
updated and both complete ICC reports were produced; the script exited 0.

## TDD evidence

The decoder-interface, forwarding, adapter, and shim tests were written before their implementation.
The first focused build was intentionally red:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t15'
rtk cmake --preset debug
rtk cmake --build --preset debug --target pvd_tests core_tests avif_adapter_tests rpgmvp_adapter_tests --parallel 6
```

It failed because the fake decoders' new `iccProfile()` methods did not yet override an interface
member and `pvd::DecodedPage` did not yet contain `iccProfile`. After the minimal implementation,
the focused suite passed 4/4.

The first complete experiment run was also usefully red: the DLL correctly returned flags 4/6 for
ICC fixtures, while older e2e assertions still required only flags 0/2. The host tests were then
made extension-sized and profile-aware without receiving the private option macro. The focused
rerun passed 3/3 and the final complete experiment run passed 19/19.

The first full lint run found two integer-to-pointer sentinels in `ShimTests.cpp` and two
PSScriptAnalyzer findings on the profile-transform function. The sentinels now use addresses of
local bytes, and profile transformation is a pure `Get-SwappedIccProfile` function. The final full
lint run is clean.

## Verification

All builds used clang-cl 19.1.5, lld-link, C++23, `/W4 /WX`, static CRT, and six build jobs. Every
build completed with zero compiler warnings.

The normal preset commands were run with `$env:PVDKIT_BUILD_SUFFIX = '-t15'`:

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

Final full-suite results:

| Preset | Result | Total time |
|---|---:|---:|
| `debug` x64 | 15/15 passed | 64.20 s |
| `release` x64 | 19/19 passed | 40.93 s |
| `debug-x86` | 15/15 passed | 64.37 s |
| `release-x86` | 19/19 passed | 31.99 s |

The release totals include the import and export policy tests for both plugins. The guard test
passed in every suite.

Final coverage commands:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t15'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '6'
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86
```

| Preset | Tests | Regions | Functions | Lines | Branches |
|---|---:|---:|---:|---:|---:|
| `coverage` x64 | 15/15 passed in 56.43 s | 789/789 (100%) | 208/208 (100%) | 1,648/1,648 (100%) | 418/418 (100%) |
| `coverage-x86` | 15/15 passed in 73.17 s | 789/789 (100%) | 208/208 (100%) | 1,648/1,648 (100%) | 418/418 (100%) |

Both coverage runs found all 20 executable production sources. Each loaded plugin produced runtime
profiles and reported 18/18 `Exports.cpp` functions executed.

Final lint command:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t15'
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6
```

Results: clang-format 0 findings in 0.7 s; clang-tidy 0 in 361.9 s; cppcheck 0 in 1.3 s;
PSScriptAnalyzer 0 in 3.9 s; BinSkim 0 in 1.1 s; `lint: clean`.

The final experiment commands were run with `$env:PVDKIT_BUILD_SUFFIX = '-t15icc'`:

```powershell
rtk cmake --preset release -DPVDKIT_EXPERIMENT_ICC=ON
rtk cmake --build --preset release --parallel 6
rtk ctest --preset release --output-on-failure
```

The configure and build completed successfully with zero compiler warnings. The final test run
passed 19/19 in 22.40 s, including guard, leak, sequence, import, export, AVIF ICC, RPGMVP ICC, and
option-enabled shim/e2e checks.

Option privacy was checked with:

```powershell
rtk rg -n "PVDKIT_EXPERIMENT_ICC" build/release-t15icc/compile_commands.json
rtk rg -c "PVDKIT_EXPERIMENT_ICC" build/release-t15icc/compile_commands.json
```

The count was exactly 3, and all matches were commands producing
`src/pvd/CMakeFiles/pvdkit_pvd.dir/{ContextHandle,Progress,Shim}.cpp.obj`.

`rtk git diff --check` exited 0 with no output. No source or task conflict was found.

## Experiment DLL

Path:

`C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-t15icc\plugins\rpgmvp\RPGMVP.pvd`

SHA-256, from `rtk certutil -hashfile build\release-t15icc\plugins\rpgmvp\RPGMVP.pvd SHA256`:

`5052d079c419ffbbe80f4a0713a53ddc49d717167c5608a5d2139215f54232e4`

Direct policy commands:

```powershell
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-imports.ps1 -Path build\release-t15icc\plugins\rpgmvp\RPGMVP.pvd
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-exports.ps1 -Path build\release-t15icc\plugins\rpgmvp\RPGMVP.pvd
```

The file is `COFF-x86-64`. Its import table contains exactly one module, `KERNEL32.dll`. Its export
table contains exactly these eight bare names:

1. `pvdExit`
2. `pvdFileClose`
3. `pvdFileOpen`
4. `pvdInit`
5. `pvdPageDecode`
6. `pvdPageFree`
7. `pvdPageInfo`
8. `pvdPluginInfo`

Both policy scripts exited 0.

## How to run the experiment in Far

Use the x64 experiment DLL above with x64 Far Manager. Install or select it through the normal
local PictureView plugin procedure, keeping the normal and experiment `RPGMVP.pvd` files separate
and loading only one at a time so the observation is attributable.

1. Open `plugins\rpgmvp\fixtures\icc_swapped_rb_64x64.png` through the GDI+ path. This is the
   no-colour-management reference: top-left red, top-right green, bottom-left blue, and bottom-right
   mid-grey.
2. With the experiment DLL active, open
   `plugins\rpgmvp\fixtures\icc_swapped_rb_64x64.rpgmvp` in PictureView.
   - If the red and blue quadrants are swapped relative to the PNG, PictureView applies the ICC
     profile supplied through flag 4 and the extension fields.
   - If it is visually identical to the PNG, PictureView ignores the supplied ICC profile.
3. Open `plugins\rpgmvp\fixtures\icc_srgb_64x64.rpgmvp`. It must look like the PNG reference in
   either outcome; a difference here means the control failed and the swapped-profile result is not
   interpretable.
4. Record the Far/PictureView versions and the observed result, then restore the normal DLL.

The automated adapter tests establish that both RPGMVP files deliver their intended profile bytes
and identical unmodified pixels to the shim. Therefore this visual difference isolates host-side
ICC handling rather than decoder colour conversion.

## What was not done

Far Manager was not run and `C:\Tools\FarManager` was not touched, so the host's ICC behavior remains
for Roma to observe. The x86 extension layout was not inferred or exercised; x86 builds prove only
that normal decoding still builds and passes while the shim does not write the extension. No network
operation, compiler/dependency installation, commit, push, worktree, staging operation, or package
archive was performed.

## Fix round 1

Date: 2026-09-13

All four review findings are fixed. The working tree remains uncommitted at base commit
`efea2696641733bd9e33a83ad265847e5833b2ee`.

### Changes

- `pvd::detail::iccExtensionSize(std::size_t)` now accepts `UINT32_MAX` exactly and returns
  `std::nullopt` above it. `writeIccExtension` writes the flag, pointer, and count only through a
  successful result, so an oversized profile leaves all three absent/untouched. The boundary test
  exercises both `UINT32_MAX` and `UINT32_MAX + 1` on x64. The existing default-build canary still
  seeds the extension fields with sentinels and proves all seven assertions, including that the
  pointer and size remain untouched.
- The e2e targets receive a separate test-only `PVDKIT_E2E_EXPECT_ICC_EXPERIMENT` value; the private
  production definition remains confined to `pvdkit_pvd`. General e2e checks now require extension
  presence for profiled fixtures in an enabled x64 build and require absence otherwise. Dedicated
  enabled-only tests load the real AVIF and RPGMVP DLLs, require flag 4, a non-null pointer and exact
  profile size, check `acsp` at offset 36 plus a pinned non-header range, retain the pointer, call
  `pvdPageFree`, decode the same page again, and re-check the original retained bytes before
  `pvdFileClose`. There is no result-dependent skip path. The AVIF profile is 596 bytes and pins its
  16-byte tag-table prefix at offset 128; the generated RPGMVP profile is 2,560 bytes and pins the
  swapped red-colourant payload at offset 448.
- `make-synthetic-fixtures.ps1` defaults `-ExifTool` to `exiftool.exe`, resolves it with
  `Get-Command`, and retains the override. It now resolves `-Ffmpeg` the same way.
- The script no longer reads the Windows/HP profile. Pure PowerShell constructs a deterministic ICC
  v2.1 display profile with zero CMM/platform/flags/device/creator/profile-ID fields, class `mntr`,
  data `RGB `, PCS `XYZ `, fixed generation date, `acsp`, rendering intent 0, D50 header illuminant,
  `desc`, MIT `cprt`, D50 `wtpt`, the requested Bradford-adapted D50 sRGB matrix colourants, and one
  shared 1,024-entry sampled piecewise-sRGB `curv` payload for `rTRC`/`gTRC`/`bTRC`. The experiment
  variant swaps only the `rXYZ` and `bXYZ` payloads. `SOURCES.md` records that these are generated,
  MIT-licensed repository bytes with no third-party profile content, and documents why the sampled
  sRGB curve was chosen over gamma 2.2.

### TDD evidence

The new size-boundary and integration expectations were added before the narrowing implementation.
The first focused build was intentionally red:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t15'
rtk cmake --preset debug
rtk cmake --build --preset debug --target pvd_tests avif_e2e_tests rpgmvp_e2e_tests --parallel 6
```

Configure succeeded; compilation failed with three errors at `ShimTests.cpp:216-219` because
`pvdkit::pvd::detail::iccExtensionSize` did not exist. After the minimal helper/writer change, the
same focused targets built without warnings. The enabled focused build and test command was:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t15icc'
rtk cmake --preset debug -DPVDKIT_EXPERIMENT_ICC=ON
rtk cmake --build --preset debug --target pvd_tests avif_adapter_tests rpgmvp_adapter_tests avif_e2e_tests rpgmvp_e2e_tests --parallel 6
rtk ctest --test-dir build\debug-t15icc --output-on-failure -R "pvd_tests|avif_adapter_tests|rpgmvp_adapter_tests|avif_e2e_tests|rpgmvp_e2e_tests"
```

The first combined build call was interrupted by the command runner after 30 seconds at 18/68;
rerunning the build resumed cleanly, and the focused suite passed 5/5 in 3.57 seconds. Direct proof
of the new cases:

```powershell
rtk proxy .\build\debug-t15\tests\pvd\pvd_tests.exe "--test-case=ICC extension sizes narrow exactly through the UINT32 boundary"
rtk proxy .\build\debug-t15\tests\pvd\pvd_tests.exe "--test-case=pageDecode writes the ICC extension only in the enabled x64 experiment build"
rtk proxy .\build\debug-t15icc\plugins\avif\tests\e2e\avif_e2e_tests.exe "--test-case=the enabled x64 DLL keeps the real AVIF ICC profile alive until file close"
rtk proxy .\build\debug-t15icc\plugins\rpgmvp\tests\e2e\rpgmvp_e2e_tests.exe "--test-case=the enabled x64 DLL keeps the real RPGMVP ICC profile alive until file close"
```

Results were respectively 1/1 cases and 3/3 assertions, 1/1 and 7/7, 1/1 and 29/29, and 1/1 and
29/29.

### Regenerated fixtures

The final generator was run twice to prove deterministic output:

```powershell
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File plugins\rpgmvp\scripts\make-synthetic-fixtures.ps1
```

Both runs exited 0 and produced the same hashes. ExifTool emitted only its existing locale-fallback
warning. The final artifacts are:

| File | Bytes | SHA-256 |
|---|---:|---|
| `plugins/rpgmvp/fixtures/icc_swapped_rb_64x64.png` | 2,586 | `3e4dda89110f82feb5a0d9dfa536a5015aaa84619e82219044f13976d309596d` |
| `plugins/rpgmvp/fixtures/icc_swapped_rb_64x64.rpgmvp` | 2,602 | `79a12dfb9739f19f121d5993cce1c80a2c20a3c410f512298dcf9959ffa04641` |
| `plugins/rpgmvp/fixtures/icc_srgb_64x64.rpgmvp` | 2,602 | `75c3136c3f6966e9aff2f56746318b6cacf929b2cff454fc64fcb4528e91113c` |

The encrypted fixtures were decrypted to `%TEMP%` and re-verified with the requested quoted tag:

```powershell
rtk exiftool "-ICC_Profile:all" C:\Users\Roma\AppData\Local\Temp\pvdkit-t15-swapped.png
rtk exiftool "-ICC_Profile:all" C:\Users\Roma\AppData\Local\Temp\pvdkit-t15-control.png
```

Both reports showed v2.1, display/RGB/XYZ, `acsp`, D50 `(0.9642, 1, 0.82491)`, profile ID 0,
`pvdkit sRGB-equivalent test profile`, and `Copyright (c) 2026 Roman Kharitonov, MIT`. The swapped
report showed red `(0.1431, 0.06059, 0.7141)` and blue `(0.4361, 0.2225, 0.0139)`; the control
showed those columns in the opposite positions. Each TRC was reported as the same 2,060-byte binary
curve payload. Temporary inspection files were removed afterward.

### Final verification

Default x64 Debug:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t15'
rtk cmake --preset debug
rtk cmake --build --preset debug --parallel 6
rtk ctest --preset debug --output-on-failure
```

Configure/build exited 0 with clang-cl 19.1.5 and no warnings. CTest passed 15/15 in 49.32 seconds;
`guard_tests` passed.

Default x64 coverage:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t15'
$env:CMAKE_BUILD_PARALLEL_LEVEL='6'
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
```

CTest passed 15/15 in 58.92 seconds. Both plugin-profile checks reported 18/18 `Exports.cpp`
functions executed, and source completeness found all 20 executable production files. The gate
reported 794/794 regions, 210/210 functions, 1,663/1,663 lines, and 420/420 branches: 100% lines and
100% branches.

Enabled x64 Debug:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t15icc'
rtk cmake --preset debug -DPVDKIT_EXPERIMENT_ICC=ON
rtk cmake --build --preset debug --parallel 6
rtk ctest --preset debug --output-on-failure
```

Configure/build exited 0 with no compiler warnings; the full suite passed 15/15 in 49.92 seconds.

Enabled x64 Release:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t15icc'
rtk cmake --preset release -DPVDKIT_EXPERIMENT_ICC=ON
rtk cmake --build --preset release --parallel 6
rtk ctest --preset release --output-on-failure
```

Configure/build exited 0 with no compiler warnings; CTest passed 19/19 in 22.63 seconds, including
both plugins' import and export gates. `PVDKIT_EXPERIMENT_ICC` still occurs exactly three times in
`build/release-t15icc/compile_commands.json`, solely for
`pvdkit_pvd`'s `ContextHandle.cpp`, `Progress.cpp`, and `Shim.cpp`.

The rebuilt experiment DLL hashes are:

| DLL | SHA-256 |
|---|---|
| `build/release-t15icc/plugins/rpgmvp/RPGMVP.pvd` | `efcf13917376c2fc0552a1299b5490a3464e4d00f4515bcde4de11c3976bef32` |
| `build/release-t15icc/plugins/avif/AVIF.pvd` | `0675339a51023044893c68609272663e428761b6faa9e38b5a5bedc2a29b53e4` |

Direct policy commands were run for each DLL:

```powershell
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-imports.ps1 -Path build\release-t15icc\plugins\rpgmvp\RPGMVP.pvd
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-exports.ps1 -Path build\release-t15icc\plugins\rpgmvp\RPGMVP.pvd
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-imports.ps1 -Path build\release-t15icc\plugins\avif\AVIF.pvd
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-exports.ps1 -Path build\release-t15icc\plugins\avif\AVIF.pvd
```

Both files are `COFF-x86-64`; each import table contains exactly `KERNEL32.dll`, and each export
table contains exactly the eight bare names `pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`,
`pvdPageDecode`, `pvdPageFree`, `pvdPageInfo`, and `pvdPluginInfo`.

Final x64 lint used the ICC-enabled compile database so the enabled-only integration code was
analyzed:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t15icc'
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6
```

Results: clang-format 0 findings in 0.5 seconds; clang-tidy 0 in 410.8 seconds; cppcheck 0 in 1.2
seconds; PSScriptAnalyzer 0 in 4.0 seconds; BinSkim 0 in 1.0 seconds; `lint: clean`. An earlier
focused format/PSScriptAnalyzer run found eight `PSUseShouldProcessForStateChangingFunctions`
warnings on pure profile-construction helpers; they were renamed with non-state-changing approved
verbs, and the focused rerun was clean before the full gate.

`rtk git diff --check` exited 0. No x86, ASan, package, Far Manager, network, installation, commit,
push, worktree, or staging operation was performed in this fix round; the requested verification
scope was x64.
