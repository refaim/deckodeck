# Review: Task 20, fix round 1 — sRGB output tables shared per module, band split in `Presentation`

Fresh review of the uncommitted working tree on top of `2c5e42b` (scope: `src/core/colour/Pipeline.{hpp,cpp}`,
`src/core/FileSession.{hpp,cpp}`, `tests/core/colour/PipelineTests.cpp`, `tests/core/FileSessionTests.cpp`,
`plugins/avif/tests/e2e/E2eTests.cpp`, `docs/ARCHITECTURE.md`, `plugins/avif/DESIGN.md`). Reviewer build suffix
`-review`, x64: `debug`, `release`, `coverage`, `asan`, lint. Task 21's version edits and the known x86
`leakcheck_tests` flake were not reviewed. Nothing in the repository was modified, staged or reset; this file is the
only thing written.

## Substantive findings

None.

Each of the nine items was checked against the code and my own build, not the report:

1. **Process-wide `static const SrgbOutputTables`** (`Pipeline.cpp:98-105`). Thread-safe one-time initialisation is
   real: `llvm-nm --undefined-only` on `Pipeline.cpp.obj` shows `_Init_thread_header`, `_Init_thread_footer`,
   `_Init_thread_epoch` (the MSVC-ABI guarded-static sequence) and **no `atexit`**, so the object is trivially
   destructible as claimed and registers nothing to run at `pvdExit`/`FreeLibrary`. The object itself is
   `?tables@...` in `.bss` (`b`), with its guard at offset `0x60000` = 384 KiB — no heap block, nothing for the
   leak gate to charge (LoadLibrary/FreeLibrary ×20 and the 8-thread scenarios pass in debug, release, coverage
   and asan). The only `operator new` references in the TU are the pre-existing `make_unique<TransferLut>` and the
   `vector<jthread>`. Lifetime: static storage in the module that owns every `Presentation`; `Presentation` holds
   `const SrgbOutputTables &` (`Pipeline.hpp:82`) and deletes copy and move (`:50-53`, ARCHITECTURE §2). Release
   `AVIF.pvd` still imports `KERNEL32.dll` only and exports the eight bare names. A grep for non-`constexpr`
   `static` under `src/**` and `plugins/*/src/**` finds exactly this one line, so §7's "the single piece of
   process-wide state" is true of the tree.
2. **O(n) merge pass** (`Pipeline.cpp:68-77`). I compiled the production `Pipeline.cpp` verbatim with the release
   flags into a scratch program (private members exposed via a patched header copy outside the repo) and rebuilt
   the bucket table with the former construction — `upper_bound` over the thresholds for each of the 65,537
   boundaries: **0 of 65,537 entries differ**, on x64 and on an i686 cross-build. Thresholds are strictly
   ascending (0 non-ascending pairs; first `5.90519392e-07`, last `0.999982774`), which is the precondition the
   merge relies on and which `quantize` needs anyway.
3. **Fewer `exactQuantize` calls per threshold** (`Pipeline.cpp:40-59`). The ascent returns the first float that
   reaches `code`, whose predecessor was already seen to fall short; the descent is unchanged. To check exactness
   without trusting the search at all, the same scratch program scanned every one of the 1,065,353,217 floats in
   [0, 1] through the pre-Task-20 `pow`-plus-`lround` path and recorded the first float reaching each code:
   **0 of 65,535 thresholds differ** from the table, 0 monotonicity violations of the old path, on both
   architectures. The in-tree exhaustive diagnostic agrees (below).
