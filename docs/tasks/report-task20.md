# Task 20 implementation report: exact HDR presentation acceleration

Date: 2026-09-14

Branch: `master`

Starting and final observed `HEAD`: `2c5e42b5bfdf408171710a10f466bb036fcf8eab`

Build suffix: `-t20`

No commit, staging operation, reset, checkout, worktree, network access, dependency installation, or
Far Manager operation was performed.

## Outcome

The exact presentation path is substantially faster, and its output is byte-identical to the
pre-optimization output, but the requested x64 target was **not met**. On the release timing harness,
the 1024x428 pass fell from 84.2194 ms to 20.9038 ms (4.029x, 75.2% less time), not to at most 10 ms
or at least 8x. The 12-megapixel pass fell from 2258.60 ms to 553.114 ms (4.083x). The direct x64
Release DLL test measured the complete warmed-up `pvdPageDecode` at 26.074 ms for the cosmos fixture.

Per the task's stop condition, work stopped after the exact output lookup, invariant hoisting, and
four-band implementation. No approximation was admitted to reach the target. A measured 4096-entry
linear-interpolated EETF follow-up is documented below, but exists only as a skipped diagnostic test.

## Implementation

- `Presentation` retains its exact 65,536-entry input-transfer LUT. The former per-channel sRGB
  `pow` and quantization call is replaced by 65,535 exact float decision thresholds. A 65,537-entry
  coarse table narrows `std::upper_bound`; it never interpolates an OETF result. Thresholds are
  corrected with `nextafter` until the former exact `linearToSrgb` plus `lround` path selects the
  intended code.
- `FileSession` constructs and owns one immutable `Presentation` when the image needs colour
  presentation, instead of reconstructing both LUTs inside every `decodePage` call.
- Images below 256 Ki pixels stay on the calling thread. Larger images use
  `min(clamp(maxThreads, 1, 4), height)` disjoint row bands and `std::jthread`. Each worker calls the
  existing `const noexcept` `Presentation::apply`; exceptions cannot cross a thread boundary.
- The architecture and AVIF design documents now record the presentation lifetime, exact output
  lookup, threshold, and thread policy.
- The existing AVIF ChangeLog HDR line was left alone; I chose not to add or fold in a separate
  `HDR-файлы открываются быстрее` claim because the specified performance target was not reached.

## Exactness and TDD evidence

The whole-image hash assertions were written before production optimization and first used zero as
the expected value. The targeted x64 Release run failed exactly the two new assertions and exposed
these baseline FNV-1a-64 hashes:

| HDR fixture | x64 baseline and final | x86 baseline and final |
|---|---:|---:|
| `colors_hdr_rec2020.avif` | `14703790622216699421` | `14703790622216699421` |
| `cosmos1650_yuv444_10bpc_p3pq.avif` | `5389512495027945087` | `15569853467021997067` |

The x86 cosmos hash is architecture-specific in libavif's decoded input. It was verified against a
temporary rebuild of the original exact presentation implementation before being pinned. Both
final architecture-specific hashes pass. The existing exact BGRA16 samples and the Task 16
FFmpeg/zscale comparison were not changed.

The deliberately red command was:

```powershell
rtk .\build\release-t20\plugins\avif\tests\e2e\avif_e2e_tests.exe --test-case="HDR AVIF is presented as 64-bit sRGB through the DLL" --no-version
```

It initially reported 1 failed case and 56/58 passing assertions. After pinning the baseline hashes,
the same test passed 1 case and 58/58 assertions. It continued to pass after every retained
optimization.

Additional tests cover:

- every one of the 65,536 linear 16-bit inputs against the former exact sRGB OETF and quantizer,
  checking B, G, R, and alpha: 262,144/262,144 assertions passed;
- one immutable `Presentation` shared by two threads over disjoint bands, byte-compared with a
  serial pass;
- `FileSession` output identity for `maxThreads` 0, 1, 2, and 8 on a 512x513 image, covering the
  single-thread clamp, two bands, four-band cap, and uneven row split;
