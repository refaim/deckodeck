# Review: Task 22, fix round 1 — re-review after docs/tasks/review-task22.md (deckodeck)

Fresh review of the uncommitted working tree on top of `19669d0` (26 modified files plus the untracked
`docs/tasks/report-task22.md` and `review-task22.md`; `git status --porcelain` contains every file the report's
"Files touched in this fix round" list names). Reviewer build suffix `-review`, `--parallel 6`, one build or lint
at a time: `debug`, `release`, `coverage`, `coverage-x86`, `asan`, lint x64, clang-format/clang-tidy/cppcheck x86.
Nothing in the repository was modified, staged or reset; this file is the only thing written. Only what the fix
round touched was re-verified, plus the full gate run; the first review's verification of the `iccProfile()`
removal, the 2..8 rule, the r2 nits and the rename stands.

## Substantive findings

None.

## Nits

1. `docs/tasks/report-task22.md` (Fix round 1, "Substantive" and "Byte verification after fix round 1") — the
   `readme_ru.txt` row says `nonASCII=0`. A Cyrillic UTF-8 file cannot have zero non-ASCII bytes; my scan of the
   same 3051 bytes reports 2233 non-ASCII bytes and `badUTF8=False`, which is the metric that matters for that
   file (the first-round table correctly printed `badUTF8=False` there). The file itself is right; only the
   claimed number is not, twice. Replace `nonASCII=0` with `badUTF8=False` in both rows.
2. `docs/tasks/report-task22.md` (Fix round 1, nit 4) — "`plugins/rpgmvp/src/core/Describe.cpp:26-28` appends
   `", ICC"`" is off by one: the `if (meta.hasIcc)` block is lines 27-29 (26 is the closing brace of the
   `interlaced` block). `DescribeTests.cpp:52-61` is the test body; the case closes at 62.
3. `plugins/rpgmvp/package/ChangeLog` / `plugins/rpgmvp/README.md` "Changes" — the RPGMVP info line now
   gains ", ICC" for files with an embedded profile (four of the shipped fixtures), which is a small user-visible
   change of the unreleased 1.1.0 entry that neither document mentions. The task said to leave that ChangeLog
   alone, so this is only for the orchestrator to decide; the entry is coarse-grained and may reasonably stay as
   is.
4. Optional, same nature as nit 8 of the first review: the ", ICC" word in RPGMVP is pinned by the describer
   unit test only. No adapter-level or DLL-level assertion on a real ICC-bearing fixture
   (`rgba16_60x20_par.rpgmvp`, `rgb16_88x4a.rpgmvp`, `icc_srgb_64x64.rpgmvp`, `icc_swapped_rb_64x64.rpgmvp`)
   pins that the composed `ImageInfo::comments` ends with `ICC`; `DefaultPluginTests.cpp:74` asserts the
   full string for the non-ICC `indexed8_trns_82x38_shadow2.png_` only, and the e2e test checks the comments
   are non-empty and equal between disk and memory mode.

## Verified

Read line by line: `AGENTS.md`, `docs/ARCHITECTURE.md`, the review prompt, `review-task22.md`,
`report-task22.md` (whole, then "Fix round 1" against the tree), and the `git diff` of every file the fix round
names.

Checklist items from the brief:

