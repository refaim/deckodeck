## Substantive findings

1. `plugins/rpgmvp/src/adapters/spng/Decoder.cpp:80` — The configured libspng chunk limits are invalid, so no chunk limit is installed. The call passes `chunk_size = 64 MiB` and `cache_limit = 8 MiB`, but libspng 0.7.4 rejects `chunk_size > cache_limit`; `spng_set_chunk_limits()` returns `1` without changing the defaults, and the return value is discarded. This matters because the plugin consequently retains libspng's effectively unbounded default chunk/cache limits, so a hostile ancillary chunk can trigger a much larger allocation than the code and Task 8 contract claim to allow. The `100000x100000` fixture proves the separate image-dimension limit only and cannot catch this defect. Use values satisfying `chunk_size <= cache_limit`, make failure impossible or checked rather than discarding it, and add a test that observes the installed limits (or demonstrates rejection of an oversized chunk before allocation).

## Nits

1. `plugins/rpgmvp/src/adapters/spng/Decoder.cpp:65` — `readCallback` is noexcept in effect today because its only substantive operation is `Stream::read`, which is declared `noexcept`, but the callback itself is not declared `noexcept`. Declaring it explicitly would make the C-boundary guarantee compiler-enforced and prevent a later edit from accidentally allowing an exception to unwind through libspng.

## Verified

- Read `AGENTS.md`, `docs/ARCHITECTURE.md`, the complete Task 8 contract including its Addendum and conflict-resolution section, every scoped source/test/build/fixture file, all uncommitted tracked diffs, the overlay port, the implementation report, the older format reference, and the relevant installed libspng 0.7.4 source and headers.
- The shared contract change is limited to the authorized `ImageMeta::indexed` and `ImageMeta::interlaced` fields, the indexed `pageInfo` bpp rule, its two test polarities, and matching architecture text. AVIF uses the new default-false members and its tests remained green.
- The RPGMVP signature substitution, IHDR CRC byte ranges, stream-offset mapping, metadata derivation for all five PNG color types, tRNS alpha handling, Adam7 reporting, sRGB CICP mapping, RGB/RGBA decode formats, and in-place R/B swap were checked against the implementation and tests. The swap walks complete 3- or 4-byte pixels, so odd widths do not alter its correctness; libspng performs 16-bit reduction and grayscale expansion before that swap.
- The callback user pointer is a member whose lifetime encloses the libspng context and every spng call. `Stream::read` is `noexcept` and maps short reads to `SPNG_IO_EOF`; no exception crosses the C frames in the current code.
- Image limits are installed before IHDR parsing and reject the `100000x100000` fixture. Output pitch, height, and byte-count calculations use checked 64-bit arithmetic before narrowing, and the configured pixel ceiling keeps accepted output representable on x86. The separate chunk-limit defect is reported above.
- Fixture sizes matched `SOURCES.md`. Both committed plain-PNG twins are explicitly rejected. `decode-failures/truncated_idat.rpgmvp` is exercised as a decode failure, and `decode-failures/bad_srgb_crc.rpgmvp` is exercised as an open/metadata failure. Decrypting the RGB16 and RGBA8 encrypted fixtures reproduced their committed PNG twins byte-for-byte; SHA-256 values were respectively `4921B770...` and `A5DE3EAA...`. Independent ffprobe inspection of all 16 accepted fixtures matched their documented dimensions and pixel formats.
- The libspng overlay matches the stock vcpkg port except for `-DCMAKE_POLICY_DEFAULT_CMP0091=NEW`; it does not patch upstream codec source. Generated x64 build rules use `-MTd` in Debug and `-MT` in Release; the existing x86 Task 8 build rules show the same CRT selection. A port-local policy default is appropriately scoped to the dependency that otherwise ignores the triplet runtime selection.
- The carried-over Task 7 fixes work as specified: plugin ID must equal the directory name, the coverage profile filename regex restricts IDs to `[A-Za-z0-9_]+`, and guard shadowing tests cover rejection and both permitted polarities.