4. **`Presentation::applyImage` band split** (`Pipeline.cpp:182-210`). Workers are started only for bands
   `0..n−2` (`:202-208`, `reserve(bandCount − 1)`), the last band runs inline (`:209`). Row arithmetic: the first
   `n−1` bands take `(n−1)·(height/n) + (height mod n)` rows (since `height mod n < n`), so the inline band gets
   exactly `height/n ≥ 1` rows — every row covered once, subspans disjoint, the last one ending at
   `pixels.size()`. `std::vector<std::jthread>` joins on the normal path and on unwinding; `applyImage` is
   correctly *not* `noexcept` (a `jthread` constructor can throw), the worker lambda is `noexcept` over the
   `const noexcept` `apply`, and the only mutable state touched is the caller's own band. No `static`/`mutable`
   exists anywhere in `src/core/colour`, so the shared `Presentation`, `Eetf`, matrices and LUT are read-only.
   `FileSession::decodePage` passes `buffer.bytes()/pitchBytes()/height()` consistently (`FileSession.cpp:107`).
   Pinned by `PipelineTests.cpp:229-267` (512×513 for `maxThreads` 0/1/2/3/4/8 vs the serial pass, plus 16×16
   and 262,144×1 staying inline), `FileSessionTests.cpp:307-330` (0/1/2/8 through the session on a fake decoder
   that fills distinct pixel ids) and the two whole-image DLL hashes. asan preset: 17/17.
5. **NaN guard** (`Pipeline.cpp:83`): `if (!(linear > 0.0F)) return 0;` — the branch count is unchanged and
   `quantize(NaN)` = 0 (my probe and `PipelineTests.cpp:215`); `−0`, `−1`, `−∞`, `denorm_min`, `min`,
   `nextafter(1, 0)`, `1`, `2`, `+∞` are all pinned against the exact path.
6. **Skipped diagnostics**: six `doctest::skip()` cases (four timing rows, construction, the exhaustive proof).
   CTest invokes `core_tests.exe` bare and its log records `87 passed | 0 failed | 6 skipped` — visible, not
   hidden. Under `--no-skip=true` they assert for real (`CHECK(totalMismatches == 0)`,
   `CHECK(totalDecreases == 0)`). The two e2e timing cases are not skipped and carry functional `REQUIRE`s.
7. **ARCHITECTURE wording**: §3.7 and the `FileSession` bullet now say `min(clamp(maxThreads, 1, 4), height)`
   with `maxThreads == 0` meaning one band, the last band on the calling thread; §7 records the static, its
   properties (each verified above) and why it is not injected. `plugins/avif/DESIGN.md` matches.
8. `E2eTests.cpp:441-444` carries the x86-hash comment. 9. Report claims: every `file:line` in the "Fix round 1"
   section resolves to what it describes; every number I re-measured reproduces (below). No new `pragma`,
   `NOLINT`, `catch (`, `reinterpret_cast`, `#ifdef` or guard token in the diff; `git diff --check` is clean.

## Nits

1. `src/core/colour/Pipeline.cpp:103` — §7 and the comment lean on "trivially destructible, no atexit entry"; that
   is true today (verified in the object file) but nothing pins it. A
   `static_assert(std::is_trivially_destructible_v<SrgbOutputTables>);` next to the static would make a future
   member with a destructor (a `std::vector`, say) a compile error instead of a silent `atexit` registration in
   every plugin DLL's CRT.
2. `tests/core/colour/PipelineTests.cpp:187-209` — the "across threads" half pins sharing after the main thread
   has already initialised the static; a concurrent *first* initialisation cannot be observed in-process (a
   function-local static initialises once per process). One clause in the case's comment saying that the
   race-freedom rests on [stmt.dcl] (and the `_Init_thread_*` sequence clang emits) would stop a reader taking it
   for a race test.
3. `tests/core/colour/PipelineTests.cpp:153-155` — the exhaustive proof is per machine: the UCRT dispatches `pow`
   by CPU (FMA3 paths on x64), so a user's machine may build thresholds that differ by ULPs from this one's — and
   its exact path differs the same way, which is precisely why building the tables at run time from the same
   `exactQuantize` is the right choice. The property that keeps them equal there is the monotonicity the
   diagnostic counts; one sentence saying so would record why the count is part of the proof.

## Verified

Environment: `PVDKIT_BUILD_SUFFIX=-review`, `--parallel 6`, one build/lint at a time, x64 only.