1. AVIF readmes. PowerShell `[IO.File]::ReadAllBytes` scan, regex `pvdkit` (case-insensitive) over the decoded
   text:

   ```
   plugins\avif\package\readme_en.txt   bytes=1822 BOM=False CR=40 LF=40 loneCR=0 loneLF=0 nonASCII=0    badUTF8=False endsCRLF=True pvdkit=False
   plugins\avif\package\readme_ru.txt   bytes=3051 BOM=True  CR=42 LF=42 loneCR=0 loneLF=0 nonASCII=2233 badUTF8=False endsCRLF=True pvdkit=False
   plugins\avif\package\ChangeLog       bytes=1202 BOM=True  CR=17 LF=17 loneCR=0 loneLF=0 nonASCII=866  badUTF8=False endsCRLF=True pvdkit=False  first='AVIF 1.1.0 14.09.2026'
   plugins\rpgmvp\package\readme_en.txt bytes=1204 BOM=False CR=30 LF=30 loneCR=0 loneLF=0 nonASCII=0    badUTF8=False endsCRLF=True pvdkit=False
   plugins\rpgmvp\package\readme_ru.txt bytes=1856 BOM=True  CR=31 LF=31 loneCR=0 loneLF=0 nonASCII=1272 badUTF8=False endsCRLF=True pvdkit=False
   plugins\rpgmvp\package\ChangeLog     bytes=580  BOM=True  CR=12 LF=12 loneCR=0 loneLF=0 nonASCII=328  badUTF8=False endsCRLF=True pvdkit=False  first='RPGMVP 1.1.0 14.09.2026'
   ```

   `git diff | cat -A` on the two readmes shows exactly one changed word each (`pvdkit` → `deckodeck` on
   `readme_en.txt:24` "them and deckodeck does not have a colour-management system yet." and `readme_ru.txt:26`
   "а в deckodeck ещё нет системы управления цветом."), every line still `<CRLF>`, the earlier homepage line
   unchanged. `.gitattributes` keeps both `-text`. The staged copies under `build/release-review/plugins/avif/package/`
   are byte-identical (`cmp`) and `avif_package_docs` / `rpgmvp_package_docs` pass in every preset below.
2. `plugins/avif/package/ChangeLog` decoded line by line: line 0 `AVIF 1.1.0 14.09.2026` (unchanged), the 1.1.0
   entry's last bullet is now the single line ` + HDR-картинки открываются в несколько раз быстрее` (the former
   two-line "примерно в 4 раза (12-мегапиксельный HDR: было 2,3 с, стало 0,55 с)" is gone; CR 18 → 17 matches
   the dropped continuation line), 1.0.0 entry untouched, BOM+CRLF intact per the scan above.
   `plugins/avif/README.md:88` ends "..., and HDR images decode several times faster." The wording is backed by
   `report-task20.md`: presentation pass 84.2 → 20.9 ms on the cosmos-sized buffer and the whole warmed-up
   `pvdPageDecode` 26.074 ms against the ≈91 ms the first review quoted (≈3.5×).
3. `plugins/rpgmvp/src/core/Describe.cpp:27-29`: `if (meta.hasIcc) { result += ", ICC"; }` after the
   `interlaced` item, the same field and literal as `plugins/avif/src/core/Describe.cpp:131-133`
   (`append(description, "ICC")` = `", " + "ICC"`). `plugins/rpgmvp/tests/core/DescribeTests.cpp:52-62` asserts
   base meta → no `ICC`, `hasIcc` → `ends_with(", ICC")`, `hasIcc && interlaced` → `ends_with("interlaced, ICC")`
   (order pinned). Without the new block the string ends in `8-bit RGB`, so the second and third assertions
   fail exactly as the report's RED transcript (`DescribeTests.cpp(58)`, `(61)`) shows; the line numbers match
   the tree. No other test asserts a full RPGMVP comments string for an ICC fixture, so nothing else needed
   changing. `plugins/rpgmvp/DESIGN.md` and `README.md` do not spell out the info-line format, so no document
   contradicts the new word. Coverage rows for the file on both architectures below: 22/22 lines, 14/14 branches.
4. Wording. `plugins/avif/DESIGN.md:31-33` now reads "... only when no shared clap/irot/imir transform
   suppresses the host code, and only for orientations 2..8; 1 means no rotation and is not printed", which is
   what `plugins/avif/src/core/Describe.cpp:138-139` does. `docs/ARCHITECTURE.md:22`
   `(today: avif → AVIF.pvd, rpgmvp → RPGMVP.pvd)` matches `AGENTS.md:18-19`. `README.md:10-15` names the
   carriers of the pvdkit name and each exists: `add_library(pvdkit_core|pvdkit_pvd|pvdkit_win STATIC ...)` in
   `src/{core,pvd,adapters}/CMakeLists.txt`; `namespace pvdkit::core` / `pvdkit::pvd` in `src/core/Error.hpp`,
   `src/pvd/Types.hpp`; `$env{PVDKIT_BUILD_SUFFIX}` in `CMakePresets.json:13`; `cmake/pvdkit-plugin.cmake`
   and `cmake/pvdkit-package-docs.cmake`; `LLVM_PROFILE_FILE=.../pvdkit-${id}-%p-%m.profraw` in
   `cmake/pvdkit-plugin.cmake:227,332` and the matching regex in `scripts/coverage.ps1:72`. A grep for `pvdkit`
   over `README.md`, `AGENTS.md`, `docs/ARCHITECTURE.md`, `plugins/*/README.md`, `plugins/*/DESIGN.md`,
   `plugins/*/package/` finds only identifiers, the explained shared-layer name, and the on-disk directory
   name in the §0 tree diagram; no product-meaning prose remains.
