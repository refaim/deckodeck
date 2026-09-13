# Task 17 report — static distribution documents

Baseline: clean worktree at `75d733ce62d20c33a9e27088e5f8a34bb11f7264`. The work remains
uncommitted.

## What was done

- Replaced each plugin's generated `package/README.txt.in` with the supplied hand-written
  `readme_en.txt`, `readme_ru.txt` and `ChangeLog`:
  - `readme_en.txt` is ASCII with CRLF;
  - `readme_ru.txt` and `ChangeLog` are UTF-8 with BOM and CRLF;
  - all six files are byte-for-byte equal to the supplied scratchpad text after only the required
    encoding and line-ending conversion.
- Added `.gitattributes` rules marking both document patterns `-text`. `git check-attr` reports
  `text: unset` for all six files, so Git will not normalise their committed bytes.
- Removed the `README` argument and template expansion from `pvdkit_add_plugin`. It now validates
  the three source documents at configure time, removes a stale staged `README.txt`, and copies
  each static document with `configure_file(... COPYONLY)`.
- Added `cmake/pvdkit-package-docs.cmake`. Its shared checker fails configuration when a required
  file is absent, the ChangeLog header does not match the plugin identity and date shape, the
  English readme contains a disallowed byte, or either UTF-8 document lacks the BOM or has a
  non-CRLF/final unterminated line.
- Registered `avif_package_docs` and `rpgmvp_package_docs`. These CMake-script CTest entries rerun
  the identity/encoding/line-ending checks against the staged copies.
- Updated `scripts/package.ps1` to require and copy the DLL plus `readme_en.txt`, `readme_ru.txt`,
  `ChangeLog`, `LICENSES.txt` and `manifest.json`. The zip naming remains unchanged. The inspected
  `scripts/build-all.ps1` consumes only `manifest.json` and did not list package payload files, so
  it needed no change.
- Added the root MIT `LICENSE` with `Copyright (c) 2026 Roman Kharitonov`, mentioned it in the root
  README, and removed the obsolete README.txt zip-content sentence.
- Updated `AGENTS.md`, the packaging and plugin-layout passages in `docs/ARCHITECTURE.md`, and the
  live AVIF plugin documentation. Historical task reports/specifications were left unchanged.

## TDD and negative evidence

The staged-document CTest was registered before production staging changed. Against the old
staging layout, the first targeted run failed as intended:

```text
ctest: 0/2 passed, 2 failed (0.06 sec)
avif_package_docs:
  pvdkit_package_docs(AVIF): required package document
  '.../build/release-t17/plugins/avif/package/readme_en.txt' does not exist
rpgmvp_package_docs:
  pvdkit_package_docs(RPGMVP): required package document
  '.../build/release-t17/plugins/rpgmvp/package/readme_en.txt' does not exist
```

After static staging was implemented, the same targeted test passed `2/2` in `0.20 sec`.

For the required version guard, the first ChangeLog header was changed byte-for-byte from
`AVIF 1.1.0` to `AVIF 9.9.9`, preserving its BOM and CRLF. Reconfiguration failed with:

```text
CMake Error at cmake/pvdkit-package-docs.cmake:101 (message):
  pvdkit_add_plugin(avif): ChangeLog first line must be exactly 'AVIF 1.1.0
  DD.MM.YYYY'; got 'AVIF 9.9.9 13.09.2026'
```

The original header bytes were then restored, the scratchpad-normalised equality probe passed
again, and reconfiguration completed.

## Release verification

`PVDKIT_BUILD_SUFFIX=-t17` was used throughout. Builds ran one at a time with a six-job cap.

| Architecture | Configure | Build | Full CTest | Compiler/linker warnings |
|---|---:|---:|---:|---:|
| x64 | passed | passed | 21/21 passed in 24.51 sec | 0 |
| x86 | passed | passed | 21/21 passed in 29.69 sec | 0 |

Both suites include `guard_tests`, both package-document tests, and the Release import/export
checks. The first fresh configure of each build restored dependencies from local vcpkg archives
and rebuilt the libspng overlay from its cached source. vcpkg printed a non-fatal warning when it
could not write the optional libspng binary-cache archive; dependency installation and both CMake
configurations succeeded. No network fetch occurred.

