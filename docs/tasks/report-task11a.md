# Task 11a — host-sequence replay driver

## Outcome

Implemented the in-process host-sequence replay infrastructure and retained the existing
caller-supplied `DecoderOptions` work after verifying its behavior. All requested x64/x86 test,
coverage, Release ABI, and x64 lint gates pass. No commit was made.

## What was built

- Added `tests/support/sequence/SequenceDriver.hpp` and `.cpp` as the framework-free static library
  `pvdkit_sequence`, linked with `pvdkit_pvd`, `pvdkit_core`, and `pvdkit_options` (not doctest).
- Added `SequenceReport { opened, pages, decoded, aborted }` and
  `replayHostSequence(pvd::Shim &, std::span<const std::byte>)`.
- The driver replays `init -> pluginInfo -> fileOpen` in memory mode, then up to the first four
  advertised pages through `pageInfo -> pageDecode -> pageFree`, followed by `fileClose -> exit`.
- Added deterministic FNV-1a callback behavior: inputs whose hash is divisible by five abort on the
  second progress callback. Reports count successful decodes and callbacks that actually aborted a
  decode.
- Added fatal host-contract checks. A violation is printed to stderr and terminates with
  `std::abort()`: interface version, bounded NUL-terminated plugin/image strings, non-null context on
  open success and null context on failure, at least one page, non-zero page dimensions, 24/32-bpp
  output, non-null pixels, sufficient absolute pitch, addressable rows, and readable pixel bytes in
  every row (including the first and last). `pageFree` and `fileClose` are void/noexcept; returning
  normally is their observable success condition. The code comments record that every Shim entry is
  `noexcept`, so an escaping C++ exception already terminates and no dead catch is added.
- Added shared `SequenceDriverTests.cpp`. It recursively replays every non-`.md` fixture twice with
  `maxPixels = 4 * 1024 * 1024`, `maxDimension = 8192`, `maxThreads = 1`, checks equal reports, and
  checks each plugin's established accept/reject result. AVIF passed 121/121 sequence assertions;
  RPGMVP passed 101/101.
- Added `pvdkit_add_plugin_sequence_tests(id)`. It deliberately creates one executable per plugin
  (`avif_sequence_tests`, `rpgmvp_sequence_tests`) because both composition libraries define
  `pvd::makePlugin` and cannot coexist in one executable.
- Kept and completed the uncommitted `makePlugin(const core::DecoderOptions &)` overload in
  `PluginFactory.hpp` and both composition roots, with adapter tests for both plugins.

## Decisions

- The AVIF reduced-limit test correctly expects open success followed by decode failure with
  `TooLarge`. `DecoderFactory::create` clamps libavif's parse-time `imageSizeLimit` to at least one;
  the 1x1 fixture therefore parses, while `PixelBuffer::create` enforces the caller's actual
  `maxPixels = 0` during decode.
- RPGMVP's dimension limit is enforced during open, so its overload test correctly expects
  `TooLarge` from `open`.
- Recursive RPGMVP fixture expectations include the two `decode-failures/` inputs: bad sRGB CRC is
  rejected at open, while truncated IDAT opens and then fails decode cleanly.
- The sequence driver always calls `pageFree` after `pageDecode`, including failed/aborted decodes,
  matching a uniform host cleanup path; a failed decode must leave `pImage == nullptr`.

## TDD evidence

The shared fixture tests and per-plugin registrations were added first with a stub driver. The first
targeted run was intentionally red:

```text
$ rtk ctest --test-dir build/debug-t11a -R sequence_tests --output-on-failure
ctest: 0/2 passed, 2 failed (0.48 sec)
avif_sequence_tests: 24 accepted fixtures reported false (121 assertions, 24 failed)
rpgmvp_sequence_tests: 17 accepted fixtures reported false (101 assertions, 17 failed)
```

After implementing the minimal driver:

```text
$ rtk ctest --test-dir build/debug-t11a -R sequence_tests --output-on-failure
ctest: 2/2 passed (1.08 sec)
```