- the two whole-image HDR hashes in the real AVIF DLL path.

The `std::pow` to `std::exp2(exponent * std::log2(value))` candidate was rejected before timing. Its
targeted HDR identity run changed both whole-image hashes to `6564224320236338391` and
`15128519939975213979`; two pinned cosmos channels also moved by -1. The original exact PQ math was
restored. Float intermediates and all EETF constants were already hoisted or stored as fields, so
there was no further invariant work to remove from the per-pixel EETF.

## Timing

The release-only doctest harness constructs one P3/PQ `Presentation`, performs one warm-up, then
runs each buffer 20 times and prints the median. Source-buffer restoration is outside the timed
region. The timing cases are skipped during normal CTest and enforce no performance gate.

| Retained step, x64 Release | 1024x428 ns/pixel | 1024x428 ms | 4000x3000 ns/pixel | 4000x3000 ms |
|---|---:|---:|---:|---:|
| Baseline exact Task 16 path | 192.162 | 84.2194 | 188.217 | 2258.60 |
| Exact output thresholds; scalar | 177.897 | 77.9674 | 179.428 | 2153.14 |
| Exact output thresholds plus four bands, final | 47.6960 | 20.9038 | 46.0928 | 553.114 |

Step 3 has no timing row because the `exp2(log2)` experiment failed the mandatory identity gate
before performance could determine whether it was admissible. The final x86 Release four-band
cosmos-sized result was 75.5922 ns/pixel, or 33.1300 ms. Direct warmed-up DLL timings, five
`pvdPageDecode` calls each, were 26.074 ms mean on x64 and 41.053 ms mean on x86.

The rows the review found missing — what a session pays inside `pvdFileOpen`, and the honest
per-file number — were measured in fix round 1 (harness cases `Presentation release timing:
construction` and `cosmos per-file open and decode timing ...`, Release, this machine, two or
three runs each; "before" is the tree the review saw, "after" is the fixed tree):

| Cost, Release | x64 before | x64 after | x86 before | x86 after |
|---|---:|---:|---:|---:|
| `Presentation` construction per session, best / mean of 9 | 9.48 / 9.6–10.5 ms | 1.74 / 1.8–2.0 ms | 23.77 / 24.2–24.4 ms | 5.37 / 5.4–5.7 ms |
| sRGB output tables, once per module (first HDR open) | inside every row above | 5.28 ms best, 5.4 mean | inside every row above | 15.26 ms best, 15.5 mean |
| First `Presentation` of the process (tables + transfer LUT) | 9.9–10.3 ms | 7.26–7.28 ms | 24.1–24.6 ms | 20.8–21.0 ms |
| cosmos per file: open + decode + free + close, mean of 5 | 44.8 / 45.3 / 52.6 ms | 37.4 / 37.8 / 37.9 ms | 73.3 / 73.7 ms | 55.0 / 56.2 / 56.4 ms |
| cosmos per file, the first view after `LoadLibrary` | 44.5 / 46.2 / 47.4 ms | 43.0 / 43.4 / 44.2 ms | 73.0 / 75.0 ms | 72.1 / 73.5 / 74.9 ms |
| cosmos `pvdPageDecode` alone, mean of 5 after warm-up | 26.1 / 26.2 / 26.6 ms | 26.5 / 26.5 / 27.0 ms | 38.3 / 40.2 ms | 39.0 / 39.5 / 39.9 ms |

The decode-only row is unchanged within noise, as it must be: the fix moves the table build out
of the session, it does not touch the per-pixel path. The per-file saving (≈ 7.5 ms on x64,
≈ 17.5 ms on x86) is the table build that every session used to pay.

The final harness commands were:

```powershell
rtk .\build\release-t20\tests\core\core_tests.exe --test-case="Presentation release timing:*" --no-skip=true --no-version
rtk .\build\release-x86-t20\tests\core\core_tests.exe --test-case="Presentation release timing: four bands 1024x428" --no-skip=true --no-version
rtk .\build\release-t20\plugins\avif\tests\e2e\avif_e2e_tests.exe --test-case="cosmos deep decode timing is reported without enforcing a performance gate" --no-version
rtk .\build\release-x86-t20\plugins\avif\tests\e2e\avif_e2e_tests.exe --test-case="cosmos deep decode timing is reported without enforcing a performance gate" --no-version
```