```
$ cmake --preset debug && cmake --build --preset debug --parallel 6      → 20 steps, no warnings, exit 0
$ ctest --preset debug --parallel 6 --output-on-failure
100% tests passed out of 17    Total Test time (real) =  39.75 sec
$ build/debug-review/tests/core/core_tests.exe
[doctest] test cases:     87 |     87 passed | 0 failed | 6 skipped
[doctest] assertions: 332504 | 332504 passed | 0 failed |

$ cmake --preset release && cmake --build --preset release --parallel 6  → 20 steps, no warnings, exit 0
$ ctest --preset release --parallel 6 --output-on-failure
100% tests passed out of 21    Total Test time (real) =  18.56 sec   (avif/rpgmvp check_imports + check_exports included)
$ llvm-readobj --coff-imports build/release-review/plugins/avif/AVIF.pvd | grep Name:   → KERNEL32.dll only
$ llvm-readobj --coff-exports ...                                                        → the eight bare pvd* names

$ llvm-nm --undefined-only build/debug-review/src/core/CMakeFiles/pvdkit_core.dir/colour/Pipeline.cpp.obj
         U ??2@YAPEAX_K@Z            (operator new: make_unique<TransferLut>, vector<jthread>)
         U _Init_thread_epoch
         U _Init_thread_footer
         U _Init_thread_header       (no atexit)
$ llvm-nm ... | grep tables
00000000 b ?tables@?1??srgbOutputTables@...   (.bss)
00060000 b ?$TSS0@?1??srgbOutputTables@...    (guard at 384 KiB)
```

Coverage (`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/coverage.ps1 -Preset coverage`,
`CMAKE_BUILD_PARALLEL_LEVEL=6`, `CTEST_PARALLEL_LEVEL=6`; `ctest --preset coverage` re-run alone for the summary):

```
100% tests passed out of 17    Total Test time (real) =  47.14 sec
src\core\FileSession.cpp        82 0 100.00%  10 0 100.00%  102 0 100.00%  48 0 100.00%
src\core\colour\Pipeline.cpp   105 0 100.00%  18 0 100.00%  170 0 100.00%  64 0 100.00%
src\core\colour\ToneMap.cpp     19 0 100.00%   6 0 100.00%   41 0 100.00%   6 0 100.00%
src\core\colour\Transfer.cpp    68 0 100.00%  18 0 100.00%  118 0 100.00%  84 0 100.00%
TOTAL                         1090 0 100.00% 261 0 100.00% 2153 0 100.00% 692 0 100.00%
Plugin profile check passed for 'avif': ... 18/18 Exports.cpp functions executed   (same for 'rpgmvp')
Coverage source completeness passed: 24 executable source files present.
Coverage gate passed: lines 100%, branches 100%.
```

Lint (`scripts/lint.ps1 -BuildDir build/debug-review -ReleaseDir build/release-review -Jobs 6`):

```
clang-format: 0 finding(s) in 0.6 s
clang-tidy: 0 finding(s) in 322.1 s
cppcheck: 0 finding(s) in 1.7 s
PSScriptAnalyzer: 0 finding(s) in 4.4 s
BinSkim: 0 finding(s) in 1.1 s
lint: clean
```

ASan (`cmake --preset asan && cmake --build --preset asan --parallel 6 && ctest --preset asan --parallel 6`):

```
100% tests passed out of 17    Total Test time (real) =  54.10 sec
```

In-tree diagnostics and hashes (x64 Release):

```
$ core_tests.exe --test-case="diagnostic: exhaustive*" --no-skip=true
exhaustive [0, 1]: 1065353217 floats, 0 mismatches, 0 exact-path monotonicity violations   (2/2 assertions)
$ core_tests.exe --test-case="Presentation release timing: construction" --no-skip=true
SrgbOutputTables construction (once per module): best 5.255 ms, mean 5.52026 ms over 10 constructions
Presentation(P3/PQ 1000 nit) construction: first 7.3455 ms, then best 1.799 ms, mean 1.86281 ms over 9 further constructions
$ core_tests.exe --test-case="Presentation release timing: scalar 1024x428,Presentation release timing: four bands*" --no-skip=true
1024x428:  median 176.638 ns/pixel,  77.4153 ms (scalar)
1024x428:  median  46.693 ns/pixel,  20.4642 ms (four bands)
4000x3000: median  45.4553 ns/pixel, 545.464 ms (four bands)
$ avif_e2e_tests.exe --test-case="HDR AVIF is presented as 64-bit sRGB through the DLL"
[doctest] test cases: 1 | 1 passed | 0 failed      assertions: 58 | 58 passed   (both FNV hashes unchanged)
$ avif_e2e_tests.exe --test-case="cosmos per-file*"
cosmos per file (open + decode + free + close): first 51239 us, then mean 36190 us over 5 further views
$ avif_e2e_tests.exe --test-case="cosmos deep decode timing*"
cosmos pvdPageDecode: nBPP=64, mean=25968 us (5 iterations after one warm-up)
```