The first full lint exposed two `bugprone-implicit-widening-of-multiplication-result` findings in
4 Mi constants (one in the pre-existing RPGMVP overload test, one in the new sequence test). Both
were changed to start with `std::uint64_t{4}`; a focused clang-tidy run then passed, followed by the
complete final gate sequence below.

## Final verification

All commands used `$env:PVDKIT_BUILD_SUFFIX = "-t11a"`. Builds used `--parallel 6`; coverage scripts
were run with `$env:CMAKE_BUILD_PARALLEL_LEVEL = "6"` so their internal build obeyed the same limit.
The final requested gates were run in this order:

1. `rtk ctest --preset debug`

   ```text
   ctest: 15/15 passed (49.25 sec)
   slowest: avif_leak_tests 23.66 sec; rpgmvp_leak_tests 18.63 sec; guard_tests 2.35 sec
   ```

2. `rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage`

   ```text
   100% tests passed, 0 tests failed out of 15 (53.04 sec)
   TOTAL: 1,561/1,561 lines (100.00%), 390/390 branches (100.00%)
   Coverage source completeness passed: 20 executable source files present.
   Plugin profile checks: AVIF 18/18 Exports.cpp functions; RPGMVP 18/18.
   Coverage gate passed: lines 100%, branches 100%.
   ```

3. `rtk ctest --preset release`

   ```text
   ctest: 19/19 passed (22.26 sec)
   slowest: avif_leak_tests 14.87 sec; rpgmvp_leak_tests 2.83 sec; avif_adapter_tests 0.96 sec
   ```

4. `rtk ctest --preset debug-x86`

   ```text
   ctest: 15/15 passed (52.30 sec)
   slowest: avif_leak_tests 26.61 sec; rpgmvp_leak_tests 17.29 sec; guard_tests 3.55 sec
   ```

5. `rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86`

   ```text
   100% tests passed, 0 tests failed out of 15 (64.16 sec)
   TOTAL: 1,561/1,561 lines (100.00%), 390/390 branches (100.00%)
   Coverage source completeness passed: 20 executable source files present.
   Plugin profile checks: AVIF 18/18 Exports.cpp functions; RPGMVP 18/18.
   Coverage gate passed: lines 100%, branches 100%.
   ```

6. `rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6`

   ```text
   clang-format:      0 finding(s) in 0.5 s
   clang-tidy:        0 finding(s) in 274.4 s
   cppcheck:          0 finding(s) in 1.2 s
   PSScriptAnalyzer:  0 finding(s) in 3.6 s
   BinSkim:           0 finding(s) in 1.2 s
   lint: clean
   ```

Prerequisite final builds were:

```text
rtk cmake --build --preset debug --parallel 6          # success, zero warnings
rtk cmake --build --preset release --parallel 6        # success, zero warnings
rtk cmake --build --preset debug-x86 --parallel 6      # success, zero warnings
```

The coverage scripts configured and built `coverage` and `coverage-x86` successfully, also with
zero compiler warnings. `rtk git diff --check` produced no output.

## Release import and export tables

The Release ctest entries `avif_check_imports`, `avif_check_exports`, `rpgmvp_check_imports`, and
`rpgmvp_check_exports` all passed. `build/release-t11a/Testing/Temporary/LastTest.log` records:

```text
AVIF.pvd    (COFF-x86-64): Name: KERNEL32.dll
RPGMVP.pvd (COFF-x86-64): Name: KERNEL32.dll
```

No other DLL appears in either import table. Both export tables contain exactly these eight bare
names:

```text
pvdExit
pvdFileClose
pvdFileOpen
pvdInit
pvdPageDecode
pvdPageFree
pvdPageInfo
pvdPluginInfo
```

## Not done / working-tree state

- No commit, push, staging, reset, checkout, clean, worktree, compiler/tool installation, or network
  access was performed. CMake/vcpkg restored the manifest dependencies from local caches into the
  suffixed build trees.