### Follow-up approximation measurement

A diagnostic 4096-entry EETF table over 0..1000 source nits was linearly interpolated and compared
with exact BT.2390 at every one of 65,536 uniformly spaced source levels. It changed 12,666 sRGB16
codes, with a maximum difference of 135 codes and a maximum target-luminance error of 0.0158639 nit.
It is therefore unsuitable for Task 20's identity contract and was not used in production. A
follow-up may benchmark this variant only if a quantified output-error budget is explicitly allowed.

```powershell
rtk .\build\release-t20\tests\core\core_tests.exe --test-case="follow-up 4096-entry EETF interpolation error" --no-skip=true --no-version
```

Result: 1/1 case and 1/1 assertion passed; the diagnostic values above were printed with `MESSAGE`.

## Vectorization diagnostics

The final exact TU was compiled separately with `/O2`, `/clang:-Rpass=loop-vectorize`,
`/clang:-Rpass-missed=loop-vectorize`, and `/clang:-Rpass-analysis=loop-vectorize`:

```powershell
rtk "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-cl.exe" /nologo -TP -IC:\Users\Roma\Dev\PictureView3\pvdkit\src /DWIN32 /D_WINDOWS /EHsc /O2 /Ob2 /DNDEBUG -MT /clang:-std=c++23 /W4 /WX /permissive- /utf-8 /EHsc /Zc:preprocessor /guard:cf -Wno-unused-command-line-argument /clang:-Rpass=loop-vectorize /clang:-Rpass-missed=loop-vectorize /clang:-Rpass-analysis=loop-vectorize /FoC:\Users\Roma\AppData\Local\Temp\pvdkit-t20-Pipeline-final.obj /c C:\Users\Roma\Dev\PictureView3\pvdkit\src\core\colour\Pipeline.cpp
```

The command exited 0. Clang reported no vectorized presentation loop. The BGRA16 loop at
`Pipeline.cpp:113` and byte bridge loop at `:136` were not vectorized because the `convert` call
cannot be vectorized and the loop iteration count could not be determined. The input LUT loop at
`:82` was blocked by `Transfer::toLinear`; exact threshold correction and `upper_bound` also have
unknown iteration counts. The exact scalar PQ/EETF calls therefore remain the limiting work, and
band parallelism is the retained speedup.

## Verification

All final build and test commands ran sequentially with `PVDKIT_BUILD_SUFFIX=-t20` and at most six
parallel jobs:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t20'
rtk cmake --build --preset debug --parallel 6
rtk ctest --preset debug --parallel 6 --output-on-failure
rtk cmake --build --preset release --parallel 6
rtk ctest --preset release --parallel 6 --output-on-failure
rtk cmake --build --preset debug-x86 --parallel 6
rtk ctest --preset debug-x86 --parallel 6 --output-on-failure
rtk cmake --build --preset release-x86 --parallel 6
rtk ctest --preset release-x86 --parallel 6 --output-on-failure
rtk cmake --build --preset asan --parallel 6
rtk ctest --preset asan --parallel 6 --output-on-failure
```

| Preset | Final result |
|---|---:|
| Debug x64 | 17/17 passed in 46.80 s |
| Release x64 | 21/21 passed in 20.94 s |
| Debug x86 | 17/17 passed in 64.83 s |
| Release x86 | 21/21 passed in 30.56 s |
| ASan x64 | 17/17 passed in 63.44 s |

All builds completed with zero compiler warnings. Guard, package-document, unit, adapter, e2e,
sequence, and leak tests are included in these results.

Coverage was run with six-way build and test parallelism through the environment because the script
owns its configure/build/CTest invocations:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t20'
$env:CMAKE_BUILD_PARALLEL_LEVEL='6'
$env:CTEST_PARALLEL_LEVEL='6'
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86
```

