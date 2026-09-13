## Substantive findings

1. `src/pvd/Shim.cpp:19` — `profile.size()` is narrowed from `std::size_t` to `UINT32` with an unchecked `static_cast`. The AVIF adapter exposes libavif's `size_t` ICC payload without a 32-bit bound, so a hostile x64 file can make the extension advertise a truncated byte count while setting `PVD_IDF_ICC_PROFILE`. This is an ABI-output bug. Check the size before setting either extension field or flag (omit the extension or reject the profile when it exceeds `UINT32_MAX`) and cover the boundary through a small, directly testable narrowing helper.

2. `plugins/avif/tests/e2e/E2eTests.cpp:80` — the e2e helper derives `exposesProfile` from the result and checks `profileAvailable` only when that result is already true; therefore an ICC fixture with no extension flag/pointer/count still passes even in the enabled build. The RPGMVP composition test at `plugins/rpgmvp/tests/e2e/CompositionTests.cpp:69` requires only non-null/non-zero values, and no test dereferences the host pointer after `pvdPageFree` or after another decode. Thus the central lifetime guarantee and the report's claim that the intended profile bytes reach the shim are not tested; a dangling or wrong pointer could remain green. In an enabled x64 integration test for each real adapter, require flag 4, exact/known profile bytes (including `acsp`), retain the returned pointer, free the page, perform another decode, and verify the original ICC bytes again before `pvdFileClose`. Keep the existing default-build canary test for the no-write case.

3. `plugins/rpgmvp/scripts/make-synthetic-fixtures.ps1:4` — the committed generator defaults ExifTool to `C:\Users\Roma\scoop\shims\exiftool.exe`. The no-argument reproduction command in the implementation report consequently works only for that account layout and fails from a clean clone on another machine, violating the repository's script reproducibility requirement. Default to `exiftool.exe`/`Get-Command exiftool` as is already done for ffmpeg, while retaining the parameter override.

4. `plugins/rpgmvp/fixtures/SOURCES.md:39` — the new fixtures embed the complete 3,144-byte Windows/HP sRGB profile, but provenance records only its filesystem location and copyright string, not a redistribution licence or permission. The extracted control profile is byte-identical to the Windows system file, so this is third-party content rather than merely generated test data. Record an applicable redistribution grant and any required notice, or replace it with an explicitly redistributable profile and update the fixtures/evidence.

## Nits

None.

## Verified

- `rtk git status --short`, `rtk git diff --stat`, and `rtk git diff --name-only` identified 28 modified tracked files, the Task 15 report, and the three new ICC fixtures. `rtk git rev-parse HEAD` returned `efea2696641733bd9e33a83ad265847e5833b2ee`; `rtk git diff --check` exited 0 with no output.
- Default x64:

  ```powershell
  $env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --preset debug
  $env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --build --preset debug --parallel 6
  $env:PVDKIT_BUILD_SUFFIX='-review'; rtk ctest --preset debug --output-on-failure
  ```

  Configure and build exited 0 with clang-cl 19.1.5 and no compiler warnings. CTest: `15/15 passed (51.56 sec)`; slowest tests were `avif_leak_tests 27.17 sec`, `rpgmvp_leak_tests 15.57 sec`, and `guard_tests 2.46 sec`.
- Coverage:

  ```powershell
  $env:PVDKIT_BUILD_SUFFIX='-review'; $env:CMAKE_BUILD_PARALLEL_LEVEL='6'; rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
  ```

  CTest passed 15/15 in 57.62 s; both plugin profile checks reported `18/18 Exports.cpp functions executed`; source completeness found all 20 executable production sources. Key rows were:

  | File | Lines | Branches |
  |---|---:|---:|
  | `plugins/avif/src/adapters/avif/Decoder.cpp` | 259/259 (100%) | 56/56 (100%) |
  | `plugins/rpgmvp/src/adapters/spng/Decoder.cpp` | 375/375 (100%) | 88/88 (100%) |
  | `src/core/FileSession.cpp` | 84/84 (100%) | 38/38 (100%) |
  | `src/core/IDecoder.hpp` | 2/2 (100%) | no branches |
  | `src/pvd/Shim.cpp` | 267/267 (100%) | 46/46 (100%) |
  | Total | 1,648/1,648 (100%) | 418/418 (100%) |

  The gate printed `Coverage gate passed: lines 100%, branches 100%`.
