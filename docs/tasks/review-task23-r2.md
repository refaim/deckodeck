# Review: Task 23 fix round 1 (after docs/tasks/review-task23.md)

Reviewed the uncommitted working tree on top of `9667cea` (14 modified, 11 new files; still nothing
under `src/**` or `plugins/*/src/**`, `plugins/*/package/*` byte-identical to HEAD). Build suffix
`-review`, `--parallel 6`, one build at a time. Each of the 15 items of the first review was read
against the code and, where a run was possible, run.

## Substantive findings

None.

## Nits

1. `.github/workflows/release.yml:146-147` — after the fifth rejected push the loop still prints
   `push rejected (attempt 5 of 5); retrying from a fresh origin/master` and sleeps 25 s before
   `:149` throws (observed in the exhaustion run below). Guard the message and the sleep with
   `$attempt -lt $attempts`; cosmetic, the exit code is right.
2. `.github/workflows/release.yml:115` — the hint `gh release delete <tag>` prompts for confirmation
   and, without `--cleanup-tag`, keeps the tag, which is what the re-run needs; say
   `gh release delete <tag> --yes` so the instruction can be pasted.
3. `.github/workflows/release.yml:15-17` — the tag filter enumerates the plugin ids, so a third plugin
   needs a workflow edit; `'*/v*'` would do, since `scripts/release-tag.ps1:17-26` already rejects an
   id without a `plugins/<id>/CMakeLists.txt`. Optional.
