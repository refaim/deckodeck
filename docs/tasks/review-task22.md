# Review: Task 22 — cleanups and documentation for the 1.1.0 release (deckodeck)

Fresh review of the uncommitted working tree on top of `19669d0` (24 modified files plus the new
`docs/tasks/report-task22.md`; `git status --porcelain` matches the report's file list exactly). Reviewer
build suffix `-review`, `--parallel 6`, one build at a time: `debug`, `release`, `asan`, `coverage`,
`coverage-x86`, lint x64. Nothing in the repository was modified, staged or reset; this file is the only
thing written.

## Substantive findings

1. `plugins/avif/package/readme_en.txt:24` and `plugins/avif/package/readme_ru.txt:26` — the shipped
   user documents still name the product `pvdkit` ("them and pvdkit does not have a colour-management
   system yet" / "а в pvdkit ещё нет системы управления цветом") while the new last line of the same
   files points the user at `https://github.com/refaim/deckodeck`. Nothing in a readme explains what
   "pvdkit" is; the identical sentence was renamed to "deckodeck" in `plugins/avif/README.md:74` and
   `plugins/avif/DESIGN.md:84`, so the tree now says both. Contract: the review brief's "docs are
   consistent and truthful" for the rename, and these are the two documents an end user actually
   reads. Fix: replace the one word in each file through the byte-exact PowerShell method (ASCII+CRLF
   for `readme_en.txt`, BOM+CRLF for `readme_ru.txt`; re-check CR == LF and `avif_package_docs`
   afterwards). `plugins/rpgmvp/package/readme_*.txt` contain no `pvdkit` and need nothing.

## Nits

1. `plugins/avif/DESIGN.md:31-32` — the describer section still says the EXIF orientation item "is
   emitted only when no shared clap/irot/imir transform suppresses the host code"; after change 2 that
   is necessary but no longer sufficient (orientation 1 is now omitted as well). One clause: "and only
   for orientations 2..8; 1 means no rotation and is not printed".
2. `docs/ARCHITECTURE.md:22` — `plugins/<id>/ … (today: avif → AVIF.pvd)` is stale; `AGENTS.md:18-19`
   was updated to list `rpgmvp` in this task, this line was not.
3. `README.md:10-12` — the parenthetical lists "presets" and "the `build/<preset>` directory names" as
   carriers of the pvdkit name; neither contains it (presets are `debug`, `release`, …; build dirs are
   `build/<preset><suffix>`). The real carriers are `PVDKIT_BUILD_SUFFIX`, the `pvdkit::` namespace,
   `cmake/pvdkit-*.cmake` and the `pvdkit-<id>-*.profraw` names.
4. `docs/tasks/report-task22.md` §1 claims `ImageMeta::hasIcc` "still drives the 'ICC' word in the info
   line — `Describe.cpp` line 132 in both plugins". Only `plugins/avif/src/core/Describe.cpp:132` prints
   `ICC`; `plugins/rpgmvp/src/core/Describe.cpp` never reads `hasIcc`. In RPGMVP the field is filled by
   `hasIccProfile` (`Decoder.cpp:147-151`) and pinned by the adapter tests but consumed by nothing (that
   was already so before this task, so not a regression). Either print `ICC` in the RPGMVP info line as
   AVIF does, or leave it and correct the report; the task premise "the info line prints ICC" holds for
   AVIF only.
5. `plugins/avif/package/ChangeLog:12-13` and `plugins/avif/README.md:88` — "12-мегапиксельный HDR: было
   2,3 с, стало 0,55 с" / "2.3 s → 0.55 s to decode" are the colour-presentation pass over a synthetic
   4000×3000 buffer (`report-task20.md`: 2258.60 → 553.114 ms), not a measured whole-file decode; the
   complete `pvdPageDecode` of the cosmos fixture went 91 → 26 ms (≈3.5×). The wording and figures were
   dictated by the task prompt, so this is only so the orchestrator knows what the number is.
6. TDD order for change 1: the test assertions on `iccProfile()` were deleted first (suites still green),
   then the pure virtual; the RED evidence is the adapters' `only virtual member functions can be marked
   'override'` compile failure, not a test that fails to compile. Equivalent proof, reported honestly,
   but not the literal "remove tests first (they must fail to compile)" of the task.
7. `vcpkg.json:2` `"name": "pvdkit"` was deliberately left (the implementer flagged it). Consistent with
   the "rename nothing but `project()`" rule; noting it only so the decision is conscious.
8. Optional: no DLL-level or adapter-level assertion on a real orientation-1 file
   (`paris_icc_exif_xmp.avif`, `exifOrientation` 1 in the fixture table) pins that its comments string no
   longer carries "EXIF orientation 1"; the unit test in `DescribeTests.cpp:113-132` covers the describer
   only.

## Verified

Read line by line: `AGENTS.md`, `docs/ARCHITECTURE.md`, the task prompt, `review-task20-r2.md`,
`report-task22.md`, `cmake/pvdkit-package-docs.cmake`, `.gitattributes`, and every changed file.

Checklist items from the brief:

1. `iccProfile()` removal: `src/core/IDecoder.hpp:94-102` now declares `meta()`, `frameTiming()`,
   `decodeFrame()` only, identical to ARCHITECTURE §3.6 after its edit. No override remains in
   `plugins/avif/src/adapters/avif/Decoder.{hpp,cpp}`, `plugins/rpgmvp/src/adapters/spng/Decoder.{hpp,cpp}`
   or `tests/core/Fakes.hpp`; `grep -rni "iccProfile\|readIccProfile"` over `src/`, `plugins/`, `tests/`,
   `docs/` (excluding `docs/tasks/`) finds only `hasIccProfile` (`Decoder.cpp:147,334`). RPGMVP's
   `hasIccProfile` is one-to-one over `spng_get_iccp` through the existing `detail::chunkPresent`
   (`SPNG_OK` → true, `SPNG_ECHUNKAVAIL` → false, else the mapped error), never touches
   `profile.profile`, and `DecoderFactory::create` stores the result in `meta.hasIcc` (`:334-338`);
   `rpgmvp_adapter_tests` pin `hasIcc` true for `rgba16_60x20_par`, `rgb16_88x4a`, `icc_swapped_rb_64x64`,
   `icc_srgb_64x64` and false for the rest (`DecoderTests.cpp:163-164,180`). AVIF sets `hasIcc` from
   `image.icc.size != 0` (`Decoder.cpp:232`) and the fixture table pins it (`paris_icc_exif_xmp` true).
   `<vector>` dropped from both RPGMVP files and `<algorithm>` from its test: no remaining use in any of
   them (build and lint clean). `avif/Describe.cpp:132` still prints `ICC` from `hasIcc`.
2. `plugins/avif/src/core/Describe.cpp:138-139`: `exifOrientation >= 2 && <= 8 && !hasTransforms`.
   `DescribeTests.cpp:113-132` asserts 1 → absent, 2 and 8 → present (`ends_with`), 9 → absent; the
   pre-existing precedence case (`:104-110`, orientation 6 with clap → absent) and the full-string case
   (`:152-171`, orientation 6 with clap/irot/imir → absent) are unchanged. The four short-circuit
   outcomes of the condition are each exercised, and llvm-cov reports 62/62 branches for the file.
3. `src/core/colour/Pipeline.cpp:104-108`: `static_assert(std::is_trivially_destructible_v<SrgbOutputTables>)`
   directly above `static const SrgbOutputTables tables`, comment names the atexit reason, `<type_traits>`
   included; `llvm-nm --undefined-only` on the release `Pipeline.cpp.obj` shows `_Init_thread_{header,footer,epoch}`
   and no `atexit`. `tests/core/colour/PipelineTests.cpp:156-160` (monotonicity count is part of the
   proof because the UCRT `pow` is dispatched per CPU) and `:194-198` (not a race test; [stmt.dcl]) are
   the two comments the r2 review asked for.
4. Rename: `CMakeLists.txt:11` `project(deckodeck LANGUAGES CXX RC)`; `grep -rn "PROJECT_NAME\|CMAKE_PROJECT_NAME\|PROJECT_SOURCE_DIR\|PROJECT_BINARY_DIR"` over `cmake/`, `scripts/`, `CMakePresets.json`,
   `*.in`, `*.rc`, every `CMakeLists.txt`: no hits. Every remaining `pvdkit` in build files and scripts is
   `pvdkit_*`, `PVDKIT_*`, `pvdkit-*`, `pvdkit::` or the fixture generator's embedded profile string; no
   target, variable, function, preset, directory or file was renamed (`git status` shows no renames).
   `README.md:1-14` names the product, both plugins, the repository URL, the release channel (GitHub
   Releases + PictureView forum thread) and explains the pvdkit split; `AGENTS.md:9-16` and
   `docs/ARCHITECTURE.md:8-10` point back to it. Staged `manifest.json` of both plugins unchanged
   (`AVIF`/`RPGMVP` 1.1.0).
5. Package documents, PowerShell `[IO.File]::ReadAllBytes` scan of all six files:

   ```
   avif/readme_en.txt   bytes=1819 BOM=False CR=40 LF=40 loneCR=0 loneLF=0 nonASCII=0 endsCRLF=True
   avif/readme_ru.txt   bytes=3048 BOM=True  CR=42 LF=42 loneCR=0 loneLF=0 badUTF8=False endsCRLF=True
   avif/ChangeLog       bytes=1343 BOM=True  CR=18 LF=18 loneCR=0 loneLF=0 badUTF8=False endsCRLF=True
                        first line 'AVIF 1.1.0 14.09.2026'
   rpgmvp/readme_en.txt bytes=1204 BOM=False CR=30 LF=30 loneCR=0 loneLF=0 nonASCII=0 endsCRLF=True
   rpgmvp/readme_ru.txt bytes=1856 BOM=True  CR=31 LF=31 loneCR=0 loneLF=0 badUTF8=False endsCRLF=True
   rpgmvp/ChangeLog     bytes=580  BOM=True  CR=12 LF=12 loneCR=0 loneLF=0 badUTF8=False endsCRLF=True
                        first line 'RPGMVP 1.1.0 14.09.2026'   (file unmodified: not in git status)
   ```

   All four readmes end with `Roman Kharitonov` / `Роман Харитонов` followed by
   `  https://github.com/refaim/deckodeck` — the same two-space-indented URL line under the author name
   as `C:\Users\Roma\Dev\burlak\dist\readme_{en,ru}.txt`. The new AVIF ChangeLog entry
   (` + HDR-картинки открываются заметно быстрее: декодирование ускорено` / `   примерно в 4 раза
   (12-мегапиксельный HDR: было 2,3 с, стало 0,55 с)`) uses the ` + ` bullet and three-space
   continuation of its neighbours and is plain Russian. `git diff --check`'s "trailing whitespace" on
   these files is the CR of the `-text` CRLF lines, as for every existing line. The staged copies under
   `build/release-review/plugins/*/package/` are byte-identical (`cmp`) and `avif_package_docs` /
   `rpgmvp_package_docs` pass in every preset.

Builds and gates (`PVDKIT_BUILD_SUFFIX=-review`, `--parallel 6`, sequential):

```
$ cmake --preset debug && cmake --build --preset debug --parallel 6       → 45 steps, 0 warnings, exit 0
$ ctest --preset debug --parallel 6
100% tests passed out of 17    Total Test time (real) =  44.10 sec
$ build/debug-review/tests/core/core_tests.exe
[doctest] test cases:     87 |     87 passed | 0 failed | 6 skipped   assertions: 332504
$ build/debug-review/plugins/avif/tests/core/avif_core_tests.exe
[doctest] test cases: 11 | 11 passed | 0 failed                       assertions: 51
$ build/debug-review/plugins/avif/tests/adapters/avif_adapter_tests.exe
[doctest] test cases:   40 |   40 passed | 0 failed                   assertions: 3945
$ build/debug-review/plugins/rpgmvp/tests/adapters/rpgmvp_adapter_tests.exe
[doctest] test cases:  18 |   18 passed | 0 failed                    assertions: 601
$ avif_core_tests.exe --test-case="describe prints EXIF orientation only for the rotated values 2..8" -s
DescribeTests.cpp(119): SUCCESS: CHECK_FALSE( describe(meta).contains("EXIF orientation") )   (orientation 1)
DescribeTests.cpp(122): SUCCESS: CHECK( describe(meta).ends_with("EXIF orientation 2") )
DescribeTests.cpp(125): SUCCESS: CHECK( describe(meta).ends_with("EXIF orientation 8") )
DescribeTests.cpp(131): SUCCESS: CHECK_FALSE( describe(meta).contains("EXIF orientation") )   (orientation 9)
[doctest] test cases: 1 | 1 passed | 0 failed | 10 skipped

$ cmake --preset release && cmake --build --preset release --parallel 6   → 45 steps, 0 warnings, exit 0
$ ctest --preset release --parallel 6
100% tests passed out of 21    Total Test time (real) =  19.37 sec   (avif/rpgmvp check_imports + check_exports included)
$ llvm-readobj --coff-imports build/release-review/plugins/{avif/AVIF,rpgmvp/RPGMVP}.pvd   → Name: KERNEL32.dll (only)
$ llvm-readobj --coff-exports …  → pvdExit pvdFileClose pvdFileOpen pvdInit pvdPageDecode pvdPageFree pvdPageInfo pvdPluginInfo

$ cmake --preset asan && cmake --build --preset asan --parallel 6         → 0 warnings, exit 0
$ ctest --preset asan --parallel 6
100% tests passed out of 17    Total Test time (real) =  57.18 sec   (no AddressSanitizer report)
```

Coverage (`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/coverage.ps1 -Preset coverage`,
`CMAKE_BUILD_PARALLEL_LEVEL=6`, `CTEST_PARALLEL_LEVEL=6`):

```
100% tests passed out of 17    Total Test time (real) =  55.27 sec
Plugin profile check passed for 'avif':   … 18/18 Exports.cpp functions executed   (same for 'rpgmvp')
plugins\avif\src\adapters\avif\Decoder.cpp    126 0 100.00%  30 0 100.00%  293 0 100.00%  70 0 100.00%
plugins\avif\src\core\Describe.cpp             71 0 100.00%   9 0 100.00%  121 0 100.00%  62 0 100.00%
plugins\rpgmvp\src\adapters\spng\Decoder.cpp  155 0 100.00%  38 0 100.00%  359 0 100.00%  86 0 100.00%
src\core\IDecoder.hpp                           2 0 100.00%   2 0 100.00%    2 0 100.00%   0 0 -
src\core\colour\Pipeline.cpp                  105 0 100.00%  18 0 100.00%  171 0 100.00%  64 0 100.00%
TOTAL                                        1085 0 100.00% 258 0 100.00% 2136 0 100.00% 692 0 100.00%
Coverage source completeness passed: 24 executable source files present.
Coverage gate passed: lines 100%, branches 100%.
```

`-Preset coverage-x86` (fresh `build/coverage-x86-review`, 122 steps, 0 warnings):

```
100% tests passed out of 17    Total Test time (real) =  77.02 sec
plugins\avif\src\core\Describe.cpp             71 0 100.00%   9 0 100.00%  121 0 100.00%  62 0 100.00%
plugins\rpgmvp\src\adapters\spng\Decoder.cpp  155 0 100.00%  38 0 100.00%  359 0 100.00%  86 0 100.00%
src\core\colour\Pipeline.cpp                  105 0 100.00%  18 0 100.00%  171 0 100.00%  64 0 100.00%
TOTAL                                        1085 0 100.00% 258 0 100.00% 2136 0 100.00% 692 0 100.00%
Coverage gate passed: lines 100%, branches 100%.
```

Lint (`scripts/lint.ps1 -BuildDir build/debug-review -ReleaseDir build/release-review -Jobs 6`):

```
clang-format: 0 finding(s) in 0.7 s
clang-tidy: 0 finding(s) in 361.5 s
cppcheck: 0 finding(s) in 1.8 s
PSScriptAnalyzer: 0 finding(s) in 4.8 s
BinSkim: 0 finding(s) in 1.3 s
lint: clean
```

Report claims cross-checked: every `file:line` in `report-task22.md` §1-§5 resolves to what it describes
except the "line 132 in both plugins" sentence (nit 4); the test counts, coverage totals (2136/692), the
17/21 ctest counts and the import/export tables reproduce exactly. No new `pragma`, `NOLINT`, `catch (`,
`reinterpret_cast`, `#ifdef`, `LCOV_EXCL` or guard token in the diff; `guard_tests` passes. All modified
text files keep the repository's CRLF endings. The `leakcheck_tests` x86 flake did not appear. Not run
here: `debug-x86`/`release-x86` as separate presets and lint x86 (the x86 compile, tests and coverage
were exercised through `coverage-x86`).

## Verdict

`REJECT` — one substantive item, purely documentary: the two shipped AVIF readmes still call the product
`pvdkit` on the line above the new deckodeck homepage. Everything else checks out: the `iccProfile()`
removal is complete with no dead code, RPGMVP's `hasIccProfile` is correct and tested, the 2..8 rule is
implemented and pinned including the irot/imir precedence, the three r2 nits are in place, the rename
touched `project()` and prose only, the package documents are byte-exact, and every gate I ran (debug,
release, asan, coverage x64 and x86, lint x64) is green with 100/100 coverage. Fix the one word in each
readme through the byte-exact method and re-run `avif_package_docs`; the nits are optional.