- Enabled x64:

  ```powershell
  $env:PVDKIT_BUILD_SUFFIX='-review-icc'; rtk cmake --preset debug -DPVDKIT_EXPERIMENT_ICC=ON
  $env:PVDKIT_BUILD_SUFFIX='-review-icc'; rtk cmake --build --preset debug --parallel 6
  $env:PVDKIT_BUILD_SUFFIX='-review-icc'; rtk ctest --preset debug --output-on-failure
  ```

  Configure and build exited 0 with no compiler warnings; CTest passed 15/15 in 51.25 s. Configure emitted one non-fatal vcpkg binary-cache submission warning (`Access is denied`), unrelated to compilation or tests.
- Compile-definition privacy:

  ```powershell
  rtk proxy powershell -NoProfile -Command "(Select-String -LiteralPath 'build/debug-review/compile_commands.json' -Pattern 'PVDKIT_EXPERIMENT_ICC').Count"
  rtk proxy powershell -NoProfile -Command "(Select-String -LiteralPath 'build/debug-review-icc/compile_commands.json' -Pattern 'PVDKIT_EXPERIMENT_ICC').Count"
  rtk rg -n "PVDKIT_EXPERIMENT_ICC" build/debug-review-icc/compile_commands.json
  ```

  Counts were 0 and 3. The three enabled occurrences compile only `pvdkit_pvd`'s `ContextHandle.cpp`, `Progress.cpp`, and `Shim.cpp`; no other target receives the definition.
- The configuration-sensitive shim case was run directly in both binaries:

  ```powershell
  rtk proxy .\build\debug-review\tests\pvd\pvd_tests.exe "--test-case=pageDecode writes the ICC extension only in the enabled x64 experiment build"
  rtk proxy .\build\debug-review-icc\tests\pvd\pvd_tests.exe "--test-case=pageDecode writes the ICC extension only in the enabled x64 experiment build"
  ```

  Each run passed 1/1 test case and 7/7 assertions. Static inspection confirms the actual extension call is under `#if defined(PVDKIT_EXPERIMENT_ICC) && defined(_WIN64)`; the default build contains no definition. No x86 build was run, as requested.
- Fixture verification used the committed decrypt script to temporary files, `rtk certutil -hashfile ... SHA256`, `rtk exiftool -G1 -s -ICC_Profile:all ...`, and ExifTool extraction followed by a byte-for-byte PowerShell comparison. The swapped RPGMVP decrypted exactly to the committed PNG (`8973988879f5df020c35b2c6652e9b4a2924d297678362b6439abd8a681f87f7`). Repository fixture hashes matched the report. Both extracted profiles were 3,144 bytes with header length 3,144 and signature `acsp`; `rXYZ` and `bXYZ` were 20-byte tags at offsets 536 and 576. Their payloads were exactly exchanged, with 12 differing bytes and zero differences outside those payloads. ExifTool reported swapped red `(0.14307, 0.06061, 0.7141)` / blue `(0.43607, 0.22249, 0.01392)` and the inverse for the control. The control ICC SHA-256, `2b3aa1645779a9e634744faf9b01e9102b0c9b88fd6deced7934df86b949af7e`, exactly matched the Windows system profile. Temporary inspection files were removed afterward.
- Local libspng 0.7.4 source inspection confirmed `spng_get_iccp` performs `*iccp = ctx->iccp`, while `spng_ctx_free` frees `ctx->iccp.profile`; copying into `Decoder::iccProfile_` is the correct lifetime decision. Local libavif 1.4.2 source inspection confirmed the image ICC allocation is populated during parse and is not replaced by `avifDecoderNthImage`; the current adapter spans therefore remain stable for the decoder/session lifetime.
- Release, x86, ASan, lint, and network operations were not run, per the review instructions.

## Verdict

REJECT

## Round 2

## Substantive findings

## Nits

None.

## Verified

- `src/pvd/Shim.cpp` now checks `profile.size()` before any extension write. `iccExtensionSize`
  accepts `UINT32_MAX` and rejects `UINT32_MAX + 1`; `writeIccExtension` changes the pointer, count,
  and flag only inside the successful `std::optional::transform`, so an oversized span leaves all
  three untouched. `tests/pvd/ShimTests.cpp` pins both boundary values through the directly testable
  helper, and the existing canary continues to prove that disabled builds preserve seeded extension
  storage.
- Both enabled-only integration cases call `loadInitializedPlugin()`. The shared host driver resolves
  `PVDKIT_PLUGIN_PATH`, calls `LoadLibraryW`, resolves all eight exports with `GetProcAddress`, and is
  linked without either plugin's static libraries. Neither case has a runtime skip path. Each requires
  flag 4, a non-null pointer, the exact profile size, `acsp`, and fixture-specific bytes; retains the
  first pointer; calls `pvdPageFree`; decodes again; and rechecks the retained bytes before
  `pvdFileClose`.