Focused PowerShell lint:

```text
PSScriptAnalyzer: 0 finding(s) in 5.5 s
lint: clean
```

Explicit policy read-back passed for all four Release DLLs:

- imports: `KERNEL32.dll` only;
- exports: exactly `pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`, `pvdPageDecode`,
  `pvdPageFree`, `pvdPageInfo`, `pvdPluginInfo`, all bare on x64 and x86.

## Package-stage and encoding evidence

The CMake package directories contain five files each and no generated `README.txt`:

| Stage | Plugin | ChangeLog | LICENSES.txt | manifest.json | readme_en.txt | readme_ru.txt |
|---|---|---:|---:|---:|---:|---:|
| x64 | avif | 796 | 24,394 | 188 | 1,349 | 2,137 |
| x64 | rpgmvp | 624 | 2,866 | 166 | 1,105 | 1,717 |
| x86 | avif | 796 | 24,394 | 188 | 1,349 | 2,137 |
| x86 | rpgmvp | 624 | 2,866 | 166 | 1,105 | 1,717 |

The byte probe reported zero lone CR and zero lone LF in every static document. The repository
source, x64 stage and x86 stage SHA-256 values matched for each document. First bytes and line
counts were:

| File | First bytes | Result |
|---|---|---|
| `avif/readme_en.txt` | `41 56 49 46 ... 20 33 0D 0A` | ASCII; 32 CRLF; no BOM |
| `avif/readme_ru.txt` | `EF BB BF 41 56 49 46 20 D0 B4 ...` | strict UTF-8; BOM; 32 CRLF |
| `avif/ChangeLog` | `EF BB BF 41 56 49 46 20 31 2E 31 2E 30 ...` | strict UTF-8; BOM; 15 CRLF |
| `rpgmvp/readme_en.txt` | `52 50 47 4D 56 50 20 66 ...` | ASCII; 29 CRLF; no BOM |
| `rpgmvp/readme_ru.txt` | `EF BB BF 52 50 47 4D 56 50 20 D0 B4 ...` | strict UTF-8; BOM; 29 CRLF |
| `rpgmvp/ChangeLog` | `EF BB BF 52 50 47 4D 56 50 20 31 2E 31 2E 30 ...` | strict UTF-8; BOM; 13 CRLF |

`git check-attr` output:

```text
plugins/avif/package/readme_en.txt: text: unset
plugins/avif/package/readme_ru.txt: text: unset
plugins/avif/package/ChangeLog: text: unset
plugins/rpgmvp/package/readme_en.txt: text: unset
plugins/rpgmvp/package/readme_ru.txt: text: unset
plugins/rpgmvp/package/ChangeLog: text: unset
```

## Coverage

The extra x64 coverage gate passed after all 17 coverage-preset CTests passed:

```text
TOTAL: 1,663/1,663 lines (100.00%), 420/420 branches (100.00%)
Coverage source completeness passed: 20 executable source files present.
Coverage gate passed: lines 100%, branches 100%.
```

The extra x86 coverage attempt ran all `17/17` tests successfully but the coverage gate reported:

```text
TOTAL: 1,661/1,663 lines (99.88%), 419/420 branches (99.76%)
Coverage source completeness passed: 20 executable source files present.
Coverage gate failed: lines 99.87973541791942%, branches 99.761904761904759%
```

The sole miss is unchanged pre-existing `src/pvd/Shim.cpp:18-20`, the
`std::size_t > UINT32_MAX` arm of `iccExtensionSize`. That arm is representable and covered on
x64 but cannot be true when `size_t` and `UINT32` are both 32-bit. `git diff -- src tests
plugins/*/src plugins/*/tests` is empty; Task 17 did not change C++ production or test code. The
task's requested x86 Release build and CTest suite are green, but this extra x86 coverage run is
reported as failed rather than claimed as passing.

## Release DLLs