Independent table check (scratchpad program; production `Pipeline.cpp`/`Transfer.cpp`/`Primaries.cpp`/`ToneMap.cpp`
compiled verbatim with `/O2 /Ob2 /DNDEBUG -MT /clang:-std=c++23 /W4 /permissive- /utf-8 /EHsc /Zc:preprocessor
/guard:cf`, private members exposed through a patched header copy outside the repo):

```
x64:
first srgbOutputTables(): 5.510 ms
thresholds non-ascending pairs: 0 (of 65534); first=5.90519392e-07 last=0.999982774
bucket entries differing from binary-search construction: 0 (of 65537)
thresholds differing from the independent full-float scan of the old path: 0 (of 65535); old-path monotonicity violations: 0; 2.6 s
quantize(NaN)=0 quantize(-0)=0 quantize(-inf)=0 quantize(+inf)=65535 quantize(1)=65535 quantize(denorm_min)=0 old(denorm_min)=0

i686 (--target=i686-pc-windows-msvc, IMAGE_FILE_MACHINE_I386):
first srgbOutputTables(): 15.484 ms
thresholds non-ascending pairs: 0 (of 65534); first=5.90519392e-07 last=0.999982774
bucket entries differing from binary-search construction: 0 (of 65537)
thresholds differing from the independent full-float scan of the old path: 0 (of 65535); old-path monotonicity violations: 0; 6.8 s
quantize(NaN)=0 quantize(-0)=0 quantize(-inf)=0 quantize(+inf)=65535 quantize(1)=65535 quantize(denorm_min)=0 old(denorm_min)=0
```

Static checks by reading: the merge pass's `buckets_[b]` = count of thresholds `<= b/65536` is by definition the
index `upper_bound` returns on sorted input; in `quantize`, `bucket = floor(linear·65536)` gives
`b/65536 ≤ linear < (b+1)/65536` exactly (power-of-two arithmetic), so the narrowed `upper_bound` window can neither
miss nor include a wrong threshold; `linear < 1.0F` bounds `bucket ≤ 65535`, so `buckets_[bucket + 1]` is in range.
`FileSession` constructs the `Presentation` only when `needed()` (`FileSession.cpp:44-47`) and
`presentationNeeded = presentation_ != nullptr` (`:90`) is therefore equivalent to the former `needed(meta.cicp)`.
Both compositions pass `max(1, hardware_concurrency())` as `maxThreads`, so the band split is live in production.
Both `E2eTests.cpp` timing cases run in every ctest and only print.

Not verified here: the x86 CMake presets (`debug-x86`, `release-x86`, `coverage-x86`) and the x86 lint run — one
architecture at a time was the budget; the quantiser's tables and NaN behaviour were verified on i686 through the
cross-compiled scratch program above, and the band split is architecture-neutral.

## Verdict

`ACCEPT` — the substantive finding of round 1 is fixed as instructed: the sRGB output tables are one guarded,
trivially destructible, heap-free function-local static per module, borrowed by reference, built in 5.3 ms once
instead of 7.7 ms per session, with contents proven identical to the former construction and to an independent
derivation on both architectures; the two image hashes and the 65,536-point grid are unchanged; the band split runs
the last band inline and joins on every path; all eight nits are done; coverage 100/100, lint clean, asan clean.