4. `.github/workflows/build.yml:48-55` — `upload-artifact@v7` without `overwrite: true`. v4+ refuses a
   second artifact of the same name in one run (409 "an artifact with this name already exists on the
   workflow run"); whether a previous *attempt*'s `plugins-x64` counts for "Re-run all jobs" I could
   not confirm offline, and `overwrite: true` costs nothing. Re-running only the failed `release` job,
   the scenario the workflow documents, does not upload and is unaffected.
5. `README.md:205-221` — the README push needs `master` to accept a direct push from `GITHUB_TOKEN`;
   with branch protection on `master` the step fails after five attempts (about 75 s) with the release
   already published and only the Downloads row stale. One sentence in the release bullet would save
   the debugging session.
6. `plugins/rpgmvp/DESIGN.md:39,51` and report "Fix round 1" nit 5 ("the 8-bit input is
   byte-identical") — true for the pixels (decoded v1 and v2 8-bit inputs compare equal, 12 M pixels)
   but not for the cached file: the task's `rpgmvp-timing-4000x3000-rgba8-noise7.rpgmvp` is 22,076,192
   bytes (46.0 %), a fresh v2 encode by the stock-zlib build is 20,543,091 bytes (42.8 %) — the cached
   inputs were evidently encoded by the zlib-ng build. "46 % of its raw size" therefore describes the
   measured file, not what a re-measurement from a fresh cache produces; one clause in DESIGN.md
   ("as encoded by the zlib-ng build; stock zlib encodes the same pixels to 43 %") keeps a future
   re-run from reading the smaller file as a generator change.
7. `cmake/find-llvm.cmake:14` — `list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PVDKIT_LLVM_DIR)`
   runs on every include of the toolchain, so the list collects duplicates; harmless, `if(NOT
   PVDKIT_LLVM_DIR IN_LIST CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)` would be tidy.
8. Report "Fix round 1" nit 7 cites `release.yml:135` for the repository URL; it is `:137`.

## Verified

Read against the code, item by item:

- Permissions: `ci.yml:13-14` and `build.yml:15-16` workflow-level `contents: read`; `release.yml:23-24`
  `contents: read`, `release` job `:65-66` `contents: write`; the caller jobs `build-x64`/`build-x86`
  set nothing and inherit read-only; `verify` holds read-only. `actionlint 1.7.12`: `Found 0 errors in
  3 files` (shellcheck/pyflakes not installed, rules disabled).
- `release.yml:105-120`: `gh release view <tag> --json assets --jq '.assets[].name'` first; exit 0 →
  both zip names must be among the assets, else throw with the delete-and-re-run instruction; return
  without touching the release; otherwise `gh release create ... --verify-tag` with an explicit
  `$LASTEXITCODE` check. `$PSNativeCommandUseErrorActionPreference = $false` at `:107` and `:128`
  (both steps run under the runner's default `pwsh`; the variable is a no-op under 5.1). Array
  splatting `@zips` into a native command verified to expand into separate arguments. Per-tag
  concurrency `release-${{ github.ref }}` kept (`:19-20`). No `${{ }}` inside any `run:` of the job;
  `:43` uses `$env:GITHUB_REF_NAME`, `:137` builds the URL from `$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY`.
- README loop (`release.yml:126-149`) simulated with real git in the scratchpad: a bare `origin.git`
  (master = M0 with README.md + `update-readme-downloads.ps1`, tags `avif/v1.2.0` and `rpgmvp/v1.2.0`),
  two shallow clones made the way `actions/checkout` does (`git init`, `fetch --depth=1
  +refs/tags/<tag>:refs/tags/<tag>`, detached checkout), the step body taken verbatim from lines
  127-149 and wrapped like the runner wraps a step (`$ErrorActionPreference = 'stop'` prologue, `exit
  $LASTEXITCODE` epilogue, dot-sourced). Two jobs at once (a one-shot 4 s `pre-receive` delay made B
  push after A): A → `README (attempt 1): updated`, `46385f4..b306bd7 master -> master`, exit 0; B →
  attempt 1 `! [remote rejected] master -> master (cannot lock ref 'refs/heads/master': is at b306bd7
  but expected 46385f4)`, `push rejected (attempt 1 of 5)`, attempt 2 `+ 46385f4...b306bd7 master ->
  origin/master (forced update)`, `Reset branch 'master'`, `README (attempt 2): updated`,
  `b306bd7..3e13abb master -> master`, exit 0; origin master = M0 → `README: AVIF 1.2.0 released` →
  `README: RPGMVP 1.2.0 released`, both rows current. Re-run of A (row current): `README (attempt 1):
  unchanged`, exit 0, nothing committed — also through `powershell -command ". 'step.ps1'"` (the
  runner's dot-source form): exit 0. B with the older `1.1.0`: `unchanged`, exit 0. Always-rejecting
  hook: five attempts, five `remote rejected`, `could not push the README row after 5 attempts`, exit 1.
- `scripts/update-readme-downloads.ps1` on copies of README.md. LF copy (0 CR, 310 LF, no BOM):
  `AVIF 1.1.0` → `unchanged`, hash identical; `AVIF 1.2.0` → `updated` (that row only); `AVIF 1.0.1` →
  `VERBOSE: the row already names 1.2.0, newer than 1.0.1; left alone` / `unchanged`; `AVIF 1.10.0` →
  `updated` (numeric compare); repeat → `unchanged`; `HEIC 0.1.0` → `updated`, row appended before the
  end anchor; repeat → `unchanged`; result 0 CR, 311 LF, no BOM. CRLF copy (310 CR = 310 LF, no lone):
  `RPGMVP 1.2.0` → `updated`, one RPGMVP row (replaced in place); repeat → `unchanged`; `HEIC 0.1.0` →
  appended with CRLF; 311 = 311, no lone CR/LF, no BOM.
- `scripts/release-tag.ps1`: `avif/v1.1.0` → `avif AVIF 1.1.0 avif/v1.1.0`; `rpgmvp/v1.1.0` OK;
  `AVIF/v1.1.0` → exit 1 `not <plugin id>/vMAJOR.MINOR.PATCH (lower-case id, ...)` (`-cnotmatch`);
  `avif/v1.2.0` → `asks for 1.2.0, but ... declares VERSION 1.1.0`; `heic/v1.0.0` → no CMakeLists;
  `v1.0.0` and a trailing space → shape error.
- `scripts/release-notes.ps1 -Id rpgmvp|avif -Zip <task zips> -OutFile`: no BOM, 0 CR; RPGMVP entry =
  header, dashed underline, two `- ` items with their 3-space continuation lines, blank, `SHA-256:`,
  blank, `    RPGMVP-1.1.0-x64.zip  011758c0...`, `    RPGMVP-1.1.0-x86.zip  ede3f1ef...`; AVIF: five
  `- ` items, stops before `AVIF 1.0.0 12.09.2026`.
- `PVDKIT_LLVM_DIR`: unset → `cmake -P cmake/find-llvm.cmake` exit 0, `check-imports.ps1` `Import
  policy passed`; `C:\nonexistent\llvm\bin` → cmake `CMake Error at cmake/find-llvm.cmake:23 ...
  (environment) does not contain clang-cl.exe` exit 1, `check-imports.ps1` / `lint.ps1 -Tools
  clang-format` / `coverage.ps1` exit 1 `PVDKIT_LLVM_DIR=C:\nonexistent\llvm\bin does not contain
  clang-cl.exe`; `-DPVDKIT_LLVM_DIR=C:/nonexistent` → `find-llvm.cmake:17` error exit 1, also inside a
  real configure with the toolchain (`-DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl.toolchain.cmake`), and the
  same for the environment form; the Build Tools directory in either form → exit 0; `lint.ps1 -LlvmDir
  C:\nonexistent` → `LLVM directory C:\nonexistent does not contain clang-cl.exe`. A probe project
  configured with the toolchain and `--debug-trycompile`: every `CMakeScratch/TryCompile-*/CMakeCache.txt`
  carries `PVDKIT_LLVM_DIR:UNINITIALIZED=C:/Program Files (x86)/.../Llvm/x64/bin`, i.e. the
  `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES` forwarding works. `build/debug-review/CMakeCache.txt`:
  `PVDKIT_LLVM_DIR:PATH=...BuildTools/VC/Tools/Llvm/x64/bin`, compiler/linker/rc under it.
- `scripts/pack.ps1 -Suffix -review -DistDir <scratch>`: exit 0; the eight gate lines (output lines
  110-600) all precede the first zip line (601); four zips with SHA-256 and four objects; entries:
  `AVIF-1.1.0-x64.zip` AVIF.pvd 2574336 + ChangeLog 1202 + LICENSES.txt 24394 + readme_en.txt 1822 +
  readme_ru.txt 3051 (5), `AVIF-1.1.0-x86.zip` AVIF.pvd 1670144 (5), `RPGMVP-1.1.0-x64.zip` RPGMVP.pvd
  320512 (5), `RPGMVP-1.1.0-x86.zip` RPGMVP.pvd 275456 (5); no `manifest.json`; `dist/` in the
  repository untouched (names and timestamps compared). Atomicity: fake `build/release[-x86]-fakerev2`
  trees (copies of the review DLLs and package staging, x86 RPGMVP manifest set to 9.9.9, i.e. the last
  entry fails): eight `policy passed` lines, then `RPGMVP.pvd carries FileVersion '1.1.0', expected
  '9.9.9'`, exit 1, the `-DistDir` was never created; fake trees removed afterwards.
- `scripts/lint.ps1 -Tools psscriptanalyzer`: `PSScriptAnalyzer: 0 finding(s) in 4.4 s`, `lint: clean`.
  `clang-format --dry-run --Werror` on `DecodeTimingTests.cpp`: exit 0; `clang-tidy -p
  build/debug-review` on it: 0 diagnostics in the file (`Suppressed 58710 warnings (58710 in non-user
  code)`).
- `cmake --preset debug` (`-review`): `Configuring done (4.9s)`; `cmake --build --preset debug --parallel
  6`: 3 steps (`DecodeTimingTests.cpp.obj`, link `rpgmvp_adapter_tests.exe`), 0 warnings, exit 0.
  `ctest --preset debug`: `100% tests passed out of 17`, `Total Test time (real) = 64.60 sec`
  (`avif_leak_tests` 37.20 s, `rpgmvp_leak_tests` 15.59 s).
- `scripts/coverage.ps1 -Preset coverage`: `100% tests passed out of 17`; `Plugin profile check passed
  for 'avif' ... 18/18 Exports.cpp functions`, same for `rpgmvp`; `TOTAL 1087 0 100.00% 258 0 100.00%
  2139 0 100.00% 694 0 100.00%`; `Coverage source completeness passed: 24 executable source files
  present.`; `Coverage gate passed: lines 100%, branches 100%.` Rows: `plugins\rpgmvp\src\DefaultPlugin.cpp`
  22/22 lines, `plugins\rpgmvp\src\adapters\spng\Decoder.cpp` 359 lines / 86 branches 100 %,
  `plugins\rpgmvp\src\core\Describe.cpp` 100/100, `plugins\rpgmvp\src\core\Format.cpp` 100/100,
  `src\pvd\Exports.cpp` 90 lines / 14 branches 100 %.
- `cmake --build --preset release --parallel 6` (`-review`): 2 steps, 0 warnings; the two skipped timing
  cases with a fresh `PVDKIT_RPGMVP_TIMING_CACHE`: `2 passed | 0 failed | 18 skipped`, 102 assertions,
  29 s; inputs written as `...-rgba8-noise7-v2.rpgmvp` (20,543,091 bytes, 42.8 %) and
  `...-rgba16-noise7-v2.rpgmvp` (51,601,252 bytes, 53.8 %); `libspng 0.7.4, zlib 1.3.2`; stock x64
  medians 197 / 190 ms (RGBA8 host / adapter) and 566 / 540 ms (RGBA16), in line with the report's
  stock rows. The v1 and v2 8-bit inputs decode to byte-identical pixels (System.Drawing, 4000x3000).
- Timing-table arithmetic (nit 4): 188.4/154.5 = 1.22, 185.9/158.6 = 1.17; 535.4/452.0 = 1.18,
  532.7/470.5 = 1.13; 195.7/232.2 = 0.84, 195.8/230.3 = 0.85; 615.8/654.8 = 0.94, 617.7/652.7 = 0.95 —
  DESIGN.md and the report table agree.
- `DecodeTimingTests.cpp:46-53,85,92`: comments say 0..7 / 0..63 / opaque alpha; alpha high byte
  `0xFF`, 16-bit low byte `0xFF` for the alpha channel only; `kSynthesisVersion = 2` is in the cache
  file name (`:157-159`).
- Docs: `lint.ps1:21-25` comments match `ci.yml:63-86`; README `:181-192` (which jobs use the composite
  action, the release job's fallback, the permissions sentence) and `AGENTS.md:76-79` match the
  workflows and `llvm-dir.ps1`; README `:34-35` seed comment present; `ci.yml:101-105` `coverage`
  `needs: build-x64` with the trade-off comment; `ARCHITECTURE.md` scripts list and §4 paragraph match
  the tree. Working tree after the review: the same 26 entries as found (plus this file).
- Still only verifiable by a real run: `gh release view`/`create` with a slash in the tag (gh
  percent-encodes the segment), the `pwsh`/`powershell` mix on the runner, artifact behaviour on
  "Re-run all jobs" (nit 4), and everything the first review listed under that heading.

## Verdict

`ACCEPT`