| Architecture | Path | SHA-256 |
|---|---|---|
| x64 | `build/release-t17/plugins/avif/AVIF.pvd` | `7697417CE5C5496B66F56B49CD56995C6765E751FA7EB9AC1AA2D78D1E3ED145` |
| x64 | `build/release-t17/plugins/rpgmvp/RPGMVP.pvd` | `6193EB1CD8B2342EA2EA26C9987449AF24C363D1C8E97A08A12BE9AB4B3F639A` |
| x86 | `build/release-x86-t17/plugins/avif/AVIF.pvd` | `68059BD3EBF8DBE4B53B9A0B766C7147FA0EACB42F8C418F9D5808F809981702` |
| x86 | `build/release-x86-t17/plugins/rpgmvp/RPGMVP.pvd` | `30F4142E46DF0C330D900F8E620256C7E9B040C39317F91BE7E5C6238246DF1B` |

## Commands run and results

TDD red/green and negative configure check:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t17'; rtk cmake --preset release
# passed with the test registered against the old stage
$env:PVDKIT_BUILD_SUFFIX='-t17'; rtk ctest --preset release -R package_docs --output-on-failure
# expected red: 0/2 passed; readme_en.txt absent for both plugins

$env:PVDKIT_BUILD_SUFFIX='-t17'; rtk cmake --preset release
# passed after static source validation/copy was implemented
$env:PVDKIT_BUILD_SUFFIX='-t17'; rtk ctest --preset release -R package_docs --output-on-failure
# 2/2 passed in 0.20 sec

# after temporarily replacing the first header bytes with "AVIF 9.9.9":
$env:PVDKIT_BUILD_SUFFIX='-t17'; rtk cmake --preset release
# expected FATAL_ERROR quoted above; original bytes then restored
$env:PVDKIT_BUILD_SUFFIX='-t17'; rtk cmake --preset release
# passed
```

Required Release pipelines:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t17'; rtk cmake --build --preset release --parallel 6
# passed; zero compiler/linker warnings
$env:PVDKIT_BUILD_SUFFIX='-t17'; rtk ctest --preset release
# 21/21 passed in 24.51 sec

$env:PVDKIT_BUILD_SUFFIX='-t17'; rtk cmake --preset release-x86
# passed
$env:PVDKIT_BUILD_SUFFIX='-t17'; rtk cmake --build --preset release-x86 --parallel 6
# passed; zero compiler/linker warnings
$env:PVDKIT_BUILD_SUFFIX='-t17'; rtk ctest --preset release-x86
# 21/21 passed in 29.69 sec
```

PowerShell lint:

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Tools PSScriptAnalyzer
# PSScriptAnalyzer: 0 findings; lint clean
```

Policy, stage and byte checks:

```powershell
foreach ($plugin in @(<the four Release DLL paths>)) {
  rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-imports.ps1 -Path $plugin
  rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-exports.ps1 -Path $plugin
}
# all four: KERNEL32.dll only; exactly eight bare exports

Get-ChildItem -Path 'build\release-t17\plugins\*\package\*',
                        'build\release-x86-t17\plugins\*\package\*' -File
# the 20 staged-file entries and byte sizes tabulated above

rtk git check-attr text -- plugins/avif/package/readme_en.txt plugins/avif/package/readme_ru.txt plugins/avif/package/ChangeLog plugins/rpgmvp/package/readme_en.txt plugins/rpgmvp/package/readme_ru.txt plugins/rpgmvp/package/ChangeLog
# text: unset for all six

# A PowerShell byte probe counted CRLF/lone CR/lone LF, validated strict UTF-8 after each BOM,
# printed the first 24 bytes, and compared source/x64-stage/x86-stage SHA-256 values.
# Results are in the package-stage table above; all comparisons were true.

