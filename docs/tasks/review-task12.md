## Substantive findings

1. `plugins/rpgmvp/CMakeLists.txt:5` — The new `PVDKIT_RPGMVP_DEEP_OUTPUT=ON` configuration is not test-clean, and the added deep test does not exercise the option-built DLL. In an isolated ON build, `ctest` for the RPGMVP adapter/e2e/sequence targets passed only 1/3 tests: `DefaultPluginTests.cpp:58` still requires the shallow comment, and `tests/e2e/PluginHost.cpp:170` rejects the DLL's valid `nBPP == 64` result. `CompositionTests.cpp` passes only because `plugins/rpgmvp/tests/e2e/CMakeLists.txt:12` attaches it to the in-process sequence executable and constructs `makePlugin(options)` directly. This violates the green-test contract and leaves the experiment DLL's deep runtime path unverified. Make the composition and RPGMVP e2e expectations configuration-aware, allow 64 bpp in the shared host helper, and run the full ON test suite against the produced DLL.

2. `plugins/rpgmvp/CMakeLists.txt:41` — The experiment macro is a `PUBLIC` usage requirement. The default build's `compile_commands.json` consequently contains `-DPVDKIT_RPGMVP_DEEP_OUTPUT=0` not only for `rpgmvp_composition`, but also for `Exports.cpp`, `Plugin.rc`, every RPGMVP adapter test, and every RPGMVP sequence-test translation unit. Thus the experiment define does leak out of the composition target in the default build, contrary to the requested isolation; it also creates an avoidable plugin-specific macro dependency for unrelated consumers. Scope the definition `PRIVATE` to the composition target and provide any configuration signal needed by tests explicitly.

## Nits

None.

## Verified

- `$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --preset debug` — configured `build/debug-review` successfully with clang-cl 19.1.5.
- `$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --build --preset debug --parallel 6` — completed all Ninja steps with no compiler warnings.
- `$env:PVDKIT_BUILD_SUFFIX='-review'; rtk ctest --preset debug` — `15/15 passed (120.65 sec)`; slowest were `avif_leak_tests` 64.30 s, `rpgmvp_leak_tests` 37.58 s, and `guard_tests` 5.96 s.
- `$env:PVDKIT_BUILD_SUFFIX='-review'; $env:CMAKE_BUILD_PARALLEL_LEVEL='6'; rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage` — `15/15` tests passed in 54.03 s; source completeness found all 20 executable production files; both plugin profile checks executed `18/18` `Exports.cpp` functions. Key rows were:

  | File | Lines | Branches |
  |---|---:|---:|
  | `plugins/avif/src/DefaultPlugin.cpp` | 24/24 (100%) | no branches |
  | `plugins/avif/src/adapters/avif/Decoder.cpp` | 259/259 (100%) | 54/54 (100%) |
  | `plugins/rpgmvp/src/DefaultPlugin.cpp` | 27/27 (100%) | 2/2 (100%) |
  | `plugins/rpgmvp/src/adapters/spng/Decoder.cpp` | 340/340 (100%) | 86/86 (100%) |
  | `src/core/FileSession.cpp` | 83/83 (100%) | 38/38 (100%) |
  | `src/pvd/Shim.cpp` | 258/258 (100%) | 46/46 (100%) |
  | **TOTAL** | **1608/1608 (100%)** | **416/416 (100%)** |

  `src/core/IDecoder.hpp` and `src/pvd/Types.hpp` also reported 100% of executable lines; `src/pvd/PvdApi.hpp` contains no functions.
- `$env:PVDKIT_BUILD_SUFFIX='-review-deep'; rtk cmake --preset debug -DPVDKIT_RPGMVP_DEEP_OUTPUT=ON` and `$env:PVDKIT_BUILD_SUFFIX='-review-deep'; rtk cmake --build --preset debug --parallel 6` — configured and built the x64 experiment variant with no compiler warnings.
- `$env:PVDKIT_BUILD_SUFFIX='-review-deep'; rtk ctest --preset debug -R 'rpgmvp_(adapter|e2e|sequence)_tests' --output-on-failure` — **failed**: `1/3 passed, 2 failed`. `rpgmvp_adapter_tests` failed at `DefaultPluginTests.cpp:58` because the actual comments included ` [experiment: deep output]`; `rpgmvp_e2e_tests` failed at `PluginHost.cpp:170` because the DLL returned 64 bpp.
- `rtk proxy .\build\debug-review\plugins\rpgmvp\tests\e2e\rpgmvp_e2e_tests.exe '--test-case=the plugin carries*'` and the same command under `build\debug-review-deep` — `49/49` assertions passed in each polarity. The generated default comments have no suffix; the ON comments end with ` [experiment: deep output]`.
- `rtk rg -n 'PVDKIT_RPGMVP_DEEP_OUTPUT|PVDKIT_PLUGIN_COMMENTS' ...` — confirmed the default generated comment has no experiment suffix and the ON generated comment does; also confirmed the `PUBLIC` macro propagation described above.
- `rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-imports.ps1 -Path <DLL>` and the corresponding `scripts\check-exports.ps1` command, run for `build\debug-review\plugins\avif\AVIF.pvd`, `build\debug-review\plugins\rpgmvp\RPGMVP.pvd`, and `build\debug-review-deep\plugins\rpgmvp\RPGMVP.pvd` — all are `COFF-x86-64`, import only `KERNEL32.dll`, and export exactly `pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`, `pvdPageDecode`, `pvdPageFree`, `pvdPageInfo`, and `pvdPluginInfo`.
- `rtk git diff --check` — exited 0 with no output.
- The libspng 0.7.4 cached headers/docs confirm `SPNG_FMT_RGBA16` is eight bytes per pixel, non-RAW output is host-endian, and RGBA16 accepts every PNG colour type/bit depth. The in-place swap advances by eight bytes and swaps byte pairs at offsets 0/1 with 4/5, so it is independent of width parity. `PixelBuffer` and destination validation keep pitch/size arithmetic in 64 bits before narrowing; the BGRA64 transform regression checks all eight bytes per pixel through a nontrivial rotation/mirror mapping.
- Per instruction, x86, ASan, and lint were not rerun. I modified no repository file other than this review.