| Coverage preset | Tests | Regions | Functions | Lines | Branches |
|---|---:|---:|---:|---:|---:|
| x64 | 17/17 in 54.82 s | 1078/1078 | 258/258 | 2141/2141 | 686/686 |
| x86 | 17/17 in 85.11 s | 1078/1078 | 258/258 | 2141/2141 | 686/686 |

Both gates reported 100% lines and 100% branches, found all 24 executable production source files,
and verified 18/18 `Exports.cpp` functions through each instrumented plugin DLL.

Lint commands and final results:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t20'
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -BuildDir build\debug-x86-t20 -ReleaseDir build\release-x86-t20 -Jobs 6
```

| Architecture | clang-format | clang-tidy | cppcheck | PSScriptAnalyzer | BinSkim |
|---|---:|---:|---:|---:|---:|
| x64 | 0 in 0.6 s | 0 in 342.4 s | 0 in 1.9 s | 0 in 4.8 s | 0 in 1.1 s |
| x86 | 0 in 0.7 s | 0 in 339.6 s | 0 in 1.4 s | 0 in 4.1 s | 0 in 1.1 s |

Both ended with `lint: clean`.

The direct Release ABI checks were:

```powershell
rtk ctest --preset release -R "_check_(imports|exports)$" -V
rtk ctest --preset release-x86 -R "_check_(imports|exports)$" -V
```

x64 passed 4/4 in 1.28 s and x86 passed 4/4 in 1.25 s. Every plugin imports only
`KERNEL32.dll`. Every plugin exports exactly `pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`,
`pvdPageDecode`, `pvdPageFree`, `pvdPageInfo`, and `pvdPluginInfo` under bare names.

Task 20's paths pass `rtk git diff --check -- <Task-20 paths>` with no output. Whole-tree
`rtk git diff --check` exits 1 only for the two concurrently edited package ChangeLogs; Task 21's
byte-level checks document that their reported CR bytes are the required UTF-8-BOM/CRLF encoding.

## Release binaries

Hashes were generated after the final Release builds with PowerShell
`Get-FileHash -Algorithm SHA256`:

| Architecture/plugin | Absolute path | SHA-256 |
|---|---|---|
| x64 AVIF | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-t20\plugins\avif\AVIF.pvd` | `7462D2944D6189222C18E292F90D29E263A81CF6A2BA919264C4AABD82E0A575` |
| x64 RPGMVP | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-t20\plugins\rpgmvp\RPGMVP.pvd` | `36D53547DCA572BEE81BFF86EB11A5268FA3FC442CC24E58CF061511D603B61D` |
| x86 AVIF | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-x86-t20\plugins\avif\AVIF.pvd` | `63EF293BCE63C8FB3DB15758E1CB2A4FD836314C9E879BDAD8518933EB15C549` |
| x86 RPGMVP | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-x86-t20\plugins\rpgmvp\RPGMVP.pvd` | `DCE71FADC404E820EFE32653554F36AF60CED2A52E975D3132E3046BB393D42F` |

## Shared-tree version note

Task 20 did not change the requested AVIF 1.2.0 version. While this work was in progress, Task 21
concurrently changed `plugins/avif/CMakeLists.txt`, its adapter assertion, readmes, and package
ChangeLog to consolidate the release identity at 1.1.0. Its report explicitly left the two AVIF E2E
assertions for this task to reconcile; those two literals were changed to 1.1.0 so the shared final
tree passes. I did not overwrite Task 21's work. Consequently, the four binaries hashed above are
the shared tree's 1.1.0 artifacts, not 1.2.0 artifacts. This is the only version-state mismatch.

## What was not done

- The at-most-10-ms / at-least-8x presentation objective was not reached; exact output took
  20.9038 ms on the specified x64 harness.
- No approximate tone-map or OETF lookup was shipped. The measured follow-up violates bit identity.
- No separate ChangeLog performance claim was added.
- Far Manager was not run and `C:\Tools\FarManager` was not touched, so host-side visual/timing
  confirmation remains for Roma.
- No network, installation, commit, push, reset, checkout, worktree, or destructive command was used.

## Fix round 1 (2026-09-15) — acting on `docs/tasks/review-task20.md`

Same rules as above: suffix `-t20`, `--parallel 6`, one build at a time, no git state change, no
edit under `plugins/*/package/`, Task 21's version edits left exactly as found. Every gate was
re-run on the final tree; the outputs are quoted at the end of this section.

### Substantive finding 1 — sRGB output tables built per session

Decision applied (orchestrator): the threshold and bucket tables are pure math independent of
`Cicp`, so they are now built once per module, lazily, as a function-local `static const`, and every
`Presentation` borrows them by reference. Only the CICP-dependent 65,536-entry transfer LUT stays
per session.

TDD order: the tests below were written first and failed to compile against the old API
(`cmake --build --preset debug` reported `no member named 'applyImage'`, `use of undeclared
identifier 'SrgbOutputTables'` / `'srgbOutputTables'`, `no member named 'outputTables'`); the
production change followed.

- `src/core/colour/Pipeline.hpp:25-39` — new class `SrgbOutputTables` (65,535 exact thresholds,
  a 65,536-bucket index, `quantize(float) const noexcept`); `:43` — `srgbOutputTables()`, the
  shared instance; `:50-53` — `Presentation` now deletes copy and move (it holds a reference, per
  ARCHITECTURE §2); `:67` — `Presentation::outputTables()` exposes the borrowed object so a test can
  pin the sharing; `:82` — `const SrgbOutputTables &outputTables_`.
- `src/core/colour/Pipeline.cpp:98-105` — `srgbOutputTables()`: `static const SrgbOutputTables
  tables;` (built on first use, thread-safe by [stmt.dcl], trivially destructible, so no atexit
  entry and no heap block for the leak gate to see); `:126` — `Presentation` takes the reference in
  its mem-initializer list; `convert` calls `outputTables_.quantize`.
- Cheaper construction, as the reviewer suggested:
  - `Pipeline.cpp:63-78` — the bucket index is one merge pass over the ascending thresholds
    (`buckets_[b]` = number of thresholds ≤ `b / 65536`, exactly what `upper_bound` returned), no
    binary searches: 2.10 ms → 0.11 ms on x64, 2.22 → 0.10 ms on x86 (scratch measurement with the
    production flags; the merged table was compared entry-for-entry with the searched one).
  - `Pipeline.cpp:40-60` — `outputThreshold` no longer re-checks the predecessor after an ascent
    (an ascent stops at the first float that reaches the code, so its predecessor is already known
    to fall short); the descent loop is unchanged in effect. Mean `exactQuantize` calls per code
    2.853 → 2.681 on both architectures (plus the one inverse-OETF `pow` of the initial guess);
    threshold build 5.41 → 5.22 ms on x64, 15.87 → 15.04 ms on x86. Why not fewer: the guess is
    already exact for 45.1 % of the codes and one ULP low for 32.2 %, and the remaining spread
    (up to ±5 ULPs) is the float rounding of the forward `pow` path itself, which no guess can
    predict — each threshold needs at least the evaluation that reaches the code and the one
    that proves its predecessor does not, so ≈ 2 calls per code is the floor and 2.68 is close
    to it. The offset histogram (threshold − guess, ULPs): −5: 1, −4: 100, −3: 1,018, −2: 2,417,
    −1: 21,103, 0: 29,581, +1: 2,092, +2: 4,528, +3: 3,441, +4: 1,216, +5: 38 (x64; x86 differs by
    a handful of codes). Every threshold value is unchanged (the scratch program compared the two
    searches for all 65,535 codes on both architectures: no mismatch).
- Tests (`tests/core/colour/PipelineTests.cpp`): `:187` "the sRGB output tables are one shared
  object across Presentations and threads" — `&srgbOutputTables()` is stable, a P3/PQ and an
  identity `Presentation` on the main thread and four HLG `Presentation`s on four `std::jthread`s
  all borrow that same object (so a second `Presentation` cannot have rebuilt them: a function-local
  static is initialised exactly once); `:119` "Presentation release timing: construction" (skipped
  harness) times `SrgbOutputTables` construction directly and `Presentation` construction with a
  cold first row; the 65,536-point grid test (`:332`) and the two whole-image FNV hashes
  (`plugins/avif/tests/e2e/E2eTests.cpp:437-448`) are unchanged and pass with the same values.
- `docs/ARCHITECTURE.md:399-426` (the `colour` bullet: what the tables are, that they are proven
  exact, that they are borrowed) and `:800-815` (§7: the single deliberate piece of process-wide
  state — immutable after construction, pure math, no host/file/option/`Cicp` dependency, once per
  module because each plugin DLL links its own `pvdkit_core` — and why it is not injected through
  the composition root: it is a constant of the sRGB standard that happens to be computed, so
  injecting it would thread a reference to a constant through `CodecPlugin`, `FileSession` and
  `Presentation` for no configurability, while building it per session cost ≈ 7.7 ms x64 /
  ≈ 18 ms x86 inside every HDR `pvdFileOpen`).
- Timing: the new rows are in the table added under "Timing" above (construction per session
  9.48 → 1.74 ms best on x64, 23.77 → 5.37 ms on x86; cosmos per file ≈ 45 → 37.7 ms on x64,
  ≈ 73.5 → 55.9 ms on x86; first view after `LoadLibrary` ≈ 43–44 ms x64 / 72–75 ms x86, which
  includes the one-time 5.3 / 15.3 ms table build).

### Nits

1. NaN guard — `src/core/colour/Pipeline.cpp:83`: `if (!(linear > 0.0F)) return 0;` replaces the
   zero guard; branch count unchanged, NaN → 0 as before. Pinned by `PipelineTests.cpp:211` (NaN,
   −0, −1, −∞, `denorm_min`, `min`, `nextafter(1, 0)`, 1, 2, +∞ against the exact path).
2. Band split — the splitter moved from `FileSession.cpp`'s anonymous namespace to
   `Presentation::applyImage` (`Pipeline.cpp:182-210`): workers are started only for bands
   0..n−2 (`workers.reserve(bandCount - 1)`, `:198`) and the last band runs on the calling thread
   (`:209`) before the `jthread` destructors join the others. `FileSession::decodePage` calls it at
   `src/core/FileSession.cpp:107`; `<thread>` left `FileSession.cpp`. Pinned directly by
   `PipelineTests.cpp:229` (512×513 for `maxThreads` 0, 1, 2, 3, 4, 8 against the serial pass; a
   16×16 image and a 262,144×1 image stay on the calling thread) and, through `FileSession`, by the
   existing `tests/core/FileSessionTests.cpp` thread-count case.
3. 4,096-bucket table measured against 65,536 on the Release harness (production splitter,
   medians of 20, two runs each; x64 and x86):

   | Buckets | x64 scalar 1024×428 | x64 four bands 1024×428 | x64 four bands 4000×3000 | x86 scalar 1024×428 | x86 four bands 1024×428 |
   |---|---:|---:|---:|---:|---:|
   | 65,536 (128 KiB, kept) | 175.5–182.2 ns/px, 76.9–79.9 ms | 47.5–50.9 ns/px, 20.8–22.3 ms | 45.8–47.8 ns/px, 549–574 ms | 289.2–292.1 ns/px, 126.7–128.0 ms | 75.8–76.1 ns/px, 33.2–33.4 ms |
   | 4,096 (8 KiB, rejected) | 199.9–203.9 ns/px, 87.6–89.4 ms | 52.6–52.9 ns/px, 23.0–23.2 ms | 50.7–51.1 ns/px, 608–613 ms | 305.5–311.9 ns/px, 133.9–136.7 ms | 81.2–81.8 ns/px, 35.6–35.8 ms |

   The smaller table is slower on both architectures: the thresholds are dense near black (about
   200 per 1/4096 in the linear segment), so its windows there take twice the probes; the 128 KiB
   table stays (`Pipeline.hpp:35`, with the reason in the comment). Construction is the same for
   both (5.20–5.35 ms x64).
4. The "four bands" harness rows now time the production splitter: `measurePresentation`
   (`PipelineTests.cpp:54`) calls `Presentation::applyImage`, the function `FileSession::decodePage`
   calls, and the test-side copy is gone. The harness re-run on the fixed tree (65,536 buckets,
   same pixel pattern as before): x64 scalar 1024×428 175.5–182.2 ns/px (76.9–79.9 ms), scalar
   4000×3000 180.2–181.5 (2,162–2,178 ms), four bands 1024×428 47.5–50.9 (20.8–22.3 ms; 47.46 /
   47.59 in the two quiet runs), four bands 4000×3000 45.8–47.8 (549–574 ms); x86 scalar
   289.2–292.1 (126.7–128.0 ms), four bands 1024×428 75.8–76.1 (33.2–33.4 ms), four bands
   4000×3000 74.3–74.4 (892–893 ms). Unchanged from the original table within noise, as expected.
5. The 4,096-entry EETF interpolation experiment is deleted from `PipelineTests.cpp` (its numbers
   remain in "Follow-up approximation measurement" above; the `ToneMap.hpp` include went with it).
6. `PipelineTests.cpp:151` "diagnostic: exhaustive quantizer proof over every float in [0, 1]"
   (skipped): all 1,065,353,217 floats from 0.0F to 1.0F on four threads against the exact path,
   also counting monotonicity violations of the exact path. Run on the fixed tree:
   x64 `exhaustive [0, 1]: 1065353217 floats, 0 mismatches, 0 exact-path monotonicity violations`
   (11.07 s), x86 the same line (17.04 s).
7. `docs/ARCHITECTURE.md:420-424` and `:460-463` now say `min(clamp(maxThreads, 1, 4), height)`
   and that `maxThreads == 0` (the `DecoderOptions` default) means one band.
8. `plugins/avif/tests/e2e/E2eTests.cpp:441-444`: comment that the x86 cosmos hash differs in
   libavif's x86 YUV→RGB output, not in the presentation.

Also: a per-file timing case `cosmos per-file open and decode timing is reported without enforcing
a performance gate` (`E2eTests.cpp:540`, runs in ctest like its sibling, prints the first view after
`LoadLibrary` separately because it carries the one-time table build).

### Gate outputs (final tree)

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t20'
cmake --build --preset <p> --parallel 6 ; ctest --preset <p> --parallel 6 --output-on-failure   # p = debug, release, debug-x86, release-x86, asan, in that order
```

| Preset | Build warnings | ctest |
|---|---:|---:|
| debug (x64) | 0 | `100% tests passed out of 17`, 39.93 s |
| release (x64) | 0 | `100% tests passed out of 21`, 18.93 s |
| debug-x86 | 0 | `100% tests passed out of 17`, 57.18 s (see note) |
| release-x86 | 0 | `100% tests passed out of 21`, 26.05 s |
| asan | 0 | `100% tests passed out of 17`, 54.77 s |

Note on debug-x86: the first run of that preset on the final tree reported `94% tests passed, 1
tests failed out of 17` — `leakcheck_tests`, `tests/support/LeakCheckTests.cpp(296): FATAL ERROR:
REQUIRE( debugInfoOnHeap(fresh->section()) )` in "a critical section's debug block is recognised
and reported, not charged", the leak gate's self-test of ntdll's static critical-section debug
pool. That executable links only `pvdkit_leakcheck` and doctest (`tests/support/CMakeLists.txt`),
not `pvdkit_core`, so it cannot observe this change; it passed 3/3 standalone runs
(`16 passed | 0 failed`, the 3 "failed" assertions being its own expected-failure self-tests), in the
earlier debug-x86 run of this round, and in the re-run quoted in the table. Six parallel tests
plus another agent's Rust build were running on the machine at the time; the self-test is timing
sensitive (ntdll hands a freed static-pool block to the fresh section). Not touched.

```powershell
$env:CMAKE_BUILD_PARALLEL_LEVEL='6'; $env:CTEST_PARALLEL_LEVEL='6'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86
```

| Coverage preset | Tests | Regions | Functions | Lines | Branches |
|---|---:|---:|---:|---:|---:|
| x64 | 17/17 in 52.06 s | 1090/1090 | 261/261 | 2153/2153 | 692/692 |
| x86 | 17/17 in 77.14 s | 1090/1090 | 261/261 | 2153/2153 | 692/692 |

Both: `src\core\colour\Pipeline.cpp 105 0 100.00% 18 0 100.00% 170 0 100.00% 64 0 100.00%`,
`src\core\FileSession.cpp 82 0 100.00% 10 0 100.00% 102 0 100.00% 48 0 100.00%`,
`Coverage source completeness passed: 24 executable source files present.`,
`Coverage gate passed: lines 100%, branches 100%.`, and `18/18 Exports.cpp functions executed`
for both `AVIF.pvd` and `RPGMVP.pvd`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -BuildDir build\debug-x86-t20 -ReleaseDir build\release-x86-t20 -Jobs 6
```

| Architecture | clang-format | clang-tidy | cppcheck | PSScriptAnalyzer | BinSkim |
|---|---:|---:|---:|---:|---:|
| x64 | 0 in 0.6 s | 0 in 334.2 s | 0 in 1.5 s | 0 in 4.1 s | 0 in 1.0 s |
| x86 | 0 in 0.7 s | 0 in 344.0 s | 0 in 2.3 s | 0 in 4.6 s | 0 in 1.2 s |

Both `lint: clean`. (The first x64 lint run reported two `performance-inefficient-vector-operation`
findings on `emplace_back` loops in the new test cases; `reserve` was added and every gate re-run.)

```powershell
ctest --preset release -R "_check_(imports|exports)$" -V
ctest --preset release-x86 -R "_check_(imports|exports)$" -V
```

x64 `100% tests passed out of 4`, x86 `100% tests passed out of 4`; both DLLs on both
architectures: `Import policy passed: KERNEL32.dll is the only imported module.` and the export
table exactly `pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`, `pvdPageDecode`, `pvdPageFree`,
`pvdPageInfo`, `pvdPluginInfo`. The function-local static uses the static CRT's
`_Init_thread_*` guard, which adds no import.

`git diff --check -- src tests plugins/avif/tests docs/ARCHITECTURE.md` exits 0. Nothing was
staged, committed, reset or checked out; `plugins/*/package/*` untouched.

### Release binaries after fix round 1 (supersede the table above)

| Architecture/plugin | Absolute path | SHA-256 |
|---|---|---|
| x64 AVIF | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-t20\plugins\avif\AVIF.pvd` | `5C2C08DA51275DF386A39B00468144027F9218CD9D201457150CCC35429D1C3C` |
| x64 RPGMVP | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-t20\plugins\rpgmvp\RPGMVP.pvd` | `033F25DA668E3461AA6AE567F8ADAC7CD4439B81874D53E062D98ED357323701` |
| x86 AVIF | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-x86-t20\plugins\avif\AVIF.pvd` | `C07EAF6842B9C94BB2AD3D91709765FB57803A341EC97F1453E5B907A8434510` |
| x86 RPGMVP | `C:\Users\Roma\Dev\PictureView3\pvdkit\build\release-x86-t20\plugins\rpgmvp\RPGMVP.pvd` | `C433094931904DEE14646F8868044B5665984DA07F46E11E30E7A3B89F05F422` |

### Not done in this round

- The per-pixel path and the Task 20 performance target are unchanged: 20.8 ms four-band on the
  cosmos-sized buffer, still above the 10 ms objective, for the reasons given in "Vectorization
  diagnostics" (the four `powf` calls of the exact BT.2390 EETF).
- The threshold search still spends one `pow` on its initial guess and ≈ 2.7 on verification per
  code; a cheaper guess would not remove the verification calls (see the floor argument above).
- The transfer LUT (1.7 ms x64 / 5.4 ms x86 per session) stays per session as instructed, although
  it depends only on the CICP transfer code.
