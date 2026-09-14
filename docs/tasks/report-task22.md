# Task 22 implementation report: cleanups and documentation for the 1.1.0 release (deckodeck)

Date: 2026-09-15

Branch: `master`

Starting and final observed `HEAD`: `19669d07e16844593810f48764d12d57c0491254` (unchanged)

Build suffix: `-t22`

No commit, staging operation, reset, checkout, clean, or worktree was performed. Working-tree edits
are the deliverable of this task; `git status --porcelain` lists exactly the files touched below
plus this report. No network access beyond vcpkg's already-warm binary cache was needed (every
`vcpkg install` step reported packages already installed / restored from cache).

## 1. Removed `IDecoder::iccProfile()`

The host never reads the ICC pointer/length pair it is handed (proven in Task 15/19;
`docs/host/pictureview-abi.md` records the reverse-engineered proof), so the accessor existed only
for a future colour-management pipeline that keeps not arriving. Removed:

- `src/core/IDecoder.hpp:98-99` (previously 98-100): the pure virtual and its doc comment. `meta()`
  is now immediately followed by `frameTiming()`.
- `plugins/avif/src/adapters/avif/Decoder.hpp:75-76`, `plugins/avif/src/adapters/avif/Decoder.cpp`
  (the `Decoder::iccProfile()` body that viewed `decoder_->image->icc`, previously lines 273-276):
  override and definition removed. `ImageMeta::hasIcc` is untouched — it is set at
  `plugins/avif/src/adapters/avif/Decoder.cpp:232` from `image.icc.size != 0`, independent of the
  removed accessor.
- `plugins/rpgmvp/src/adapters/spng/Decoder.hpp/.cpp`: the override, the `std::vector<std::byte>
  iccProfile_` member, and the `Decoder` constructor's `iccProfile` parameter are gone
  (`Decoder.hpp:80-83`, `Decoder.cpp:256-262`). The free function that used to copy the profile
  bytes (`readIccProfile`, returning `std::vector<std::byte>`) is now `hasIccProfile`
  (`Decoder.cpp:147-150`), returning `core::Result<bool>` from the same `spng_get_iccp` call
  without touching `profile.profile`/`profile.profile_len`; `DecoderFactory::create`
  (`Decoder.cpp:334-339`) sets `meta.hasIcc = icc` directly and constructs `Decoder` with one fewer
  argument. `#include <vector>` dropped from both files (no longer used).
- `tests/core/Fakes.hpp:97-102`: `FakeDecoder`'s override removed.
- Documentation sentences that existed only for the accessor: `docs/ARCHITECTURE.md` (the
  `iccProfile()` line in the `IDecoder` listing and the paragraph immediately after it, and the §8
  out-of-scope sentence), `plugins/avif/DESIGN.md` (§6 "Out of scope"), `plugins/rpgmvp/DESIGN.md`
  ("Exclusions"). Each now says plainly that ICC presence is detected (`ImageMeta::hasIcc`), but the
  bytes are neither retained nor forwarded. Correction (see "Fix round 1" below): at this point in the
  task, `ImageMeta::hasIcc` drove the "ICC" word in the info line for AVIF only
  (`plugins/avif/src/core/Describe.cpp:132`, untouched by this task); RPGMVP's describer never read
  `hasIcc`, so "in both plugins" was wrong as originally written here. Historical task reports/reviews
  (`docs/tasks/report-task15.md`, `review-task15.md`, `review-task19.md`) were left alone as dated
  records of what was true when they were written.

### TDD evidence (removal, tests first)

1. Removed the assertions that exercised the accessor while the interface still declared it, so the
   suites keep passing at that point: `plugins/avif/tests/adapters/DecoderTests.cpp` (the
   `!(*created)->iccProfile().empty()` check in "all positive fixtures expose complete container
   metadata", and the whole `TEST_CASE("AVIF exposes decoder-owned ICC bytes")`, since its one
   remaining fact — `hasIcc` on `paris_icc_exif_xmp.avif` — is already pinned by the fixture table);
   `plugins/rpgmvp/tests/adapters/DecoderTests.cpp` (the same check in "factory reports metadata for
   every supported PNG source shape", and the profile-byte assertions in what is now "embedded ICC
   profiles are detected and do not disturb exact quadrant pixels in the RPGMVP adapter" — the
   `hasIcc` and pixel-exactness assertions stay, the ICC magic-byte assertions do not).