## Verdict

REJECT

## Round 2

### Substantive findings

### Nits

None.

### Verified

- `$env:PVDKIT_BUILD_SUFFIX='-review-deep'; rtk cmake --preset debug -DPVDKIT_RPGMVP_DEEP_OUTPUT=ON` configured `build/debug-review-deep` successfully from installed/local cached dependencies. `$env:PVDKIT_BUILD_SUFFIX='-review-deep'; rtk cmake --build --preset debug --parallel 6` then completed with no compiler warnings.
- `$env:PVDKIT_BUILD_SUFFIX='-review-deep'; rtk ctest --preset debug` passed the complete option-ON suite. Summary: `ctest: 15/15 passed (50.93 sec)`; the slowest tests were `avif_leak_tests` at 23.20 s, `rpgmvp_leak_tests` at 18.95 s, and `guard_tests` at 2.60 s.
- `$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --build --preset debug --parallel 6` refreshed and built the existing default tree with no compiler warnings. `$env:PVDKIT_BUILD_SUFFIX='-review'; rtk ctest --preset debug` passed. Summary: `ctest: 15/15 passed (48.71 sec)`; the slowest tests were `avif_leak_tests` at 23.27 s, `rpgmvp_leak_tests` at 18.64 s, and `guard_tests` at 2.27 s.
- `rtk rg -n PVDKIT_RPGMVP_DEEP_OUTPUT build/debug-review/compile_commands.json` returned exactly one match, line 382: `-DPVDKIT_RPGMVP_DEEP_OUTPUT=0` on `rpgmvp_composition.dir\\src\\DefaultPlugin.cpp.obj`, sourced from `plugins\\rpgmvp\\src\\DefaultPlugin.cpp`. The production macro does not appear on `Exports.cpp`, `Plugin.rc`, or any test/adapter translation unit.
- The separate test signal is derived directly from the same option: `rpgmvp_adapter_tests`, `rpgmvp_e2e_tests`, and `rpgmvp_sequence_tests` receive `PVDKIT_TEST_DEEP_OUTPUT=$<BOOL:${PVDKIT_RPGMVP_DEEP_OUTPUT}>`. The production definition on `rpgmvp_composition` is `PRIVATE`.
- `tests/e2e/PluginHostTests.cpp` pins both sides of the helper contract: tight 24/32-bit rows are accepted while padded/undersized rows are rejected; tight and padded 64-bit rows are accepted while undersized, zero, and negative pitches are rejected; an unsupported depth is rejected. `isSupportedDecodeLayout()` implements exactly that rule using 64-bit size arithmetic. `tests/support/leak/LeakScenarios.cpp` calls the same helper rather than carrying another layout predicate.
- The option-ON `rpgmvp_e2e_tests` compile command contains both `PVDKIT_TEST_DEEP_OUTPUT=1` and `PVDKIT_PLUGIN_PATH=\".../build/debug-review-deep/plugins/rpgmvp/RPGMVP.pvd\"`. The e2e target links no RPGMVP static library; `loadInitializedPlugin()` passes that configured path to `PluginLibrary::load()`, which calls `LoadLibraryW` and resolves the eight exports. Thus the configuration-aware checks run against the produced DLL.
- Through that loaded DLL, the accepted-fixture test derives 64 bpp for both deep fixtures and checks tight pitches (`60 * 8 == 480`, `88 * 8 == 704`) plus `Flags == 2/0` from their alpha expectations. The pixel test checks the RGB16 first pixel as little-endian BGRA16 bytes `{F4, 3B, 60, 60, 96, 1B, FF, FF}`, i.e. `(3BF4, 6060, 1B96, FFFF)`. A focused invocation of `build/debug-review-deep/plugins/rpgmvp/tests/e2e/rpgmvp_e2e_tests.exe` for these two test cases passed `2/2` cases and `496/496` assertions.
- Per instruction, coverage, lint, x86, ASan, and import/export checks were not rerun. No network access was used, and no file other than this review was modified by the review.

### Verdict

ACCEPT
