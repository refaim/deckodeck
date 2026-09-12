# Task 10 — memory/handle leak gates (all plugins, both architectures)

Goal: prove, per plugin and per architecture, that the plugin returns every byte and every handle
it takes, on the happy path and on every error/abort path, and that decoding hostile input causes
no memory errors. Two levels, both automated gates. **No testing in real Far** (Roma's decision): every host
behaviour that matters for leaks is reproduced by the e2e driver, which calls the DLL exactly
as 0PictureView.dll does.

## Level 1 — deterministic CRT heap + handle accounting (every ctest run, both archs)
- New shared test support in `tests/support/LeakCheck.hpp/.cpp` (doctest helper, static CRT):
  `HeapSnapshot` around a scope using `_CrtMemCheckpoint` / `_CrtMemDifference` /
  `_CrtMemDumpStatistics` in Debug builds; in Release builds (where `_Crt*` is compiled out) use
  `HeapWalk`/`GetProcessHeap()` allocation counting or `HeapSummary` — pick what is reliable with
  the static CRT and document the choice; plus `GetProcessHandleCount` before/after, and
  `GetProcessMemoryInfo(PrivateUsage)` as a coarse cross-check. The assertion is
  **zero delta** in allocation count and bytes and in handle count after warm-up.
- Warm-up: run the scenario once before the snapshot (libavif/dav1d/libspng may lazily allocate
  thread pools, tables, CPU-feature caches on first use — that is not a leak; a second run must
  not grow anything). If a library keeps growing on the second/third run, that IS a finding —
  report it with numbers, do not raise the threshold.
- Scenarios (e2e, through the real DLL via `LoadLibrary`, N = 200 iterations each, every fixture):
  1. init → open(disk) → pageInfo → decode → free → close → exit.
  2. same in memory mode.
  3. open → decode → **close without free** (host may do this).
  4. decode with a callback that aborts at step 0, 1, 2.
  5. every rejection path (garbage, truncated, png, short head, missing file, page out of range).
  6. two pages outstanding, freed in both orders; N sessions open at once then closed.
  7. `pvdInit`/`pvdExit` cycled N times (process-wide state must not accumulate).
  8. 8 threads × N iterations concurrently (heap/handle delta measured after join).
  9. `LoadLibrary`/`FreeLibrary` of the plugin cycled 20 times (DllMain-less, but CRT/TLS state
     of a static-CRT DLL must not leak on unload — check handle count and private bytes).
  10. Host-like browsing: 3 files kept open at once (current + prefetched neighbours), advancing
      through all fixtures in a sliding window, opening the next before closing the previous,
      with a decode aborted by callback every 5th file and a page switch on every animated file;
      N passes over the folder; delta measured after the last close.
- Also unit-level: `FileMapping` open/close N times → handle count delta 0; `Decoder` create/
  destroy N times → heap delta 0 (adapter tests, both plugins).
- Registered as `leak_tests` (Debug and Release, x64 and x86). It must be fast (< 30 s per arch).

## Level 2 — AddressSanitizer + LeakSanitizer preset (x64; pre-release gate)
- Preset `asan` (x64 only; document that clang-cl's Windows ASan does not support x86 leak
  detection): `-fsanitize=address` on every target including the plugin DLL and all test
  executables, `/MT` stays, `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` (verify LeakSanitizer
  actually works with clang 19 Windows x64 static CRT — if it does not, say so with the
  evidence, and keep ASan for memory errors only), `/Zi` + `/DEBUG` in that preset only so
  reports carry symbols. Link `clang_rt.asan-x86_64.lib` (static ASan runtime with `/MT`; check
  the LLVM 19 lib dir for the exact library names and use `/wholearchive:` if required).
- `ctest --preset asan` runs the whole suite including `leak_tests` and `e2e_tests`; any ASan
  report fails the run. Run it also over the **hostile corpus**: every negative fixture plus a
  generated set of 200 mutated files (random byte flips / truncations of each real fixture,
  deterministic seed, generated at test time into `%TEMP%`, not committed) — the plugin must
  return FALSE or a valid image, never trip ASan. This is a fuzz-lite gate; keep it under 2 min.
- `scripts/package.ps1` runs the asan preset before zipping (x64), in addition to lint.


## Rules
As AGENTS.md: TDD (a leak test must first be shown failing against a deliberately leaking fake or
a temporary leak injected in a scratch copy — not in the repo), no commits, no worktrees,
`PVDKIT_BUILD_SUFFIX=-t10`, both architectures for level 1, zero warnings, all existing gates
still green, coverage 100/100 (the new test support code under `tests/` is not gated, but
anything added under `src/` or `plugins/*/src/` is).

## Verify and report
`ctest --preset release`/`release-x86`/`debug`/`debug-x86` (now including `leak_tests`),
`ctest --preset asan` (with the hostile corpus), the red run of the leak gate against an injected
leak, the numbers (allocation/handle deltas per scenario), any library-level growth found. Both plugins if RPGMVP exists by then; otherwise AVIF only with the
support code plugin-agnostic.