5. Report. Every other fix-round claim reproduces: the byte counts (1822 / 3051 / 1202, CR == LF, BOM
   placement), `rpgmvp_core_tests` 7 cases / 30 assertions, the RED transcript's line numbers, coverage totals
   2139 lines / 694 branches (up from 2136 / 692 by the three new lines and one new `if`), 17/17 and 21/21
   ctest, lint x64 clean, the files-touched list. The two inaccurate figures are nits 1 and 2 above.

All touched text files are CRLF throughout (`README.md` 204/204, `docs/ARCHITECTURE.md` 836/836,
`plugins/avif/DESIGN.md` 148/148, `plugins/avif/README.md` 89/89, `Describe.cpp` 38/38, `DescribeTests.cpp`
81/81, `report-task22.md` 442/442 CRLF lines, zero lone LF); `git diff --check` on them is clean. The new code
adds no `pragma`, `NOLINT`, `catch (`, `reinterpret_cast`, `#ifdef` or guard token; `guard_tests` passes.

Builds and gates (`PVDKIT_BUILD_SUFFIX=-review`, `--parallel 6`, sequential, the existing `-review` directories
reconfigured; every build was an 8-step incremental rebuild of `rpgmvp_core`, `RPGMVP.pvd` and the RPGMVP test
executables with 0 warnings):

```
$ cmake --preset debug && cmake --build --preset debug --parallel 6           → 8 steps, 0 warnings, exit 0
$ ctest --preset debug --parallel 6
100% tests passed out of 17    Total Test time (real) =  43.88 sec
$ build/debug-review/plugins/rpgmvp/tests/core/rpgmvp_core_tests.exe
[doctest] test cases:  7 |  7 passed | 0 failed | 0 skipped   assertions: 30 | 30 passed
$ rpgmvp_core_tests.exe --test-case="describe reports an embedded ICC profile and omits it otherwise" -s
DescribeTests.cpp(55): SUCCESS: CHECK_FALSE( describe(meta).contains("ICC") )
DescribeTests.cpp(58): SUCCESS: CHECK( describe(meta).ends_with(", ICC") )
DescribeTests.cpp(61): SUCCESS: CHECK( describe(meta).ends_with("interlaced, ICC") )
[doctest] test cases: 1 | 1 passed | 0 failed | 6 skipped

$ cmake --preset release && cmake --build --preset release --parallel 6       → 8 steps, 0 warnings, exit 0
$ ctest --preset release --parallel 6
100% tests passed out of 21    Total Test time (real) =  20.47 sec   (check_imports/check_exports/package_docs included)
$ llvm-readobj --coff-imports build/release-review/plugins/{avif/AVIF,rpgmvp/RPGMVP}.pvd   → Name: KERNEL32.dll (only, both)
$ llvm-readobj --coff-exports …  → pvdExit pvdFileClose pvdFileOpen pvdInit pvdPageDecode pvdPageFree pvdPageInfo pvdPluginInfo (both)

$ cmake --preset asan && cmake --build --preset asan --parallel 6             → 8 steps, 0 warnings, exit 0
$ ctest --preset asan --parallel 6
100% tests passed out of 17    Total Test time (real) =  55.10 sec   (0 lines matching "AddressSanitizer:" in the output)
```

Coverage (`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/coverage.ps1 -Preset coverage`,
`CMAKE_BUILD_PARALLEL_LEVEL=6`, `CTEST_PARALLEL_LEVEL=6`; build 8 steps, 0 warnings):