- ASan, x86 Release, and x86 lint were not requested in the prescribed task-11a command matrix and
  were not run.
- No production decoder contract changed beyond exposing the injected-options composition overload.
- The task files remain intentionally uncommitted, together with the overload changes that were
  already present at task start.

## Fix round 1

### Review findings resolved

- `rowSpan` is now the pure row-address/bounds helper used by the driver. It computes the absolute
  pitch and full `|pitch| * height` extent with unsigned 64-bit arithmetic, rejects extents that do
  not fit `size_t` (with an explicit-limit overload for architecture-independent boundary tests),
  rejects a wrapping end address, and maps bottom-up logical row `r` to
  `pImage + (height - 1 - r) * |pitch|`. Tests cover row zero and the last row for both pitch signs,
  invalid rows, a simulated 32-bit size overflow, and an overflowing end address. The driver reads
  every logical row, so the first and last rows are touched inside the checked buffer range.
- `SequenceDriver.hpp` now states that this is a validator of the stricter pvdkit `pvd::Shim`
  output contract, not arbitrary PVD plugins. The 24/32-bpp and non-null string checks remain for
  that reason. The named `kMaxStringBytes` constant has a comment explaining that its 4096-byte
  bound prevents an unbounded read from a malformed Shim result while exceeding every pvdkit-owned
  string.
- A failed `pageDecode` is now opaque: the driver does not inspect `pvdInfoDecode` and never calls
  `pageFree`. Callback-requested failures increment `aborted`; all other decode failures increment
  the new `SequenceReport::failed` counter. `pageFree` is called only after a successful decode.
- The fixture test now checks complete counter relationships. Every open-accepted fixture reports
  at least one page and satisfies
  `decoded + aborted + failed == min(pages, 4)`; ordinary accepted fixtures require `failed == 0`,
  while plugin-listed decode-failure fixtures are replayed with abort disabled and must report a
  failure. `AbortPolicy{1, 1}` deterministically aborts every attempted page of ordinary accepted
  fixtures, proving the callback-abort path in each plugin fixture folder without copying a file.
  The default policy remains the original FNV-1a divisible-by-five, second-callback behavior.
- Fixture policy is plugin-owned. AVIF and RPGMVP now keep their established accepted/rejected/
  decode-failure names in one plugin-local `FixtureExpectations.hpp`, shared by their existing e2e
  tests and their small `SequenceExpectations.cpp`. Shared sequence code contains no plugin names.
  `pvdkit_add_plugin_e2e_tests` requires `SEQUENCE_EXPECTATIONS`, and
  `pvdkit_add_plugin_sequence_tests` requires an existing `EXPECTATIONS` source, with explicit
  configure-time fatal messages when either is missing.
- `checkPluginInfo`, `checkImageInfo`, `checkPageInfo`, `checkDecoded`, `checkProgress`,
  `checkInvariant`, and `rowSpan` expose the invariant logic as pure functions. Their success and
  failure branches are unit-tested. One death test starts the sequence executable itself with
  `CreateProcessW`, redirects the child's `stderr`, invokes `invariantViolation` through the common
  enforcement path, verifies the diagnostic, and requires a nonzero exit. In coverage builds the
  SIGABRT handler writes the child's counters before `_Exit`, after `invariantViolation` has printed
  and flushed its message.

### TDD and review-fix evidence

The new tests and declarations were added before their implementation. The first build caught a
deprecated `getenv` use in the new Windows death-test harness under `/WX`; after changing that test
to `GetEnvironmentVariableW`, the intended red build failed at link time on the new row/check/
policy/expectation APIs:

