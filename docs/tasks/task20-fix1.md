# Task 20, fix round 1 — act on the review of the exact HDR presentation acceleration

Repository: `C:\Users\Roma\Dev\PictureView3\pvdkit` (Windows, clang-cl 19, C++23, `/MT`, x64 + x86).
Read completely, in this order, before touching anything:
1. `AGENTS.md` and `docs/ARCHITECTURE.md` — the contract (rules 1–12: ownership, adapters, no C,
   exceptions/firewall, TDD, 100% line+branch coverage, zero warnings, guard tokens).
2. The original task: `C:\Users\Roma\AppData\Local\Temp\claude\c--Users-Roma-Dev-PictureView3\630ccd33-5bbb-4673-9fea-509fa47e861e\scratchpad\task20.md`.
3. The implementer's report: `docs/tasks/report-task20.md`.
4. The review you must act on: `docs/tasks/review-task20.md` (verdict REJECT, one substantive
   finding, eight nits).

The working tree is uncommitted on top of `2c5e42b` and also carries Task 21 (version 1.2.0 → 1.1.0
in `plugins/avif/CMakeLists.txt`, `DefaultPluginTests.cpp`, `E2eTests.cpp` version literals, READMEs,
package ChangeLogs). Leave those edits exactly as they are. Never run `git checkout`, `git reset`,
`git stash`, `git clean`, never stage or commit. Never edit `plugins/*/package/*` (CRLF + BOM files).

## What to do

### Substantive finding 1 — sRGB output tables built per session
Decision (orchestrator): the sRGB threshold and bucket tables are pure, immutable math independent
of `Cicp`; build them **once per process, lazily, as a function-local `static const`** in
`src/core/colour/Pipeline.cpp` (C++ guarantees thread-safe one-time initialisation), and let every
`Presentation` borrow them by reference. Only the CICP-dependent 65,536-entry transfer LUT stays per
session. Document this in `docs/ARCHITECTURE.md` as the single, deliberate piece of process-wide
state: immutable after construction, pure math, no host or file dependency — and say why it is not
injected through the composition root.
Also make the construction itself cheap, as the reviewer suggested: the bucket table via one O(n)
merge pass over the sorted thresholds instead of 65,537 binary searches, and fewer `exactQuantize`
calls per threshold if you can do it without losing exactness. The exactness proof must still hold:
keep the 65,536-point grid test and the two whole-image FNV hashes unchanged (they must still pass
with the same values — output is bit-identical to before).
TDD: first write the failing test(s) that pin the new behaviour (e.g. the shared tables are the same
object across two `Presentation` instances / across threads; construction of a second `Presentation`
does not rebuild them), then implement.
Then measure and add the missing row(s) to the timing table in `docs/tasks/report-task20.md`:
`Presentation` construction time before and after (x64 and x86), and the honest per-file number for
the cosmos fixture (open + decode) before and after.

### Nits — do all of them
1. NaN guard in `quantize`: `if (!(linear > 0.0F)) return 0;` replacing the existing zero guard so
   the branch count does not grow and the old behaviour (NaN → 0) is preserved.
2. Band split: run the last band on the calling thread and start workers only for the others.
3. Measure a 4,096-bucket table (8 KiB) against the 65,536-bucket one on the release timing harness;
   keep whichever is faster, record both numbers in the report.
4. Either time the production band splitter through `FileSession` or state in the report that the
   "four bands" harness rows time a test-side copy.
5. The 4,096-entry EETF interpolation experiment: drop it (its numbers are already in the report).
6. Add the exhaustive quantiser proof (every float in [0, 1] against `exactQuantize`) as a skipped
   diagnostic test next to the timing cases, named as a diagnostic.
7. `docs/ARCHITECTURE.md`: write the actual clamp — `min(clamp(maxThreads, 1, 4), height)`,
   `maxThreads == 0` means one band.
8. `plugins/avif/tests/e2e/E2eTests.cpp` near the x86 cosmos hash: one comment that the difference is
   in libavif's x86 YUV→RGB output, not in the presentation.

## Gates (all must be green before you report)
With `PVDKIT_BUILD_SUFFIX=-t20`, sequentially, `--parallel 6`, one build at a time, never x64 and
x86 concurrently (another agent runs a Rust build on this machine):
`cmake --build --preset debug` + `ctest --preset debug`; same for `release`, `debug-x86`,
`release-x86`, `asan`; `scripts/coverage.ps1 -Preset coverage` and `-Preset coverage-x86`
(100% lines and branches); `scripts/lint.ps1 -Jobs 6` for x64 and for x86
(`-BuildDir build\debug-x86-t20 -ReleaseDir build\release-x86-t20`); the release import/export
checks. `pwsh` is not on PATH — use `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\<name>.ps1`.
Zero warnings. Quote the summary lines.

## Report
Update `docs/tasks/report-task20.md` in place: add a "Fix round 1" section that lists each finding
and nit, what you changed (file:line), the new timing rows, and the gate outputs. Your final message
is a short summary of that section.