```
100% tests passed out of 17    Total Test time (real) =  54.44 sec
Plugin profile check passed for 'avif':   process 19008 wrote 2 profiles … 18/18 Exports.cpp functions executed
Plugin profile check passed for 'rpgmvp': process 38028 wrote 2 profiles … 18/18 Exports.cpp functions executed
plugins\avif\src\core\Describe.cpp     71 0 100.00%   9 0 100.00%  121 0 100.00%  62 0 100.00%
plugins\rpgmvp\src\core\Describe.cpp   22 0 100.00%   3 0 100.00%   22 0 100.00%  14 0 100.00%
TOTAL                                1087 0 100.00% 258 0 100.00% 2139 0 100.00% 694 0 100.00%
Coverage source completeness passed: 24 executable source files present.
Coverage gate passed: lines 100%, branches 100%.
```

`-Preset coverage-x86` (build 8 steps, 0 warnings):

```
100% tests passed out of 17    Total Test time (real) =  75.80 sec
Plugin profile check passed for 'avif' … 18/18; for 'rpgmvp' … 18/18
plugins\avif\src\core\Describe.cpp     71 0 100.00%   9 0 100.00%  121 0 100.00%  62 0 100.00%
plugins\rpgmvp\src\core\Describe.cpp   22 0 100.00%   3 0 100.00%   22 0 100.00%  14 0 100.00%
TOTAL                                1087 0 100.00% 258 0 100.00% 2139 0 100.00% 694 0 100.00%
Coverage gate passed: lines 100%, branches 100%.
```

Lint x64 (`scripts/lint.ps1 -BuildDir build/debug-review -ReleaseDir build/release-review -Jobs 6`):

```
clang-format: 0 finding(s) in 0.8 s
clang-tidy: 0 finding(s) in 312.3 s
cppcheck: 0 finding(s) in 1.4 s
PSScriptAnalyzer: 0 finding(s) in 4.1 s
BinSkim: 0 finding(s) in 1.0 s
lint: clean
```

Lint x86 (`-BuildDir build/coverage-x86-review -Tools clang-format,clang-tidy,cppcheck -Jobs 6`; no
`release-x86-review` exists and the three-line change does not warrant one, so BinSkim was not run for x86):

```
clang-format: 0 finding(s) in 0.6 s
clang-tidy: 1 finding(s) in 325.7 s
  tests\support\sequence\SequenceDriverTests.cpp:119:24: declaration uses identifier '__llvm_profile_write_file',
  which is a reserved identifier [bugprone-reserved-identifier]
cppcheck: 0 finding(s) in 1.5 s
```

That one clang-tidy finding is out of this task's scope: the file is unchanged since commit `efea269` (not in
`git status`), the declaration sits under `#if PVDKIT_COVERAGE`, and it surfaces only because I pointed clang-tidy
at a *coverage* compile database; the documented lint invocation (a `debug`/`release` build directory) never
compiles that block, which is why the x64 and x86 lints of the report and of the first review are clean. Recorded
here for the orchestrator; it is not a Task 22 defect and does not affect the verdict. The rare `leakcheck_tests`
x86 flake did not appear. Not run here: `debug-x86`/`release-x86` as separate presets (the x86 compile, tests and
coverage were exercised through `coverage-x86`, 0 warnings).

## Verdict

`ACCEPT` — the one substantive item of the first review is fixed byte-exactly (both AVIF readmes free of
`pvdkit`, ASCII+CRLF / BOM+CRLF intact, CR == LF, staged copies identical), the ChangeLog and README carry the
figure-free HDR line with the first line and encoding untouched, the RPGMVP describer prints `, ICC` from
`ImageMeta::hasIcc` exactly like AVIF with a test that fails without it and 14/14 branches on both architectures,
the reworded DESIGN.md / ARCHITECTURE.md / README.md sentences are accurate and every named carrier exists, and
every gate (debug, release, asan, coverage x64 and x86, lint x64) is green with 100/100 coverage. The nits are
two wrong numbers in the report and two optional observations.