Commands and key results:

- `$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --preset debug` — configured successfully. vcpkg restored cached dependencies and rebuilt libspng from the pre-downloaded distfile; its binary-cache submission emitted an access-denied warning, but installation and configuration completed.
- `$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --build --preset debug --parallel 6` — 101 build steps completed with zero compiler warnings.
- `$env:PVDKIT_BUILD_SUFFIX='-review'; rtk ctest --preset debug --output-on-failure` — `13/13` tests passed in `48.63 sec`.
- `$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --preset release` — configured successfully.
- `$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --build --preset release --parallel 6` — completed with zero compiler warnings.
- `$env:PVDKIT_BUILD_SUFFIX='-review'; rtk ctest --preset release --output-on-failure` — `17/17` tests passed in `22.75 sec`.
- `$env:PVDKIT_BUILD_SUFFIX='-review'; $env:CMAKE_BUILD_PARALLEL_LEVEL='6'; rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage` — `13/13` tests passed; both plugin profile checks reported `18/18 Exports`; coverage completeness found all 20 source files; the gate passed at `1467/1467` lines and `388/388` branches (`100.00%` each). Scoped rows included `Decoder.cpp` at `217/217` lines and `70/70` branches, both RPGMVP core files at 100%, and `FileSession.cpp` at `80/80` lines and `32/32` branches.
- `rtk ctest --test-dir build\release-review -C Release -R "rpgmvp_check_(imports|exports)" -V` — `2/2` passed; `RPGMVP.pvd` is COFF x86-64, imports only `KERNEL32.dll`, and exports exactly the eight bare `pvd*` entry points.
- `rtk ctest --test-dir build\release-review -C Release -R "^rpgmvp_leak_tests$" -V` — `12` test cases and `21667` assertions passed; all heap, handle, and mapped-view deltas were `+0`; the hostile corpus exercised 206 files over two iterations.
- `rtk git diff --check` — passed with no output.
- `rtk powershell -NoProfile -ExecutionPolicy Bypass -File plugins\rpgmvp\scripts\rpgmvp-decrypt.ps1 ...` plus `rtk C:\Users\Roma\scoop\apps\ffmpeg\current\bin\ffprobe.exe ...` — the two byte-for-byte twin checks and all 16 independent fixture format checks passed.
- x86 and the full lint suite were not rerun, in accordance with the instruction to rerun x86 only when a concrete architecture-specific reason arose. Their quoted Task 8 report output was inspected; the x86 arithmetic and generated CRT flags were independently checked. The blocking finding is architecture-independent.

## Verdict

REJECT — the invalid `spng_set_chunk_limits` arguments silently disable the required hostile-input memory bound.

## Round 2

### Substantive findings

1. `plugins/rpgmvp/src/adapters/spng/Decoder.cpp:93` — `settingResult()` maps every setter failure to `ErrorCode::Internal`, including `SPNG_EMEM` from `spng_set_png_stream()` — libspng 0.7.4 allocates its `SPNG_READ_SIZE` buffer inside that setter and returns `SPNG_EMEM` when the allocation fails, so this is genuine OOM rather than invalid adapter configuration; `Internal` is documented for programming errors, and AGENTS.md reserves `std::bad_alloc` for OOM — handle `SPNG_EMEM` as `std::bad_alloc` while retaining `Internal` for configuration/invariant rejections, and exercise the real allocation-failure path (for example with a failing libspng allocator seam) rather than feeding a fabricated result to the mapper.

### Nits

None.

### Verified