2. Removed the interface's pure virtual (`src/core/IDecoder.hpp`) next. Building the still-unedited
   adapters against it is the compile-time proof that the tests really depended on the removed API:

   ```
   $ cmake --build --preset debug --parallel 6
   plugins/avif/src/adapters/avif/Decoder.hpp(76,69): error: only virtual member functions can be
   marked 'override'
   plugins/rpgmvp/src/adapters/spng/Decoder.hpp(85,69): error: only virtual member functions can be
   marked 'override'
   ```

   (RED — two translation units each, 4 errors total.)
3. Removed the now-dangling `override` in both adapters and `tests/core/Fakes.hpp`, and rewrote the
   RPGMVP ICC presence check. Rebuild is clean and the full suite is green (§6 below has the ctest
   summary for every preset).

## 2. Info line: "EXIF orientation N" only for N in 2..8

`plugins/avif/src/core/Describe.cpp:138-139` changed the guard from `meta.exifOrientation != 0` to
`meta.exifOrientation >= 2 && meta.exifOrientation <= 8`; the `!core::Transform::hasTransforms(...)`
clause and the irot/imir precedence it protects are unchanged.

TDD: added `TEST_CASE("describe prints EXIF orientation only for the rotated values 2..8")`
(`plugins/avif/tests/core/DescribeTests.cpp:113`) asserting orientation 1 is absent and 2/8 are
present, before touching `Describe.cpp`:

```
$ build/debug-t22/plugins/avif/tests/core/avif_core_tests.exe --test-case="describe prints EXIF orientation only for the rotated values 2..8"
DescribeTests.cpp(119): ERROR: CHECK_FALSE( describe(meta).contains("EXIF orientation") ) is NOT correct!
  values: CHECK_FALSE( true )
[doctest] test cases: 1 | 0 passed | 1 failed | 10 skipped
```

(RED.) After the fix the same case, and the full `avif_core_tests`, pass (§6). A third assertion for
`exifOrientation = 9` was added once the x64 coverage run below flagged the untested "9 > 8" branch
of the new condition (98.39% branches, 1 missed region in `Describe.cpp`) — with it, branches are
100% (verified in the second, green coverage run).

## 3. Review nits from docs/tasks/review-task20-r2.md

