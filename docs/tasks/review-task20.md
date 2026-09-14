# Review: Task 20 — exact HDR presentation acceleration

Reviewer build suffix: `-review` (x64 `debug`, `release`, `coverage`; lint x64). Working tree on top of
`2c5e42b`; Task 21 edits (version 1.2.0→1.1.0 in `plugins/avif/CMakeLists.txt`, `DefaultPluginTests.cpp`,
`E2eTests.cpp` version literals, READMEs, ChangeLogs) were not reviewed.

## Substantive findings

1. `src/core/FileSession.cpp:73-76` + `src/core/colour/Pipeline.cpp:87-97` — the whole `Presentation`
   construction now runs inside the `FileSession` constructor, i.e. inside `pvdFileOpen`, and it costs
   **10.8 ms best / 12.5 ms mean per session on x64 and 23.7 ms on x86** on this machine (Release, the
   plugin's compile flags: PQ transfer LUT 1.7 ms — pre-existing, previously paid per decode — plus the
   new sRGB output thresholds 5.5 ms and buckets 2.2 ms, x64). Neither the move nor the cost appears anywhere in
   `docs/tasks/report-task20.md`: the timing harness constructs the object outside the timed region,
   and the DLL case (`cosmos deep decode timing`) times `pvdPageDecode` only, after `openImage`.
   Why it matters: (a) contract — AGENTS.md rule 12 and the task's deliverable ("the timing table,
   before/after, per step") require the cost the change itself introduced to be measured; the honest
   per-file number for the cosmos fixture is ≈ 11 ms open + 20.6 ms apply ≈ 32 ms on x64 (or ≈ 37 ms
   with the 26 ms DLL decode) and ≈ 24 + 33 ≈ 57 ms on x86, not the reported 21 / 33 ms — and the
   open-time cost alone exceeds the task's 10 ms budget; (b) design — `outputThresholds_`/`outputBuckets_` (`Pipeline.cpp:39-54, 87-97`) do not depend
   on `Cicp` at all; they are constants of the sRGB OETF, yet every HDR/wide-gamut session rebuilds them
   (7.7 ms) and holds a private 384 KiB copy, including sessions the host opens only to read
   `ImageInfo`, and every prefetch/sliding-window open in Far. What to do: build the sRGB
   threshold/bucket tables once per process and let sessions borrow them by reference — either owned by
   the composition root through `CodecPlugin` (fits ARCHITECTURE §2 "injected by reference, outlives
   its users by construction") or, if the orchestrator accepts immutable pure-math process state, a
   function-local `static const` in `Pipeline.cpp`; keep only the CICP-dependent 65,536-entry transfer
   LUT per session. Then re-measure and add the open-time row to the table. Independently of where they
   live, the construction itself is avoidably slow: the bucket table does 65,537 binary searches where a
   single merge pass over the sorted thresholds is O(n) (2.2 ms → well under 0.1 ms), and
   `outputThreshold` calls the `pow`-based `exactQuantize` 2-5 times per code.

## Nits

1. `src/core/colour/Pipeline.cpp:160-167` — `quantize` casts `linear * 65536` to `std::size_t`
   without excluding NaN; for NaN both guards are false and the cast is UB (on x64 it yields
   0x8000000000000000 → out-of-bounds read of `outputBuckets_`). The former path returned 0 for NaN
   (`lround(NaN)` → `LONG_MIN` → 0 on this CRT). I traced every upstream stage (clamped LUT input,
   HLG OOTF guarded by `luminance > 0`, finite matrix, `applyMaxRgb` guarded by `driving > 0` with
   `mapNits` clamped) and NaN is unreachable today, so this is latent, but `if (!(linear > 0.0F)) return
   0;` keeps the old behaviour at zero cost and without adding a branch to cover.
2. `src/core/FileSession.cpp:38-51` — the calling thread only spawns and waits; the last band could
   run on it (one fewer thread per decode). Also, if a `std::jthread` constructor throws
   `std::system_error` mid-loop, the already-started bands are joined by unwinding (correct), but the
   remaining rows stay unconverted and the exception becomes `FALSE` for the host — a new failure mode
   the serial path did not have. Rare and firewall-covered; running the last band inline and starting
   workers only for the others reduces the exposure. (A `catch` fallback is not an option: the guard
   forbids `catch (` outside `Firewall.hpp`.)
3. `src/core/colour/Pipeline.cpp:17-18, 167-172` — the per-pixel working set is now 640 KiB of tables
   (256 KiB LUT + 256 KiB thresholds + 128 KiB buckets), which is larger than most L2 caches. A
   4,096-bucket table (8 KiB) bounds the `upper_bound` window to ≈16 thresholds (4 probes) and would
   likely be faster than the 65,536-bucket one; worth one measurement. The threshold step bought only
   192 → 178 ns/pixel; the four `powf` calls in `Eetf::mapNits` remain the cost, as the report says.
4. `tests/core/colour/PipelineTests.cpp:26-45` — `applyBands` is a test-side copy of
   `FileSession::applyPresentation`; the "four bands" timing rows measure this copy, not the production
   band splitter (the e2e DLL case does measure production). Say so in the report or time through
   `FileSession`.
5. `tests/core/colour/PipelineTests.cpp:115-156` — the 4,096-entry interpolation experiment lives on as
   a permanently skipped test whose only assertion is `CHECK(maximumTargetNitsError >= 0.0F)`. Its
   numbers are already in the report; either drop it or name it a diagnostic in the case title so nobody
   reads it as a gate.
6. Exactness evidence in the tree is the 65,536-point linear grid (`PipelineTests.cpp:221-246`) plus
   two image hashes; the domain `quantize` actually sees is every float in (0, 1). I ran the production
   `Pipeline.cpp` (same release flags, same CRT) against `exactQuantize` over all 1,065,353,217 floats in
   [0, 1], on x64 and on an i686 cross-build: 0 mismatches on both, thresholds strictly increasing,
   `exactQuantize` monotone, 4.5 s / 7.8 s on four threads. Adding that as a skipped diagnostic next
   to the timing cases would keep the proof in the repository rather than in a review.
7. `docs/ARCHITECTURE.md:449` says "at most `min(maxThreads, 4)` bands"; the code is
   `min(clamp(maxThreads, 1, 4), height)` — `maxThreads == 0` (the `DecoderOptions` default) means one
   band. Write the clamp into the sentence.
8. `plugins/avif/tests/e2e/E2eTests.cpp:444` — the x86 cosmos hash is selected by
   `sizeof(std::size_t) == 8`; a one-line comment that the difference is in libavif's x86 YUV→RGB
   output (verified against the pre-Task-20 pipeline, per the report), not in the presentation, would
   stop the next reader from suspecting the quantiser.

## Verified

Build/test (x64, suffix `-review`):

```
$ cmake --preset debug            → Build files have been written to: build/debug-review
$ cmake --build --preset debug --parallel 6   → 31 steps, zero warnings (/W4 /WX), exit 0
$ ctest --preset debug --parallel 6 --output-on-failure
100% tests passed out of 17    Total Test time (real) =  46.40 sec
$ build/debug-review/tests/core/core_tests.exe
[doctest] test cases:     84 |     84 passed | 0 failed | 5 skipped
[doctest] assertions: 332481 | 332481 passed | 0 failed |
$ cmake --preset release && cmake --build --preset release --parallel 6   → zero warnings, exit 0
$ ctest --preset release --parallel 6 --output-on-failure
100% tests passed out of 21    Total Test time (real) =  21.33 sec   (check_imports/check_exports included)
```

Coverage (`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/coverage.ps1 -Preset coverage`,
`CMAKE_BUILD_PARALLEL_LEVEL=6`, `CTEST_PARALLEL_LEVEL=6`):

```
100% tests passed out of 17    Total Test time (real) =  57.34 sec
src\core\FileSession.cpp        95 0 100.00%  12 0 100.00%  127 0 100.00%  56 0 100.00%
src\core\colour\Pipeline.cpp    80 0 100.00%  13 0 100.00%  133 0 100.00%  50 0 100.00%
src\core\colour\ToneMap.cpp     19 0 100.00%   6 0 100.00%   41 0 100.00%   6 0 100.00%
src\core\colour\Transfer.cpp    68 0 100.00%  18 0 100.00%  118 0 100.00%  84 0 100.00%
TOTAL                         1078 0 100.00% 258 0 100.00% 2141 0 100.00% 686 0 100.00%
Coverage source completeness passed: 24 executable source files present.
Coverage gate passed: lines 100%, branches 100%.
```

Lint (`scripts/lint.ps1 -BuildDir build/debug-review -ReleaseDir build/release-review -Jobs 6`):

```
clang-format: 0 finding(s) in 0.7 s
clang-tidy: 0 finding(s) in 345.5 s
cppcheck: 0 finding(s) in 1.5 s
PSScriptAnalyzer: 0 finding(s) in 4.4 s
BinSkim: 0 finding(s) in 1.0 s
lint: clean
```

Timing harnesses (x64 Release, my build; the report's numbers reproduce):

```
$ core_tests.exe --test-case="Presentation release timing:*" --no-skip=true
1024x428:  median 179.008 ns/pixel,   78.4544 ms (scalar)
4000x3000: median 178.541 ns/pixel, 2142.49 ms (scalar)
1024x428:  median  47.041 ns/pixel,   20.6167 ms (four bands)
4000x3000: median  45.343 ns/pixel,  544.116 ms (four bands)
$ avif_e2e_tests.exe --test-case="cosmos deep decode timing is reported without enforcing a performance gate"
cosmos pvdPageDecode: nBPP=64, mean=25721 us (5 iterations after one warm-up)
```

Review diagnostics (scratchpad programs compiled with the release compile line of `Pipeline.cpp`:
`/O2 /Ob2 /DNDEBUG -MT /clang:-std=c++23 /W4 /permissive- /utf-8 /Zc:preprocessor /guard:cf`, the
production `Pipeline.cpp`/`Transfer.cpp`/`Primaries.cpp`/`ToneMap.cpp` included verbatim, private
members exposed through a patched header copy outside the repo):

```
Presentation ctor (P3/PQ 1000 nit): best 10.751 ms, mean 12.521 ms over 10 runs
Presentation ctor (BT.2020/HLG): 11.061 ms
best of 10: PQ transfer LUT 1.73 ms; output thresholds 5.52 ms; buckets 2.20 ms
thresholds non-increasing pairs: 0 (of 65534)
thresholds not a local crossing: 0
exhaustive [0,1]: 1065353217 floats, 0 mismatches, 0 reference monotonicity violations, 4.5 s
outside-[0,1] probes: 0 mismatches      (−0, negatives, >1, ±inf, denorm_min, min)
NaN: reference=0 (new quantize on NaN not evaluated: UB cast)
```

The same program cross-compiled with `--target=i686-pc-windows-msvc` (the flags of
`cmake/clang-cl-x86.toolchain.cmake`), run on this machine:

```
Presentation ctor (P3/PQ 1000 nit): best 23.676 ms, mean 24.257 ms over 10 runs
Presentation ctor (BT.2020/HLG): 18.862 ms
thresholds non-increasing pairs: 0 (of 65534)
thresholds not a local crossing: 0
exhaustive [0,1]: 1065353217 floats, 0 mismatches, 0 reference monotonicity violations, 7.8 s
outside-[0,1] probes: 0 mismatches
```

Vectorisation remarks (`/clang:-Rpass=loop-vectorize /clang:-Rpass-missed=loop-vectorize` on
`Pipeline.cpp`, same flags): `loop not vectorized` at lines 45, 49, 82, 113, 136 — matches the report; no
loop vectorised.

Static checks done by reading: `exactQuantize` is the former `quantize` verbatim (`git show
2c5e42b:src/core/colour/Pipeline.cpp`, only `numeric_limits<uint16_t>::max()` → the same-typed
`kCodeMaximum`); `convert` unchanged apart from the member call; band rows sum to `height`, spans are
disjoint and end exactly at `bytes.size()`; `Presentation::apply` touches only `const` state, is
`const noexcept`, and the `Presentation` outlives the workers (owned by the session, workers joined by
`std::jthread` destructors on the normal path and on unwinding); the FNV hash is computed over
`DecodedPage::pixels()`, which copies every row from the DLL's `pImage` (`tests/e2e/PluginHost.cpp:130-155`);
the five `doctest::skip()` cases are not run by CTest (`core_tests` reports "5 skipped") and contain no
assertion whose failure could be masked. `<thread>` in `src/core` is std-only; no guard tokens added.

Not verified: the x86 CMake presets (`debug-x86`, `release-x86`, `coverage-x86`, the x86 lint run) and
`asan` — one architecture, one build at a time was the budget; the report's claim that the x86 cosmos
hash equals the pre-Task-20 pipeline's output rests on the implementer's temporary rebuild (the
quantiser itself is proven exact on i686 above, and the band split is architecture-neutral).

## Verdict

`REJECT` — one substantive finding: the presentation tables are rebuilt per session inside
`pvdFileOpen` at an unreported ≈ 11 ms on x64 / ≈ 24 ms on x86 (most of it CICP-independent
constants), which the timing table must include and which should be built once. Everything else —
exactness (proven over every float on both architectures), thread and exception safety of the band
split, the hash tests, the skipped diagnostics, coverage, lint, warnings — checks out.