```text
$env:PVDKIT_BUILD_SUFFIX='-t11a'
rtk cmake --build --preset debug --parallel 6
lld-link: error: undefined symbol: pvdkit::sequence::rowSpan(...)
lld-link: error: undefined symbol: pvdkit::sequence::checkPluginInfo(...)
lld-link: error: undefined symbol: pvdkit::sequence::checkImageInfo(...)
lld-link: error: undefined symbol: pvdkit::sequence::checkPageInfo(...)
lld-link: error: undefined symbol: pvdkit::sequence::checkDecoded(...)
lld-link: error: undefined symbol: pvdkit::sequence::fixtureDisposition(...)
lld-link: error: undefined symbol: pvdkit::sequence::replayHostSequence(..., AbortPolicy)
ninja: build stopped: subcommand failed.
```

After the minimal implementation and plugin expectations landed, the focused tests passed:

```text
$env:PVDKIT_BUILD_SUFFIX='-t11a'
rtk cmake --build --preset debug --parallel 6
rtk ctest --test-dir build/debug-t11a -R sequence_tests --output-on-failure
ctest: 2/2 passed (1.08 sec)
```

Direct final runs reported:

```text
rtk proxy build/debug-t11a/plugins/avif/tests/e2e/avif_sequence_tests.exe --no-colors=true
[doctest] test cases: 6 | 6 passed | 0 failed | 0 skipped
[doctest] assertions: 368 | 368 passed | 0 failed

rtk proxy build/debug-t11a/plugins/rpgmvp/tests/e2e/rpgmvp_sequence_tests.exe --no-colors=true
[doctest] test cases: 6 | 6 passed | 0 failed | 0 skipped
[doctest] assertions: 297 | 297 passed | 0 failed
```

The first fix-round lint found three `performance-no-int-to-ptr` diagnostics in the checked-address
implementation/tests. Row construction now uses a checked positive pointer offset, and artificial
near-maximum pointers use `std::bit_cast`; the final complete lint is clean.

### Final verification

Every command below used `$env:PVDKIT_BUILD_SUFFIX = '-t11a'`. Builds used at most six jobs; the
coverage script used `$env:CMAKE_BUILD_PARALLEL_LEVEL = '6'`. No builds or lint runs overlapped.

1. `rtk ctest --preset debug`

   ```text
   ctest: 15/15 passed (48.17 sec)
   slowest: avif_leak_tests 23.43 sec; rpgmvp_leak_tests 18.64 sec; guard_tests 2.23 sec
   ```

2. `rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage`

   ```text
   100% tests passed, 0 tests failed out of 15 (54.60 sec)
   TOTAL: 1,561/1,561 lines (100.00%), 390/390 branches (100.00%)
   Coverage source completeness passed: 20 executable source files present.
   Plugin profile checks: AVIF 18/18 Exports.cpp functions; RPGMVP 18/18.
   Coverage gate passed: lines 100%, branches 100%.
   ```

3. Focused sequence-support coverage:

   ```text
   rtk proxy powershell -NoProfile -Command "& '<LLVM>/llvm-cov.exe' report 'build/coverage-t11a/plugins/avif/tests/e2e/avif_sequence_tests.exe' '-instr-profile=build/coverage-t11a/coverage.profdata' 'tests/support/sequence/SequenceDriver.cpp' 'tests/support/sequence/SequenceDriverTests.cpp'"

   SequenceDriver.cpp       193/193 lines (100.00%), 76/76 branches (100.00%)
   SequenceDriverTests.cpp  219/219 lines (100.00%), 16/16 branches (100.00%)
   TOTAL                    412/412 lines (100.00%), 92/92 branches (100.00%)
   ```

4. Release configure/build and tests:

   ```text
   rtk cmake --preset release
   rtk cmake --build --preset release --parallel 6
   rtk ctest --preset release
   ctest: 19/19 passed (21.76 sec)
   slowest: avif_leak_tests 14.37 sec; rpgmvp_leak_tests 2.81 sec; avif_adapter_tests 0.97 sec
   ```

5. x86 Debug configure/build and tests:

   ```text
   rtk cmake --preset debug-x86
   rtk cmake --build --preset debug-x86 --parallel 6
   rtk ctest --preset debug-x86
   ctest: 15/15 passed (51.08 sec)
   slowest: avif_leak_tests 25.40 sec; rpgmvp_leak_tests 17.28 sec; guard_tests 3.39 sec
   ```