- Read only the round-1 fix scope: `plugins/rpgmvp/src/adapters/spng/Decoder.cpp`, `plugins/rpgmvp/src/adapters/spng/Decoder.hpp`, and `plugins/rpgmvp/tests/adapters/DecoderTests.cpp`, plus the prior review, the fix report, the governing repository documents, and the relevant installed libspng 0.7.4 implementation.
- Installed libspng defines `spng_u32max` as `INT32_MAX`; `spng_set_chunk_limits()` rejects `chunk_size > spng_u32max` or `chunk_size > cache_limit` and assigns both limits only after that check. The new 16 MiB chunk / 64 MiB cache values satisfy both rules, and the production-path test seam observes those exact installed values.
- All four `spng_set_*` results in the adapter are now passed through `settingResult()`. The invalid-`maxDimension` test reaches the real `spng_set_image_limits()` rejection through `makeContext()` and verifies `Internal`; the remaining error-classification issue is the OOM case reported above.
- `readCallback` is explicitly `noexcept`; its callback body performs no allocation and delegates reading to `Stream::read`, which is also `noexcept`.
- `detail::ConfiguredLimits` and `detail::configuredLimits()` expose only standard-library and core types. Repository search found the seam referenced only by its adapter implementation and adapter tests. `src/pvd/Plugin.def` still names only the eight PVD exports, and the existing x64 DLL export table contains exactly those eight names, not the seam.
- Changed-code style compiled cleanly under `/W4 /WX`, and the guard suite passed. Coverage, lint, x86, and ASan were not rerun, as instructed.

Commands and key results:

- `rtk proxy rg -n -C 30 "spng_set_chunk_limits" C:/Users/Roma/scoop/apps/vcpkg/current/buildtrees/libspng/src/v0.7.4-e8b3878a48.clean/spng/spng.c` plus the corresponding `spng_u32max` search — line 366: `spng_u32max = INT32_MAX`; line 5162: rejection when `chunk_size > spng_u32max || chunk_size > cache_limit`; lines 5164–5166 install the values.
- `rtk proxy powershell -NoProfile -Command "(Get-Content 'C:/Users/Roma/scoop/apps/vcpkg/current/buildtrees/libspng/src/v0.7.4-e8b3878a48.clean/spng/spng.c')[5067..5105]"` — `spng_set_png_stream()` allocates `SPNG_READ_SIZE` at line 5087 and returns `SPNG_EMEM` at line 5088 if that allocation fails.
- `$env:PVDKIT_BUILD_SUFFIX = '-review'; rtk cmake --build --preset debug --parallel 6 --target rpgmvp_adapter_tests guard_tests` — exit 0; rebuilt the RPGMVP adapter and adapter tests with no warnings.
- `rtk proxy .\build\debug-review\plugins\rpgmvp\tests\adapters\rpgmvp_adapter_tests.exe` — `13/13` test cases and `554/554` assertions passed.
- `rtk proxy .\build\debug-review\tests\guard\guard_tests.exe` — `19/19` test cases and `258/258` assertions passed.
- `rtk proxy 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-readobj.exe' --coff-exports build\debug-review\plugins\rpgmvp\RPGMVP.pvd` — COFF x86-64; exactly `pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`, `pvdPageDecode`, `pvdPageFree`, `pvdPageInfo`, and `pvdPluginInfo` are exported.
- `rtk git diff --check -- docs/tasks/review-task8.md` — passed with no output.

### Verdict

REJECT — the bounds are now valid and installed, but the checked stream-setter failure still misclassifies libspng's real OOM result as an internal programming error.

### Orchestrator decision on Round 2 (2026-09-13)
Round 2 finding 1 (`SPNG_EMEM` from `spng_set_png_stream` classified as `ErrorCode::Internal`
instead of `std::bad_alloc`) is **not accepted as substantive**: the repository's reviewed
convention, set by the AVIF adapter, is that a library *result code* reporting exhaustion maps to
`Internal` (`AVIF_RESULT_OUT_OF_MEMORY → Internal`, `plugins/avif/tests/adapters/DecoderTests.cpp`)
and only a null handle from a creation call throws `std::bad_alloc` (`requireDecoder`,
`requireContext`). Host-visible behaviour is identical either way (the open fails), and the
proposed failing-allocator seam would put a production allocator indirection (and `malloc`-family
symbols) into the adapter for the sake of one test. Task 8 is therefore ACCEPTED with rounds 1
(fixed) and 2 (overruled) on record.