- A review-only PowerShell parser (removed after use) reconstructed the PNG bytes from both RPGMVP
  fixtures, decompressed each `iCCP` chunk without ExifTool, and parsed the ICC bytes directly. All
  three profiles are 2,560-byte ICC v2.1 display profiles with `mntr`, `RGB `, `XYZ `, `acsp`, the
  fixed 2026-09-13 timestamp, intent 0, zero platform/flags/device/creator/profile-ID fields, and D50
  `(0.964203, 1.000000, 0.824905)`. The nine tag ranges are in bounds; payload offsets
  `240, 368, 420, 440, 460, 480, 500` are 4-byte aligned; unpadded sizes are
  `126, 49, 20, 20, 20, 20, 2060`, with zero padding to the next aligned offset. `desc` has a valid
  ICC-v2 `desc` payload, `cprt` is `text`, every colourant is a 20-byte `XYZ ` tag containing three
  s15Fixed16 values, and the shared `curv` has count 1,024 and exact size `12 + 2 * 1024 = 2060`,
  monotonic samples from 0 to 65,535. The plain and wrapped swapped profiles are byte-identical; the
  control and swapped profiles differ in 12 bytes, all inside exchanged `rXYZ`/`bXYZ` payloads.
- `plugins/rpgmvp/scripts/make-synthetic-fixtures.ps1` defaults to `exiftool.exe` and resolves both
  ExifTool and ffmpeg with `Get-Command`, while retaining parameter overrides. `SOURCES.md` accurately
  describes the generated profile, shared sampled curve, swapped colourants, and MIT copyright.
  Direct parsing found `pvdkit sRGB-equivalent test profile` and
  `Copyright (c) 2026 Roman Kharitonov, MIT`; the raw fixture grep for HP/Microsoft strings returned
  no matches. Fixture SHA-256 values match the fix report: `3e4dda89...309596d`,
  `79a12dfb...fa04641`, and `75c3136c...91113c`.
- Compile-database inspection found exactly three `PVDKIT_EXPERIMENT_ICC` occurrences, all on
  `pvdkit_pvd` (`ContextHandle.cpp`, `Progress.cpp`, `Shim.cpp`). The separate
  `PVDKIT_E2E_EXPECT_ICC_EXPERIMENT=1` appeared ten times, exclusively on the five translation units
  of each plugin's e2e executable; it is absent from production targets.
- Requested build and full test commands:

  ```powershell
  $env:PVDKIT_BUILD_SUFFIX='-review-icc'; rtk cmake --preset debug -DPVDKIT_EXPERIMENT_ICC=ON
  $env:PVDKIT_BUILD_SUFFIX='-review-icc'; rtk cmake --build --preset debug --parallel 6
  $env:PVDKIT_BUILD_SUFFIX='-review-icc'; rtk ctest --preset debug --output-on-failure
  ```

  Configure and build exited 0 with no compiler warnings. CTest summary: `15/15 passed (59.10 sec)`;
  the slowest tests were `avif_leak_tests 34.71 sec`, `rpgmvp_leak_tests 16.01 sec`, and
  `guard_tests 2.51 sec`.
- The two requested direct commands were:

  ```powershell
  rtk proxy .\build\debug-review-icc\plugins\avif\tests\e2e\avif_e2e_tests.exe "--test-case=the enabled x64 DLL keeps the real AVIF ICC profile alive until file close"
  rtk proxy .\build\debug-review-icc\plugins\rpgmvp\tests\e2e\rpgmvp_e2e_tests.exe "--test-case=the enabled x64 DLL keeps the real RPGMVP ICC profile alive until file close"
  ```

  Each exited 0 with `1/1` test case and `29/29` assertions passed (AVIF: 16 other cases skipped;
  RPGMVP: 9 skipped).
- Supporting read-only commands included `rtk rg -n "PVDKIT_EXPERIMENT_ICC" ...`,
  `rtk rg -n "PVDKIT_E2E_EXPECT_ICC_EXPERIMENT" ...`, `rtk rg -n "SKIP|skip|..." ...`, the three
  `rtk certutil -hashfile ... SHA256` calls, and
  `rtk rg -a -n -i "Hewlett|Microsoft|HP.*copyright|copyright.*HP" plugins/rpgmvp/fixtures`.
  The definition counts were 3 and 10, no `SKIP`/may-fail tokens were present, the profile-copyright
  grep had no matches, all hashes matched the report, and `rtk git diff --check` exited 0 with no
  output.
- Per the requested narrow scope, release, x86, coverage, ASan, lint, import/export checks, fixture
  regeneration, network operations, and Far Manager were not run.

## Verdict

ACCEPT