6. `rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6`

   ```text
   clang-format:      0 finding(s) in 0.6 s
   clang-tidy:        0 finding(s) in 297.2 s
   cppcheck:          0 finding(s) in 1.2 s
   PSScriptAnalyzer:  0 finding(s) in 3.7 s
   BinSkim:           0 finding(s) in 1.3 s
   lint: clean
   ```

The final Debug, Release, x86 Debug, and coverage builds completed with zero compiler warnings.
`rtk git diff --check` produced no output.

### Release ABI tables

The Release tests `avif_check_imports`, `avif_check_exports`, `rpgmvp_check_imports`, and
`rpgmvp_check_exports` passed. `build/release-t11a/Testing/Temporary/LastTest.log` contains one
import for each plugin:

```text
AVIF.pvd:   Name: KERNEL32.dll
RPGMVP.pvd: Name: KERNEL32.dll
```

Both export tables contain exactly the eight bare names:

```text
pvdExit
pvdFileClose
pvdFileOpen
pvdInit
pvdPageDecode
pvdPageFree
pvdPageInfo
pvdPluginInfo
```

### Not done / working-tree state

- No commit, push, staging, reset, checkout, clean, worktree, installation, or network access was
  performed. Configure steps found every vcpkg dependency already installed locally.
- ASan, x86 Release, x86 coverage, and x86 lint were not requested for this fix round and were not
  run.
- The reviewed Task 11a changes, this fix round, and the pre-existing injected-options changes all
  remain intentionally uncommitted.

## Fix round 2

### Done

- `tests/support/sequence/SequenceDriverTests.cpp` now compiles both
  `exitAfterAbortSignal` and its `std::signal(SIGABRT, ...)` installation only under
  `#if PVDKIT_COVERAGE`. Coverage children still call `__llvm_profile_write_file()` before
  `_Exit(3)`; Debug and Release leave `SIGABRT` at its default disposition and assert only that
  the child exit code is non-zero.
- `CaptureFile` owns the death-test capture path and attempts removal unconditionally in its
  destructor. Its focused test creates a real file and proves that scope exit removes it.
- `ChildProcess` immediately adopts both `PROCESS_INFORMATION` handles into `std::unique_ptr`
  instances with a `CloseHandle` deleter. Its destructor terminates/reaps before closing the
  handles on every early exit. After the bounded 30-second wait, `runDeathChild()` calls
  `TerminateProcess` and waits again before checking the original wait result, so a timeout is
  cleaned up before `REQUIRE` reports it.

### TDD evidence

The capture-file cleanup test and an intentionally undefined `CaptureFile` destructor were added
first. The red build failed in both plugin sequence targets for exactly that missing behavior:

```text
$env:PVDKIT_BUILD_SUFFIX='-t11a'
rtk cmake --build --preset debug --parallel 6
error: function 'CaptureFile::~CaptureFile' has internal linkage but is not defined
ninja: build stopped: subcommand failed.
```

After implementing the owner and integrating both RAII holders, the focused build and test run
passed without compiler warnings:

```text
$env:PVDKIT_BUILD_SUFFIX='-t11a'
rtk cmake --build --preset debug --parallel 6
[4/5] Linking CXX executable plugins\avif\tests\e2e\avif_sequence_tests.exe

rtk ctest --test-dir build/debug-t11a -R sequence_tests --output-on-failure
ctest: 2/2 passed (1.07 sec)
```

### Final verification

Every command below used `$env:PVDKIT_BUILD_SUFFIX='-t11a'`; build parallelism was capped at six,
and no build or lint command overlapped another.

1. Final x64 Debug build and tests:

   ```text
   rtk cmake --build --preset debug --parallel 6
   (exit code 0; zero warning diagnostics)

   rtk ctest --preset debug
   ctest: 15/15 passed (48.58 sec)
   slowest: avif_leak_tests 23.55 sec; rpgmvp_leak_tests 18.47 sec; guard_tests 2.25 sec
   ```