1. `src/core/colour/Pipeline.cpp:107-108`: `static_assert(std::is_trivially_destructible_v<SrgbOutputTables>);`
   immediately above `static const SrgbOutputTables tables;`, with a comment naming the reason
   (an atexit entry in every plugin DLL's CRT for a static that in fact never needs one). Compile-time
   check only — `SrgbOutputTables` is already trivially destructible (proven in the Task 20 review by
   object-file inspection), so there is no behaviour to test; the assertion is exercised by every
   build in §6, all green. `<type_traits>` added to the include block.
2. `tests/core/colour/PipelineTests.cpp:194-199`: comment on `TEST_CASE("the sRGB output tables are
   one shared object across Presentations and threads")` stating plainly that this is not a race
   test (a function-local static initialises at most once per process, [stmt.dcl]) and naming what it
   actually pins (sharing after the main thread's initialisation).
3. `tests/core/colour/PipelineTests.cpp:156-161`: comment on the exhaustive diagnostic explaining that
   the exact path is machine-dependent (UCRT dispatches `pow` per CPU) and that counting
   `totalDecreases` (monotonicity), not just `totalMismatches`, is part of the proof that a
   differently-derived table on another machine still agrees with the exact path.

## 4. Product name deckodeck

- `CMakeLists.txt:11`: `project(pvdkit ...)` → `project(deckodeck ...)`, with a comment explaining the
  split. Grepped `PROJECT_NAME`/`CMAKE_PROJECT_NAME` across `cmake/`, `scripts/`, `CMakePresets.json`,
  every `plugins/*/CMakeLists.txt` and every `.cmake` file: no hits anywhere in the tree, so nothing
  depends on the CMake project name. Reconfigured and rebuilt `debug` after the rename (`ninja: no
  work to do` on the following build — no target's build graph depends on `PROJECT_NAME`).
- `README.md`: new title and one-paragraph intro naming the product (deckodeck), both plugins
  (`AVIF.pvd`, `RPGMVP.pvd`), the repository URL (`https://github.com/refaim/deckodeck`) and the
  release channel (GitHub Releases plus the PictureView forum thread), and the one explanation of the
  pvdkit/deckodeck split that every other document points back to. The stale `Plugins` bullet list
  (previously `avif` only) now lists `rpgmvp` too.
- `AGENTS.md:1,9-16`, `docs/ARCHITECTURE.md:1,8-10`: title and intro updated the same way, each with a
  one-line pointer to README.md instead of repeating the full explanation. Both documents otherwise
  keep saying `pvdkit` throughout (targets, namespaces, variables, presets, profiling filenames) —
  grepped every remaining `pvdkit` occurrence in both files after editing and confirmed each is one
  of those, not repository/product prose.
- `plugins/avif/README.md`, `plugins/avif/DESIGN.md`, `plugins/rpgmvp/DESIGN.md`: the three sentences
  that said "pvdkit" meaning the repository, or meaning "this project has no CMS yet", now say
  "deckodeck"; the two DESIGN.md ICC sections were also reworded for change 1 above.
  `plugins/rpgmvp/README.md` had no `pvdkit` occurrence to begin with.
- `scripts/*.ps1` (root and both plugins): every `pvdkit` occurrence is a function name
  (`pvdkit_add_plugin_e2e_tests`, `pvdkit_plugin_identity`), an env var (`PVDKIT_BUILD_SUFFIX`), or a
  profiling filename pattern (`pvdkit-<id>-%p-%m.profraw`) — none renamed, per the task's "documentation
  only" rule.
- Not renamed, as instructed: any CMake target, variable, function, preset, script parameter,
  directory or file. `vcpkg.json`'s `"name": "pvdkit"` was left alone too — it is a build-system
  manifest identifier, not documentation, and the task's explicit rename list named only the root
  `project()` call.
- Historical documents (`docs/tasks/*.md`, `docs/host/pictureview-abi.md`) were left as dated records
  of what was true when they were written, rather than rewritten to say "deckodeck" retroactively;
  flagging this choice in case it should be revisited.

## 5. Package documents (byte-exact)

Edited exclusively through PowerShell `[IO.File]::ReadAllBytes`/`WriteAllBytes` (string built with
explicit `[char]13 + [char]10` for CRLF, `New-Object Text.UTF8Encoding($false)` plus a manually
prepended `EF BB BF` for the BOM'd files); never `sed`, never Git Bash redirection, never an editor.

- `plugins/avif/package/readme_en.txt` and `plugins/rpgmvp/package/readme_en.txt`: appended
  `  https://github.com/refaim/deckodeck` as a new final line under `Roman Kharitonov`, mirroring the
  placement and wording of the homepage line in `C:\Users\Roma\Dev\burlak\dist\readme_en.txt`
  (`Roman Kharitonov` / two-space-indented URL lines).
- `plugins/avif/package/readme_ru.txt` and `plugins/rpgmvp/package/readme_ru.txt`: the same line
  under "Роман Харитонов", BOM preserved.
- `plugins/avif/package/ChangeLog`: one new line in the 1.1.0 entry, in the existing plain Russian
  style, saying HDR images now open about 4x faster on decode with the 12-megapixel HDR example
  (2.3 s → 0.55 s), mirrored in `plugins/avif/README.md`'s `## Changes` 1.1.0 bullet in English.
- `plugins/rpgmvp/package/ChangeLog`: untouched, as instructed.

Byte verification (PowerShell, after every write):

| File | BOM | CR == LF | Ending / content | Result |
|---|---|---|---|---|
| avif readme_en.txt | absent (correct) | 40 == 40 | ends "...deckodeck\r\n", 0 non-ASCII bytes | pass |
| avif readme_ru.txt | EF BB BF | 42 == 42 | ends "...deckodeck\r\n" | pass |
| rpgmvp readme_en.txt | absent (correct) | 30 == 30 | ends "...deckodeck\r\n", 0 non-ASCII bytes | pass |
| rpgmvp readme_ru.txt | EF BB BF | 31 == 31 | ends "...deckodeck\r\n" | pass |
| avif ChangeLog | EF BB BF | 18 == 18 | first line "AVIF 1.1.0 14.09.2026", ends "...0,55 с\r\n" | pass |

`avif_package_docs` and `rpgmvp_package_docs` (the configure-time check re-run under ctest) both pass
in every preset in §6, including immediately after each edit (`cmake --preset debug` re-run cleanly
right after the byte edits, before any other change).

## 6. Gates

All sequential, `PVDKIT_BUILD_SUFFIX=-t22`, `--parallel 6`, one build at a time (never x64 and x86
concurrently). `pwsh` is not on PATH; every script below ran as
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\<name>.ps1`. Zero warnings on every
`cmake --build`.

```
debug:        cmake --preset debug && cmake --build --preset debug --parallel 6      → 0 warnings
              ctest --preset debug --parallel 6           → 100% tests passed, 17/17
release:      cmake --preset release && cmake --build --preset release --parallel 6  → 0 warnings
              ctest --preset release --parallel 6         → 100% tests passed, 21/21
debug-x86:    cmake --preset debug-x86 && cmake --build --preset debug-x86 --parallel 6 → 0 warnings
              ctest --preset debug-x86 --parallel 6       → 100% tests passed, 17/17
release-x86:  cmake --preset release-x86 && cmake --build --preset release-x86 --parallel 6 → 0 warnings
              ctest --preset release-x86 --parallel 6     → 100% tests passed, 21/21
asan (x64):   cmake --preset asan && cmake --build --preset asan --parallel 6        → 0 warnings
              ctest --preset asan --parallel 6            → 100% tests passed, 17/17 (no ASan report)
```

No `leakcheck_tests`/`*_leak_tests` flake was seen on either architecture in this run (x86 debug
leak tests: 53-56 s, within the documented instrumented-build range).

Import/export policy, both architectures (`ctest --preset release|release-x86 -R "_check_(imports|exports)$"`,
4/4 passed each) plus direct `llvm-readobj` verification:

```
x64  AVIF.pvd / RPGMVP.pvd: imports KERNEL32.dll only; exports pvdInit, pvdExit, pvdPluginInfo,
     pvdFileOpen, pvdPageInfo, pvdPageDecode, pvdPageFree, pvdFileClose (the eight bare names)
x86  same DLLs: IMAGE_FILE_MACHINE_I386, same import/export result
```

Coverage (`scripts/coverage.ps1`):

```
-Preset coverage     → 100% tests passed, 17/17; TOTAL lines 2136/2136 100.00%, branches 692/692 100.00%
                       (first run: branches 99.855% / 691 of 692, one untested branch in the new
                       Describe.cpp condition — fixed per §2, second run clean)
-Preset coverage-x86 → 100% tests passed, 17/17; TOTAL lines 2136/2136 100.00%, branches 692/692 100.00%
```

Both runs report every plugin's profile check passed (avif/rpgmvp, 18/18 Exports.cpp functions
executed from each plugin's own two raw profiles) and "Coverage source completeness passed: 24
executable source files present."

Lint (`scripts/lint.ps1 -Jobs 6`, x64 on `build/debug-t22` + `build/release-t22`; x86 with
`-BuildDir build\debug-x86-t22 -ReleaseDir build\release-x86-t22`):

```
x64: clang-format 0, clang-tidy 0 (388.7 s), cppcheck 0, PSScriptAnalyzer 0, BinSkim 0 → lint: clean
x86: clang-format 0, clang-tidy 0 (353.5 s), cppcheck 0, PSScriptAnalyzer 0, BinSkim 0 → lint: clean
```

## Final test counts (debug-t22, after every fix)

```
core_tests:            87 test cases, 332504 assertions, 0 failed, 6 skipped
avif_core_tests:       11 test cases, 51 assertions, 0 failed
avif_adapter_tests:    40 test cases, 3945 assertions, 0 failed
rpgmvp_adapter_tests:  18 test cases, 601 assertions, 0 failed
```

## Files changed

```
AGENTS.md
CMakeLists.txt
README.md
docs/ARCHITECTURE.md
docs/tasks/report-task22.md (new)
plugins/avif/DESIGN.md
plugins/avif/README.md
plugins/avif/package/ChangeLog
plugins/avif/package/readme_en.txt
plugins/avif/package/readme_ru.txt
plugins/avif/src/adapters/avif/Decoder.cpp
plugins/avif/src/adapters/avif/Decoder.hpp
plugins/avif/src/core/Describe.cpp
plugins/avif/tests/adapters/DecoderTests.cpp
plugins/avif/tests/core/DescribeTests.cpp
plugins/rpgmvp/DESIGN.md
plugins/rpgmvp/package/readme_en.txt
plugins/rpgmvp/package/readme_ru.txt
plugins/rpgmvp/src/adapters/spng/Decoder.cpp
plugins/rpgmvp/src/adapters/spng/Decoder.hpp
plugins/rpgmvp/tests/adapters/DecoderTests.cpp
src/core/IDecoder.hpp
src/core/colour/Pipeline.cpp
tests/core/Fakes.hpp
tests/core/colour/PipelineTests.cpp
```

No commit, staging, or reset was performed; the orchestrator commits.

## Fix round 1 (review: docs/tasks/review-task22.md, REJECT)

Build suffix stays `-t22` (same tree reused, not `-review`). No commit, staging, reset, checkout, or
clean was performed.

### Substantive: `pvdkit` still named in the two shipped AVIF readmes

`plugins/avif/package/readme_en.txt:24` and `plugins/avif/package/readme_ru.txt:26` said "pvdkit"
one line above the new deckodeck homepage line, contradicting it for the one document an end user
actually reads. Fixed byte-exactly (PowerShell `ReadAllBytes`/`WriteAllBytes`, `String.Replace` on
the decoded text, re-encoded the same way as the original edit — ASCII for `readme_en.txt`, UTF-8
with the `EF BB BF` BOM prepended for `readme_ru.txt`):

- `readme_en.txt`: "them and pvdkit does not have a colour-management system yet." → "them and
  **deckodeck** does not have a colour-management system yet."
- `readme_ru.txt`: "а в pvdkit ещё нет системы управления цветом." → "а в **deckodeck** ещё нет
  системы управления цветом."

Byte re-verification:

```
readme_en.txt  bytes=1822 BOM=False CR=40 LF=40 loneCR=0 loneLF=0 nonASCII=0 endsCRLF=True
readme_ru.txt  bytes=3051 BOM=True  CR=42 LF=42 loneCR=0 loneLF=0 nonASCII=0 endsCRLF=True
```

Both files now contain zero occurrences of "pvdkit" (checked with a regex match over the decoded
text). `cmake --preset debug` (which re-runs `pvdkit_check_package_docs` at configure time) and
`ctest --preset debug -R package_docs` both pass afterwards (`avif_package_docs` and
`rpgmvp_package_docs`, 2/2).

### Nit 1 — `plugins/avif/DESIGN.md:31-33`

Added the missing clause: the EXIF orientation item is emitted "only when no shared clap/irot/imir
transform suppresses the host code, **and only for orientations 2..8; 1 means no rotation and is
not printed**."

### Nit 2 — `docs/ARCHITECTURE.md:22`

`plugins/<id>/ … (today: avif → AVIF.pvd)` → `… (today: avif → AVIF.pvd, rpgmvp → RPGMVP.pvd)`,
matching `AGENTS.md`'s plugin list.

### Nit 3 — `README.md:10-14`

The parenthetical no longer claims "presets" or "the `build/<preset>` directory names" carry the
`pvdkit` name (they don't: presets are `debug`/`release`/…, build dirs are `build/<preset><suffix>`).
It now names the real carriers: the `pvdkit_core`/`pvdkit_pvd`/`pvdkit_win` targets, the `pvdkit::`
C++ namespace, the `PVDKIT_BUILD_SUFFIX` environment variable, `cmake/pvdkit-*.cmake`, and the
coverage script's `pvdkit-<id>-*.profraw` profile filenames.

### Nit 4 — RPGMVP's info line did not print "ICC"

`ImageMeta::hasIcc` was set by the RPGMVP adapter (`hasIccProfile`, `Decoder.cpp:147-151`) but never
read by `plugins/rpgmvp/src/core/Describe.cpp`, so the task's "the info line prints ICC" premise held
for AVIF only — the original report's "`Describe.cpp` line 132 in both plugins" claim was wrong, now
corrected in-place above (§1).

TDD: added `TEST_CASE("describe reports an embedded ICC profile and omits it otherwise")`
(`plugins/rpgmvp/tests/core/DescribeTests.cpp:52-61`) asserting the base fixture has no "ICC", that
`hasIcc = true` makes `describe()` end with `", ICC"`, and that it still does with `interlaced` also
set. RED first:

```
$ build/debug-t22/plugins/rpgmvp/tests/core/rpgmvp_core_tests.exe --test-case="describe reports an embedded ICC profile and omits it otherwise"
DescribeTests.cpp(58): ERROR: CHECK( describe(meta).ends_with(", ICC") ) is NOT correct!
  values: CHECK( false )
DescribeTests.cpp(61): ERROR: CHECK( describe(meta).ends_with("interlaced, ICC") ) is NOT correct!
  values: CHECK( false )
[doctest] test cases: 1 | 0 passed | 1 failed | 6 skipped
```

Fix: `plugins/rpgmvp/src/core/Describe.cpp:26-28` appends `", ICC"` when `meta.hasIcc`, mirroring
AVIF's `if (meta.hasIcc) { append(description, "ICC"); }` (same field, same literal string, adapted
to this file's plain `result +=` style since it has no `append()` helper). Rebuild is green
(`rpgmvp_core_tests`: 7 test cases, 30 assertions, 0 failed). No other test asserts the full RPGMVP
description string for a fixture with `hasIcc == true`
(`plugins/rpgmvp/tests/adapters/DefaultPluginTests.cpp:74` uses a non-ICC fixture), so nothing else
needed updating. Coverage: `plugins/rpgmvp/src/core/Describe.cpp` grew from 12 to 14 branches (both
new outcomes of the added `if`); the coverage runs below confirm 100% lines and 100% branches on both
architectures with the higher totals (2139 lines / 694 branches, up from 2136 / 692).

### Nit 5 — untruthful performance figures

`plugins/avif/package/ChangeLog`'s 1.1.0 entry and `plugins/avif/README.md`'s Changes line quoted
"12-мегапиксельный HDR: было 2,3 с, стало 0,55 с" / "2.3 s → 0.55 s to decode" as if it were a
measured whole-file decode; per `report-task20.md` that number is the colour-presentation pass alone
over a synthetic 4000×3000 buffer, while the complete `pvdPageDecode` of the cosmos fixture went
91 → 26 ms (≈3.5×) — a different, real number the ChangeLog never claimed. Replaced with a plain,
truthful line carrying no figures:

- `plugins/avif/package/ChangeLog` 1.1.0 entry, byte-exact edit (PowerShell, BOM/CRLF preserved):
  ```
  - " + HDR-картинки открываются заметно быстрее: декодирование ускорено" / "   примерно в 4 раза
    (12-мегапиксельный HDR: было 2,3 с, стало 0,55 с)"
  + " + HDR-картинки открываются в несколько раз быстрее"
  ```
  Re-verified: `bytes=1202 BOM=True CR=17 LF=17 loneCR=0 loneLF=0 endsCRLF=True`, first line
  still exactly `AVIF 1.1.0 14.09.2026`.
- `plugins/avif/README.md:88` Changes bullet: "...and HDR pictures open about 4x faster (a
  12-megapixel HDR file: 2.3 s → 0.55 s to decode)." → "...and HDR images decode several times
  faster."

`avif_package_docs` re-run and passes (below).

### Report correction (nit 4's other half)

`docs/tasks/report-task22.md` §1's "still drives the 'ICC' word in the info line — `Describe.cpp`
line 132 in both plugins" is corrected in place above to say AVIF only, as it stood at that point in
the task, with a pointer to this section for the RPGMVP fix.

### Byte verification after fix round 1 (PowerShell `ReadAllBytes` scan of every touched package doc)

```
avif/readme_en.txt  bytes=1822 BOM=False CR=40 LF=40 loneCR=0 loneLF=0 nonASCII=0 endsCRLF=True  (no "pvdkit")
avif/readme_ru.txt  bytes=3051 BOM=True  CR=42 LF=42 loneCR=0 loneLF=0 nonASCII=0 endsCRLF=True  (no "pvdkit")
avif/ChangeLog      bytes=1202 BOM=True CR=17 LF=17 loneCR=0 loneLF=0 endsCRLF=True; first line "AVIF 1.1.0 14.09.2026"
rpgmvp/ChangeLog    unmodified (not in git status)
```

### Gates re-run after fix round 1

Sequential, `PVDKIT_BUILD_SUFFIX=-t22`, `--parallel 6`, one build at a time; `pwsh` not on PATH, so
every script ran as `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\<name>.ps1`.

```
debug:   cmake --build --preset debug --parallel 6     → 0 warnings
         ctest --preset debug --parallel 6              → 100% tests passed, 17/17 (44.15 s)
release: cmake --build --preset release --parallel 6    → 0 warnings
         ctest --preset release --parallel 6             → 100% tests passed, 21/21 (20.91 s)
```

Coverage:

```
-Preset coverage      → 100% tests passed, 17/17; TOTAL lines 2139/2139 100.00%, branches 694/694 100.00%
                        (rpgmvp/src/core/Describe.cpp: 14/14 branches, up from 12; both plugins'
                        profile checks pass, 18/18 Exports.cpp functions each)
-Preset coverage-x86  → 100% tests passed, 17/17; TOTAL lines 2139/2139 100.00%, branches 694/694 100.00%
```

Lint x64 (`scripts/lint.ps1 -Jobs 6`, `build/debug-t22` + `build/release-t22`):

```
clang-format: 0 finding(s) in 0.7 s
clang-tidy: 0 finding(s) in 345.7 s
cppcheck: 0 finding(s) in 1.5 s
PSScriptAnalyzer: 0 finding(s) in 4.3 s
BinSkim: 0 finding(s) in 1.1 s
lint: clean
```

Not re-run this round (unaffected by the fix-round changes, already green in the base run above and
in the independent reviewer run in `review-task22.md`): `debug-x86`, `release-x86`, `asan`, lint x86,
`check_imports`/`check_exports`.

### Files touched in this fix round

```
docs/ARCHITECTURE.md
docs/tasks/report-task22.md
plugins/avif/DESIGN.md
plugins/avif/README.md
plugins/avif/package/ChangeLog
plugins/avif/package/readme_en.txt
plugins/avif/package/readme_ru.txt
plugins/rpgmvp/src/core/Describe.cpp
plugins/rpgmvp/tests/core/DescribeTests.cpp
README.md
```

No commit, staging, or reset was performed; the orchestrator commits.