rtk git diff --check
# passed, no output
rtk git diff -- src tests plugins/avif/src plugins/avif/tests plugins/rpgmvp/src plugins/rpgmvp/tests
# passed, no output
```

Additional coverage commands:

```powershell
$env:PVDKIT_BUILD_SUFFIX='-t17'; $env:CMAKE_BUILD_PARALLEL_LEVEL='6'
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
# 17/17 tests; 100% lines and 100% branches; passed
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86
# 17/17 tests; 99.88% lines and 99.76% branches; failed on unchanged Shim.cpp branch
```

## Not run or changed

- `scripts/package.ps1` was deliberately not run, as required; no zip was produced. Its CMake
  staging inputs were inspected directly.
- The full multi-tool lint and ASan presets were not run. The requested PSScriptAnalyzer slice
  passed, and no C++ source or tests changed.
- `scripts/build-all.ps1` needed no edit because it discovers package metadata only through
  `manifest.json`; payload enumeration belongs to `scripts/package.ps1`.
- No repository URL was added to the readmes, no Far Manager installation was touched, no network
  operation was requested, and no commit was created.

## Fix round 1

Both review findings were fixed:

- `cmake/pvdkit-package-docs.cmake` now applies the lone-CR, lone-LF and final-CRLF checks to
  `readme_en.txt`, `readme_ru.txt` and `ChangeLog`. The scan starts at byte zero for the English
  readme and after the UTF-8 BOM only for the two BOM-bearing documents.
- `cmake/pvdkit-plugin.cmake` now removes and recreates each generated package directory before
  copying the three static documents and writing `LICENSES.txt` and `manifest.json`. An arbitrary
  `stale-sentinel.txt` placed in the AVIF stage before reconfiguration disappeared, and both
  regenerated stages contained exactly the five current files.

The reviewer's negative test removed only the CR byte at offset 22 from the first CRLF in
`plugins/avif/package/readme_en.txt`. Configure failed as required, and the source was restored
byte-for-byte in a `finally` block:

```text
=== removed CR at byte offset 22 ===
CMake Error at cmake/pvdkit-package-docs.cmake:86 (message):
  pvdkit_add_plugin(avif): every line in readme_en.txt must end with CRLF
Call Stack (most recent call first):
  cmake/pvdkit-plugin.cmake:137 (pvdkit_check_package_docs)
  plugins/avif/CMakeLists.txt:51 (pvdkit_add_plugin)
=== lone LF in readme_en exit=1 ===
=== restored SHA-256 B207999C8D2221D7F3ADC6CF16DAAEDB9C507AD1B1F714DAB9DCD6B4880B4091 ===
```

The required grep now reports only historical task documents:

```text
docs/tasks/report-task13.md:19:- Both package `README.txt.in` files were left unchanged: they contain only
docs/tasks/task16-colour.md:82:AVIF → 1.2.0 (feature). README/README.txt.in: "HDR (PQ/HLG) and wide-gamut (Rec.2020, P3) images
docs/tasks/task6-x86.md:122:   `dist/AVIF-<version>-x86.zip`, each containing `AVIF.pvd`, `README.txt` (install: copy next to
docs/tasks/task8-rpgmvp.md:96:   `pvdkit_add_plugin(rpgmvp LINK rpgmvp_composition README package/README.txt.in LICENSES libspng
docs/tasks/task8-rpgmvp.md:99:3. `plugins/rpgmvp/package/README.txt.in` (install, what is supported, limitations; the
```

The corresponding exclusion check produced no output:

```powershell
rtk proxy git grep -n "README.txt" -- . ':(exclude)docs/tasks/**'
# no matches (exit 1)
```

Required verification used `PVDKIT_BUILD_SUFFIX=-t17`, one build at a time and a six-job cap:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t17'; rtk proxy cmake --preset release
# passed: configuring 7.7 s, generating 0.5 s
$env:PVDKIT_BUILD_SUFFIX = '-t17'; rtk proxy cmake --build --preset release --parallel 6
# passed: Ninja reported no work to do; zero compiler/linker warnings
$env:PVDKIT_BUILD_SUFFIX = '-t17'; rtk proxy ctest --preset release
# passed: 21/21 in 35.25 s, including both package-docs, guard, import and export tests
rtk git diff --check
# passed, no output
```

No PowerShell file was changed, so the conditional PSScriptAnalyzer run was not needed. Coverage
was not rerun in this fix round because no C++ source changed; the Task 17 x64 coverage result above
remains 1,663/1,663 lines and 420/420 branches. No network operation or commit was performed.
