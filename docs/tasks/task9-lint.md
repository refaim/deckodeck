# Task 9 — static analysis gate (clang-tidy, cppcheck, clang-format, PSScriptAnalyzer, BinSkim)

Goal: a minimal, non-fanatical analyzer set wired into the build as a gate, modelled on
`C:\Users\Roma\Dev\ObserverModules` (read its `.clang-tidy`, `.clang-format`,
`.github/workflows/main.yml` lines ~105-120 and `docs/build-system.md` "analyzer" paragraphs).
Zero findings after this task, achieved by fixing code or by **justified, narrowly scoped**
suppressions (one-line reason each; a suppression file is fine for third-party noise).

## Tools (all already installed on this machine — do not install others)
- clang-tidy 19 + clang-format 19: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin`
- cppcheck 2.21: `cppcheck` on PATH (scoop)
- BinSkim 4.4.9.11: scoop (`binskim` on PATH; check `scoop prefix binskim`)
- PSScriptAnalyzer: if `Get-Module -ListAvailable PSScriptAnalyzer` is empty, install it for the
  current user (`Install-Module PSScriptAnalyzer -Scope CurrentUser -Force`, PSGallery); it is a
  PowerShell module, not a system tool, so this is allowed.

## Configuration
1. `.clang-tidy` at the repo root, same check set as ObserverModules:
   `-*, clang-analyzer-*, bugprone-*, performance-*, portability-*,
   -bugprone-easily-swappable-parameters`, `WarningsAsErrors: '*'`,
   `HeaderFilterRegex` matching our `src/` and `plugins/*/src/` but not `third_party/` or
   vcpkg. Add `misc-include-cleaner`? No — keep the set minimal as asked. If a check fires only
   on doctest macros in `tests/`, restrict that check to `src/` via a `tests/.clang-tidy`
   override rather than disabling it globally.
2. `.clang-format`: copy ObserverModules' style (Microsoft-based, 120 columns, 4 spaces,
   `Standard: c++20` → set `Latest`). Format the whole tree once (`src/`, `tests/`,
   `plugins/`), commit the reformat separately in spirit (it is one task, but keep the reformat
   as the FIRST `git add`-able step and report its diffstat) so reviewers can tell style churn
   from logic changes.
3. cppcheck: `--enable=warning,performance,portability --std=c++23 --inline-suppr
   --error-exitcode=1 --library=windows -I src -I plugins/*/src --suppress=*:*/vcpkg_installed/*
   --suppress=missingIncludeSystem`, project via the compile database. Suppressions in
   `cppcheck-suppressions.txt` with reasons.
4. PSScriptAnalyzer over `scripts/*.ps1`: `Invoke-ScriptAnalyzer -Recurse -Severity Warning,Error
   -Settings PSGallery` (or a small `PSScriptAnalyzerSettings.psd1` excluding only rules with a
   stated reason). Zero findings.
5. BinSkim on the release DLLs (x64 and x86): `binskim analyze <dll> --config default`; treat
   `Error` results as gate failures; document any rule that cannot apply to a static plugin DLL
   (e.g. missing PDB by design) with the exact rule id in a config file. Check that `/guard:cf`,
   `/DYNAMICBASE`, `/HIGHENTROPYVA`, `/NXCOMPAT`, `/SAFESEH` (x86) are on; if BinSkim asks for
   something we deliberately do not do, say so rather than silently excluding.

## Wiring
- `scripts/lint.ps1` runs all five (clang-format `--dry-run --Werror`, clang-tidy via
  `compile_commands.json` from an existing configured build dir — pass `-BuildDir`; run tidy
  in parallel over the compile database with `run-clang-tidy` if present in the LLVM dir, else a
  PowerShell `ForEach-Object -Parallel`), cppcheck, PSScriptAnalyzer, BinSkim on the release
  DLLs — and exits non-zero on any finding. `CMAKE_EXPORT_COMPILE_COMMANDS ON` in the presets.
- A `lint` ctest test in the release preset is NOT wanted (too slow for every ctest run); instead
  `scripts/package.ps1` calls `lint.ps1` before zipping, and README documents `scripts/lint.ps1`
  as part of "definition of done". Add it to AGENTS.md rule 12's DoD list.
- Important: clang-tidy must consume the real compile flags. Our flags are clang-cl style
  (`/clang:-std=c++23`, `/W4`, `/EHsc`); clang-tidy reads `compile_commands.json` from the
  Ninja build, which already has the right driver mode (`clang-cl.exe` as the compiler entry
  makes clang-tidy use cl mode). The orchestrator's exploratory run with hand-written flags
  produced only bogus `no type named string_view` errors, so verify with a planted bug
  (`strcpy` into a 4-byte buffer + null deref in a scratch file compiled through the same
  invocation) that the run really analyses, and quote it.

## Findings
Run everything, then fix real findings in code with TDD where behaviour changes (most will be
mechanical: `performance-unnecessary-value-param`, `bugprone-narrowing-conversions`, etc.).
Report every suppression with its reason. Both architectures: the x86 compile database too
(`build/debug-x86-*/compile_commands.json`) since `portability-*` and narrowing differ there.

## Verify and report
`scripts/lint.ps1 -BuildDir build/debug-t9 -ReleaseDir build/release-t9` (and the x86 pair) →
exit 0; before/after counts per tool; the planted-bug proof; clang-format diffstat; full ctest +
coverage + check-imports still green on both architectures after the fixes. No commits; `git add`.