2. Final x64 Release build and tests, including both real-default-abort death tests and all four
   ABI policy tests:

   ```text
   rtk cmake --build --preset release --parallel 6
   (exit code 0; zero warning diagnostics)

   rtk ctest --preset release
   ctest: 19/19 passed (21.90 sec)
   slowest: avif_leak_tests 14.53 sec; rpgmvp_leak_tests 3.00 sec; avif_adapter_tests 0.98 sec
   ```

3. Final coverage build/test/gate (`$env:CMAKE_BUILD_PARALLEL_LEVEL='6'`):

   ```text
   rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
   100% tests passed, 0 tests failed out of 15 (51.76 sec)
   TOTAL: 1,561/1,561 lines (100.00%), 390/390 branches (100.00%)
   Coverage source completeness passed: 20 executable source files present.
   Plugin profile checks: AVIF 18/18 Exports.cpp functions; RPGMVP 18/18.
   Coverage gate passed: lines 100%, branches 100%.
   ```

4. Focused sequence-support coverage:

   ```text
   rtk proxy powershell -NoProfile -Command "& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-cov.exe' report 'build/coverage-t11a/plugins/avif/tests/e2e/avif_sequence_tests.exe' '-instr-profile=build/coverage-t11a/coverage.profdata' 'tests/support/sequence/SequenceDriver.cpp' 'tests/support/sequence/SequenceDriverTests.cpp'"

   SequenceDriver.cpp       194/194 lines (100.00%), 76/76 branches (100.00%)
   SequenceDriverTests.cpp  255/255 lines (100.00%), 16/16 branches (100.00%)
   TOTAL                    449/449 lines (100.00%), 92/92 branches (100.00%)
   ```

5. The first complete lint run found only eight clang-format findings in the new RAII declarations;
   clang-tidy, cppcheck, PSScriptAnalyzer, and BinSkim were already clean. The configured LLVM
   formatter was then applied, and the final complete run passed:

   ```text
   rtk proxy powershell -NoProfile -Command "& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-format.exe' -i 'tests/support/sequence/SequenceDriverTests.cpp'"
   rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6

   clang-format:      0 finding(s) in 0.5 s
   clang-tidy:        0 finding(s) in 288.2 s
   cppcheck:          0 finding(s) in 1.2 s
   PSScriptAnalyzer:  0 finding(s) in 3.5 s
   BinSkim:           0 finding(s) in 1.0 s
   lint: clean
   ```

6. `rtk git diff --check` produced no output.

### Release ABI tables

`build/release-t11a/Testing/Temporary/LastTest.log`, inspected with
`rtk rg -n -C 3 "KERNEL32\.dll|Export checks passed|Import checks passed|pvd(File|Init|Page|Plugin|Exit)" build/release-t11a/Testing/Temporary/LastTest.log`, records exactly one imported module for
each Release plugin:

```text
AVIF.pvd:   Name: KERNEL32.dll
RPGMVP.pvd: Name: KERNEL32.dll
```

Both export tables contain exactly the eight bare names:

```text
pvdExit
pvdFileClose
pvdFileOpen
pvdInit
pvdPageDecode
pvdPageFree
pvdPageInfo
pvdPluginInfo
```

### Not done / working-tree state

- A controlled hung-child mode was not added solely to force the 30-second timeout. That path is
  documented rather than directly induced: another child protocol was not worth the extra test
  infrastructure. The cleanup operations themselves are unconditional and covered, and the file
  owner's real deletion behavior has a focused test.
- No ASan or x86 run was requested for Round 2, so neither was run.
- No network access, dependency installation, commit, push, staging, reset, checkout, clean, or
  worktree operation was performed. Vcpkg reported that every dependency was already installed.
- The reviewed Task 11a work and both Round 2 fixes remain intentionally uncommitted alongside the
  pre-existing working-tree changes.
