## Substantive findings

1. `tests/support/sequence/SequenceDriver.cpp:107` — Negative pitch is handled by adding `signedPitch * rowIndex` to `pImage`. For a legal bottom-up PVD image this walks before the buffer: the SDK's reference `pvdBMP.cpp` returns the low-address pixel base and a negative pitch for bottom-up BMPs. The current fixtures never take this branch, so the bug is latent. This is an out-of-bounds read / false host-contract failure. Compute row addresses within `[pImage, pImage + |pitch| * height)` (reverse the logical row index when pitch is negative), check `lastRowOffset + rowBytes` for overflow/addressability, and add a negative-pitch test.

2. `tests/support/sequence/SequenceDriver.cpp:91` — Treating every decoded image whose `nBPP` is not 24 or 32 as a fatal host-contract violation rejects legal PVD output. `PictureViewPlugin.h` explicitly defines `pPalette` for formats of 8 bpp or less, and the author's BMP decoder returns indexed output. This makes the purported host replay narrower than the ABI. Accept and validate indexed layouts (including packed row-byte arithmetic and palette metadata), or explicitly make this a pvdkit-internal output-contract validator rather than claiming it mirrors a PVD host.

3. `tests/support/sequence/SequenceDriver.cpp:54` — `pCompression` and `pComments` are required to be non-null and all strings are arbitrarily required to terminate within 4096 bytes, but neither restriction is in the SDK. The reference BMP decoder returns null for both fields, and the reference IJL decoder can return a null image comment. A conforming decoder therefore aborts in the replay driver. Allow null optional image strings and remove the invented 4096-byte host-contract limit (or confine such checks to guarantees actually made by pvdkit's own `Shim`).

4. `tests/support/sequence/SequenceDriver.cpp:174` — The failure path inspects `decoded.pImage` and line 184 calls `pageFree` unconditionally. The SDK says `pvdPageFree` is called after a *successful* `pvdPageDecode`; fields are not promised on failure. The author's IJL decoder even frees a failed allocation but leaves the non-null value in `pImage`, so this driver would first abort on legitimate behavior and, without that check, would double-free it. Ignore `pvdInfoDecode` after `FALSE` (including the normal callback-abort result) and call `pageFree` only after `TRUE`.

5. `tests/support/sequence/SequenceDriverTests.cpp:94` — The fixture test compares two reports and checks only `opened`; it never asserts `pages`, `decoded`, or `aborted`. A regression that makes every accepted fixture's `pageDecode` return `FALSE` deterministically still passes, and no pixel is touched. This is a test gap in the central sequence behavior. Assert complete expected reports, or at least require every replayed page to decode or take the deterministic abort path while explicitly whitelisting the known corrupt decode-only fixtures.

6. `tests/support/sequence/SequenceDriverTests.cpp:37` — Shared test code special-cases only `"avif"` and `"rpgmvp"`, while `pvdkit_add_plugin_e2e_tests` automatically registers this test for every plugin. Any third plugin receives an empty rejection span and fails at line 82 before replaying anything. That breaks the monorepo's plugin-addition contract and puts plugin-specific fixture policy in shared infrastructure. Move expectations to plugin-local test data/sources and pass them into the generic helper.

7. `tests/support/sequence/SequenceDriver.cpp:24` — None of the new fatal checks is tested. A focused `llvm-cov` report showed `invariantViolation` executed zero times and `SequenceDriver.cpp` at 92.86% lines / 86.67% branches; the negative-pitch branch also executed zero times. The repository-wide 100/100 gate hides this because `tests/**` is excluded. This leaves the task's defining behavior—diagnostic emission, `stderr` flushing, abort, and malformed-host-output handling—without TDD evidence. Add subprocess/death tests with controlled fake results and verify the emitted message before the abnormal exit.

## Nits

None.

## Verified

Commands were run from `C:\Users\Roma\Dev\PictureView3\pvdkit`, with no network access. The requested isolated configuration succeeded:

```text
$env:PVDKIT_BUILD_SUFFIX='-review'
rtk cmake --preset debug
-- pvdkit plugins: avif;rpgmvp
-- Configuring done
-- Generating done
-- Build files have been written to: .../build/debug-review
```

The requested build used six jobs and completed with no compiler warnings:

```text
$env:PVDKIT_BUILD_SUFFIX='-review'
rtk cmake --build --preset debug --parallel 6
[16/25] Linking CXX executable tests\pvd\pvd_tests.exe
(exit code 0; no warning diagnostics)
```

The requested debug test run passed, including `guard_tests` and both sequence executables:

```text
$env:PVDKIT_BUILD_SUFFIX='-review'
rtk ctest --preset debug
ctest: 15/15 passed (49.17 sec)
slowest: avif_leak_tests 23.48 sec; rpgmvp_leak_tests 18.56 sec; guard_tests 2.15 sec
```

Direct sequence runs confirm the implementer's assertion counts:

```text
rtk proxy build/debug-review/plugins/avif/tests/e2e/avif_sequence_tests.exe
[doctest] assertions: 121 | 121 passed | 0 failed |

rtk proxy build/debug-review/plugins/rpgmvp/tests/e2e/rpgmvp_sequence_tests.exe
[doctest] assertions: 101 | 101 passed | 0 failed |
```

The requested coverage command also used six build jobs and passed the production-source gate:

```text
$env:PVDKIT_BUILD_SUFFIX='-review'
$env:CMAKE_BUILD_PARALLEL_LEVEL='6'
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
100% tests passed out of 15
Total Test time (real) = 53.49 sec
plugins\avif\src\DefaultPlugin.cpp       24/24 lines (100.00%), 0 branches
plugins\rpgmvp\src\DefaultPlugin.cpp     22/22 lines (100.00%), 0 branches
TOTAL                                  1561/1561 lines (100.00%), 390/390 branches (100.00%)
Coverage source completeness passed: 20 executable source files present.
Plugin profile check passed for 'avif': 18/18 Exports.cpp functions executed.
Plugin profile check passed for 'rpgmvp': 18/18 Exports.cpp functions executed.
Coverage gate passed: lines 100%, branches 100%.
```

A focused report on the new, normally excluded test-support sources exposed the missing failure-path coverage:

```text
rtk powershell -NoProfile -Command "& '<LLVM>/llvm-cov.exe' report '<build>/avif_sequence_tests.exe' '-instr-profile=build/coverage-review/coverage.profdata' 'tests/support/sequence/SequenceDriver.cpp' 'tests/support/sequence/SequenceDriverTests.cpp'"
SequenceDriver.cpp        lines 92.86%    branches 86.67%
SequenceDriverTests.cpp   lines 98.33%    branches 68.75%
```

`rtk git diff --check` produced no output. Inspection also confirms `makePlugin()` still delegates to `defaultOptions()` in both composition roots, the supplied `DecoderOptions` is copied into `CodecPlugin`, and `invariantViolation` calls `std::fflush(stderr)` before `std::abort()`; the missing death test is finding 7. Per the review instructions, x86, Release/import/export checks, ASan, and lint were not run.

## Verdict

REJECT

## Round 2

### Substantive findings

1. `tests/support/sequence/SequenceDriverTests.cpp:242` — The child installs `exitAfterAbortSignal` in every configuration; only the profile write inside that handler is conditional on `PVDKIT_COVERAGE`. Consequently the Debug and Release death tests intercept the `SIGABRT` raised by `std::abort()` and replace the default abort termination with `_Exit(3)`, so the production-style tests do not prove the real abort path. Compile and register this handler only for coverage builds, and leave `SIGABRT` at its default disposition in all other builds.
2. `tests/support/sequence/SequenceDriverTests.cpp:102` — The timeout and error paths after `CreateProcessW` are not resource-safe. A `REQUIRE` on `WaitForSingleObject`, `GetExitCodeProcess`, capture-file opening, or another later operation can leave `hProcess` open; on timeout it can also leave the child running, and every such early exit bypasses the capture-file removal at lines 111–113. This fails the requirement that the death test not leave child processes or temporary files when the test itself fails. Own both process handles and the capture path with unconditional cleanup, and terminate then reap the child before reporting a timeout.

### Nits

None.

### Verified

Commands were run from `C:\Users\Roma\Dev\PictureView3\pvdkit` with no network access. The requested x64 Debug configure and six-job build succeeded with no warning diagnostics:

```text
$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --preset debug
-- pvdkit plugins: avif;rpgmvp
-- Configuring done
-- Generating done
-- Build files have been written to: .../build/debug-review

$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --build --preset debug --parallel 6
[14/15] Linking CXX executable plugins\rpgmvp\tests\e2e\rpgmvp_sequence_tests.exe
(exit code 0; no warning diagnostics)
```

The requested full test preset passed:

```text
$env:PVDKIT_BUILD_SUFFIX='-review'; rtk ctest --preset debug
ctest: 15/15 passed (49.18 sec)
slowest: avif_leak_tests 23.83 sec; rpgmvp_leak_tests 18.43 sec; guard_tests 2.22 sec
```

Direct sequence runs passed and exercised the new checks:

```text
rtk proxy build\debug-review\plugins\avif\tests\e2e\avif_sequence_tests.exe --no-colors=true
test cases: 6/6 passed; assertions: 372/372 passed

rtk proxy build\debug-review\plugins\rpgmvp\tests\e2e\rpgmvp_sequence_tests.exe --no-colors=true
test cases: 6/6 passed; assertions: 297/297 passed
```

The implementer's profile and executable were present, so coverage was not rebuilt. The requested focused report is complete:

```text
rtk proxy powershell -NoProfile -Command "& '<LLVM>/llvm-cov.exe' report 'build/coverage-t11a/plugins/avif/tests/e2e/avif_sequence_tests.exe' '-instr-profile=build/coverage-t11a/coverage.profdata' 'tests/support/sequence/SequenceDriver.cpp' 'tests/support/sequence/SequenceDriverTests.cpp'"
SequenceDriver.cpp       194/194 lines (100.00%), 76/76 branches (100.00%)
SequenceDriverTests.cpp  218/218 lines (100.00%), 16/16 branches (100.00%)
TOTAL                    412/412 lines (100.00%), 92/92 branches (100.00%)
```

Inspection confirms findings 1, 4, 5, and 6 from round 1 are otherwise closed: bottom-up rows map to `(height - 1 - row) * |pitch|` after checked full-extent arithmetic, and tests cover both pitch signs plus simulated 32-bit and end-address overflow; failed decodes neither inspect `pvdInfoDecode` nor call `pageFree`; every opened fixture checks `decoded + aborted + failed == min(pages, 4)`, ordinary fixtures require zero failures, only plugin-listed decode-failure fixtures permit failures, and `AbortPolicy{1, 1}` proves aborts. Per the orchestrator's decisions on findings 2 and 3, `SequenceDriver.hpp` now explicitly scopes the driver to pvdkit's stricter `pvd::Shim` output contract and documents the bounded string scan. The expectation tables are plugin-local, a case-insensitive search for `avif|rpgmvp` in shared sequence/CMake code returned no matches, and the existing e2e assertion bodies are unchanged apart from consuming those shared plugin-local tables. Requiring `SEQUENCE_EXPECTATIONS` in `pvdkit_add_plugin_e2e_tests` is the correct boundary because that helper unconditionally registers `pvdkit_add_plugin_sequence_tests`; both layers issue explicit configure-time errors for a missing source.

The passing death-test run left no matching `pvdkit-sequence-death-*.txt` file and no sequence-test child process, but source inspection exposes the failure-path leaks in finding 2. Per the review instructions, x86, ASan, lint, Release, and coverage rebuilds were not run.

`rtk git diff --check` completed with no output.

### Verdict

REJECT

## Round 3

### Substantive findings

### Nits

None.

### Verified

`tests/support/sequence/SequenceDriverTests.cpp` closes both Round 2 findings. The
`exitAfterAbortSignal` definition is inside `#if PVDKIT_COVERAGE` at line 118, and its only
`std::signal(SIGABRT, ...)` installation is inside the same guard at line 332. The Release compile
commands contain no `PVDKIT_COVERAGE` definition, so the requested sequence run exercised the real
default abort disposition.

`CaptureFile` owns the capture path and unconditionally attempts `std::filesystem::remove` from its
destructor. `ChildProcess` immediately adopts both `PROCESS_INFORMATION` handles into `UniqueHandle`
members, its destructor calls `stop()`, and `stop()` calls `TerminateProcess` followed by
`WaitForSingleObject` before member destruction closes the handles. The only remaining raw handle
values are the transient `CreateProcessW` outputs and non-owning arguments passed directly to Win32;
no raw owning handle remains. In `runDeathChild()`, the bounded wait is followed by `child.stop()`
before `test::require(waitResult == WAIT_OBJECT_0)`, so timeout and wait-error assertion paths first
terminate and reap the child. Subsequent assertion failures also unwind both RAII owners, closing
both process handles and removing the capture file.

The requested x64 Release configure and six-job build completed successfully with no warning
diagnostics:

```text
$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --preset release
-- pvdkit plugins: avif;rpgmvp
-- Configuring done (4.5s)
-- Generating done (0.2s)
-- Build files have been written to: C:/Users/Roma/Dev/PictureView3/pvdkit/build/release-review

$env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --build --preset release --parallel 6
[28/35] Linking CXX executable plugins\avif\tests\e2e\avif_sequence_tests.exe
[34/35] Linking CXX executable plugins\rpgmvp\tests\e2e\rpgmvp_sequence_tests.exe
(exit code 0; no warning diagnostics)
```

The requested focused Release test run passed with the non-coverage binaries:

```text
$env:PVDKIT_BUILD_SUFFIX='-review'; rtk ctest --preset release -R sequence
ctest: 2/2 passed (0.63 sec)
slowest:
  avif_sequence_tests 0.34 sec
  rpgmvp_sequence_tests 0.27 sec
```

Per the narrow review instruction, no other build, test, coverage, lint, architecture, or ABI gate
was run.

### Verdict

ACCEPT
