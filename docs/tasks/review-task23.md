# Review: Task 23 — zlib-ng evaluation, pack.ps1 / package.ps1, LLVM discovery, GitHub Actions, README Downloads

Reviewed the uncommitted working tree on top of `9667cea` (14 modified, 11 new files; no change under
`src/**` or `plugins/*/src/**`, `plugins/*/package/*` byte-identical to HEAD). Build suffix `-review`.

## Substantive findings

1. `.github/workflows/release.yml:19-20`, `.github/workflows/ci.yml` (no `permissions` block),
   `.github/workflows/build.yml` (no `permissions` block) — `contents: write` is granted at the
   workflow level of `release.yml`, so `verify`, `build-x64` and `build-x86` (which run vcpkg port
   builds and the whole test suite) hold a write token they never use; `ci.yml`/`build.yml` fall back
   to the repository's default token permissions instead of declaring what they need. The review
   brief asks for `contents: write` only where needed — only the `release` job (`gh release create`,
   `git push`) needs it. Fix: `permissions: contents: read` at the top of `ci.yml`, `build.yml` and
   `release.yml`, and `permissions: contents: write` on the `release` job only (a caller job of a
   reusable workflow may set `permissions:` too, so the build jobs can stay read-only).
2. `.github/workflows/release.yml:102-114` — the README "Downloads" push has no retry, and the
   concurrency group `release-${{ github.ref }}` (`:16-17`) is per tag, so two tags pushed in one
   `git push origin avif/v1.1.0 rpgmvp/v1.1.0` (the very first use of this workflow: both plugins are
   at 1.1.0) run two release workflows in lock-step — same verify, same build time, `release` jobs
   starting within seconds of each other. If the second job's `git fetch --depth=1 origin master`
   (`:104`) happens before the first job's `git push origin master` (`:114`) lands, the second push is
   rejected as non-fast-forward; the step fails after the GitHub Release already exists, the README
   row for that plugin stays stale (the orchestrator's requirement that release.yml keeps the table
   current is then not met), and re-running the failed job fails earlier at `gh release create`
   because the release exists. Fix: make the push idempotent under contention — loop a few times
   over `git fetch origin master`, `git rebase origin/master` (or re-run
   `update-readme-downloads.ps1` on the fresh `origin/master`), `git push`, stopping on success;
   alternatively a job-level `concurrency: { group: readme-downloads }` on the `release` job (note
   GitHub keeps only one *pending* run per group, so that alone is safe for two simultaneous tags,
   not three). Either way, a re-run of the README step must not depend on `gh release create`
   succeeding again.

## Nits

1. `scripts/lint.ps1:21-25` — the parameter comments say the CI installs cppcheck "with Chocolatey"
   and BinSkim with `dotnet tool install --global`; `ci.yml:59-82` installs cppcheck from the pinned
   MSI (administrative extract) and BinSkim from the pinned nupkg. The comments describe a plan that
   was not implemented; say "PATH when empty; the CI passes the extracted tool directories".
2. `README.md:181` — "Every job starts with the composite action `.github/actions/toolchain`" is
   false for `release.yml`'s `verify` and `release` jobs (neither uses it; `pack.ps1`'s gates rely on
   `Get-LlvmDir` finding the runner's VS 2022 Enterprise LLVM without `PVDKIT_LLVM_DIR`). Same
   imprecision in `AGENTS.md:78` ("the GitHub workflows set it to the runner's VS 2022 LLVM").
3. `scripts/llvm-dir.ps1:22-23`, `cmake/find-llvm.cmake:11-12` — a `PVDKIT_LLVM_DIR` that points at a
   directory without `clang-cl.exe` is silently skipped and the Build Tools default wins (verified:
   `PVDKIT_LLVM_DIR=C:\nonexistent\llvm\bin` -> Build Tools). `AGENTS.md` calls the variable an
   override; an override that is set but wrong should fail loudly like `-LlvmDir` does
   (`llvm-dir.ps1:15-19`). Harmless on CI (the action derives it from an existing file).
4. `plugins/rpgmvp/DESIGN.md:41-42` — the speed-up ranges are rounded differently from the report's
   table: the adapter pairs give 1.17-1.22x (188.4/154.5, 185.9/158.6), 1.13-1.18x and 0.84x, the
   DESIGN says 1.19-1.22x, 1.14-1.18x and 0.85x. The decision is unaffected; make the two agree.
5. `plugins/rpgmvp/tests/adapters/DecodeTimingTests.cpp:46,61,87` — "+-3 levels" is `+0..7`
   (`random & 0x07` added, never subtracted), and the 16-bit variant's alpha is not opaque: its low
   byte carries `(random >> 24) & 0x3F`, so alpha is 0xFF00..0xFF3F. Neither changes what is
   measured (the 16-bit page is BGRA64 either way); fix the two comments or zero the alpha low byte.
6. `scripts/release-tag.ps1:17` — `-notmatch` is case-insensitive, so the lowercase-only character
   class is decorative (`AVIF/v1.1.0` passes the regex and only fails later on the case-sensitive
   .NET regex against CMakeLists.txt). `-cnotmatch` states the intent; the tag filter in
   `release.yml` is case-sensitive anyway.
7. `.github/workflows/release.yml:39` — `${{ github.ref_name }}` is interpolated into a PowerShell
   double-quoted string (a tag containing `$(...)` would be evaluated). Only collaborators can push
   tags, so not a security finding, but the job already has `RELEASE_TAG` as an env var at `:65`;
   use `$env:GITHUB_REF_NAME` here as well.
8. `scripts/update-readme-downloads.ps1:42-51` — the row is overwritten with whatever version is
   released, so tagging an older line (e.g. `avif/v1.0.1` after 1.2.0) demotes the "Latest version"
   cell (verified: 1.2.0 -> 1.0.0 -> `updated`). Compare `[version]` objects and print `unchanged`
   when the row already names a newer version. Also `(?m)^...$` with `.*` would swallow a `\r` if the
   README ever became CRLF (it is LF today, 0 CRs).
9. `scripts/pack.ps1:128-164` — gates and zips are interleaved per plugin, so a gate failure on the
   second plugin leaves the first plugin's zips in `-DistDir`. `package.ps1` deletes stale zips
   beforehand and the release job never reaches `gh release create`, so no harm today; running all
   gates first and zipping afterwards would make `pack.ps1` atomic on its own.
10. `.github/workflows/ci.yml:97` — `coverage` has no `needs`, so on a cold cache it builds every x64
    port a second time in parallel with `build-x64` (both then try to save the same cache key; the
    second save is a warning). `needs: build-x64` would serialise it behind a warm cache; a
    deliberate trade-off either way, worth a comment.
11. `README.md:34-39` — the seeded rows link to `releases/tag/avif/v1.1.0` and
    `releases/tag/rpgmvp/v1.1.0`, tags that do not exist yet (no remote, 1.1.0 not released through
    this workflow). Fine as a seed if those exact tags are pushed first; a 404 until then.
12. `cmake/find-llvm.cmake:9` — `PVDKIT_LLVM_DIR` given as `-D` reaches the main project only;
    CMake's `try_compile` sub-projects re-run the toolchain file with a fresh cache and resolve the
    directory again from the environment/candidates. Prefer the environment variable (what the
    workflows do) and say so in the comment.
13. `scripts/release-notes.ps1` — the ChangeLog's ` * ` and ` + ` bullets render as two separate
    Markdown lists on GitHub (different bullet characters start a new list). Cosmetic.

## Verified

Environment: `PVDKIT_BUILD_SUFFIX=-review`, `--parallel 6`, one build at a time; `build/debug-review`
removed first so the preset change (no compiler entries in `base`) was exercised from scratch.

- `cmake --preset debug` (from scratch): `Running vcpkg install - done` (all ports restored from the
  binary cache, i.e. the new triplet/toolchain ABI was already built once by the task), `The CXX
  compiler identification is Clang 19.1.5`, `Configuring done (10.3s)`. Cache:
  `PVDKIT_LLVM_DIR:PATH=C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin`,
  `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER`/`CMAKE_LINKER`/`CMAKE_RC_COMPILER` under it — identical to
  the pre-task values.
- `cmake --build --preset debug --parallel 6`: exit 0, 0 `warning` lines, 38.9 s.
  `ctest --preset debug`: `100% tests passed out of 17`, `Total Test time (real) = 68.37 sec`.
- `cmake --preset release` + build: 0 warnings. `ctest --preset release`: `100% tests passed out of
  21` (`avif_check_imports`, `avif_check_exports`, `rpgmvp_check_imports`, `rpgmvp_check_exports`
  included), 32.60 s.
- `cmake --preset release-x86` (new directory): `Restored 11 package(s) from
  C:\Users\Roma\AppData\Local\vcpkg\archives` (every x86 port from cache: the one-time rebuild the
  triplet change forces is done), build 0 warnings. `ctest --preset release-x86`: `100% tests passed
  out of 21`, 35.78 s.
- `scripts/coverage.ps1 -Preset coverage`: `TOTAL 1087 0 100.00% 258 0 100.00% 2139 0 100.00% 694 0
  100.00%`; `Plugin profile check passed for 'avif' ... 18/18 Exports.cpp functions`, same for
  `rpgmvp`; `Coverage source completeness passed: 24 executable source files present.`; `Coverage
  gate passed: lines 100%, branches 100%.` Scope rows: `plugins\rpgmvp\src\DefaultPlugin.cpp 22/22
  lines`, `plugins\rpgmvp\src\adapters\spng\Decoder.cpp 359 lines 100.00% 86 branches 100.00%`,
  `plugins\rpgmvp\src\core\Describe.cpp 100.00%/100.00%`, `plugins\rpgmvp\src\core\Format.cpp
  100.00%/100.00%`.
- Timing instrument on the release-review x64 build with the task's cached inputs
  (`PVDKIT_RPGMVP_TIMING_CACHE`): `rpgmvp_adapter_tests --no-skip=true -tc="RPGMVP release timing*"`
  -> RGBA8 `compressed 22076192 bytes (45.99 %)`, `host path median 196.072 ms`, `adapter ... median
  185.438 ms`; RGBA16 `51920598 bytes (54.08 %)`, host `550.43 ms`, adapter `528.616 ms`; `libspng
  0.7.4, zlib 1.3.2`; `2 passed | 0 failed | 18 skipped`. Same input sizes and the same stock
  numbers as the report (186-198 / 533-548 ms), so the stock side of the decision reproduces; the
  best reported zlib-ng x64 figure is 1.22x < 1.3x and x86 is slower, so "not adopted" follows the
  task's rule. Tree: `ports/` = `libavif`, `libspng`; `vcpkg.json` unchanged; the only `zlib-ng`
  mentions are DESIGN.md, the test's header comment and the report.
- `clang-tidy -p build/debug-review DecodeTimingTests.cpp`: no diagnostics. `lint.ps1 -Tools
  psscriptanalyzer`: `PSScriptAnalyzer: 0 finding(s)`, `lint: clean`. `lint.ps1 -Tools
  cppcheck,binskim,clang-format -BuildDir build/debug-review -ReleaseDir build/release-review
  -CppcheckDir <extracted MSI>\PFiles\Cppcheck -BinSkimDir <nupkg>\tools\net9.0\win-x64` (the exact
  shape ci.yml uses): `clang-format: 0`, `cppcheck: 0`, `BinSkim: 0`, `lint: clean`; the extracted
  cppcheck run separately reports `Checking` for 50 translation units (cfg/ present).
- `scripts/pack.ps1 -Suffix -review -DistDir <scratch>`: both gates printed for all four DLLs
  (`Import policy passed`, `Export policy passed`), four zips with SHA-256, four `PSCustomObject`s
  (Name, Version, Architecture, Zip, Sha256). `7z l`: `AVIF-1.1.0-x64.zip` = AVIF.pvd 2574336,
  ChangeLog 1202, LICENSES.txt 24394, readme_en.txt 1822, readme_ru.txt 3051 -> `5 files`;
  `AVIF-1.1.0-x86.zip` AVIF.pvd 1670144 -> 5 files; `RPGMVP-1.1.0-x64.zip` RPGMVP.pvd 320512 -> 5
  files; `RPGMVP-1.1.0-x86.zip` RPGMVP.pvd 275456 -> 5 files; no `manifest.json` anywhere; the DLL
  sizes equal the report's. Re-run with `-Plugins rpgmvp` (quoted and unquoted `-Suffix -review`):
  AVIF zips and a foreign `SOMETHING-0.0.1-x64.zip` untouched (same timestamps), RPGMVP zips
  rewritten with identical SHA-256.
- pack.ps1 failure paths (fake build trees under `build/release[-x86]-fakerev*`, removed afterwards):
  a plugin whose DLL is a renamed test exe -> `Import policy violation ... found: KERNEL32.dll,
  VERSION.dll`, exit 1, no zip; manifest `9.9.9` vs DLL -> `carries FileVersion '1.1.0', expected
  '9.9.9'`, exit 1; plugin present on x64 only -> `Plugin 'rpgmvp' is built for x64 but not for x86`,
  exit 1; `-Plugins nosuch` -> `Plugin 'nosuch' has no package manifest under ...\build\release-review\plugins (x64)`;
  `-Suffix -nonexistent` -> `...\build\release-nonexistent\plugins does not exist: build the release
  preset for x64 first`. `dist/` in the repository was never written.
- `release-tag.ps1`: `avif/v1.1.0` -> `Id=avif Name=AVIF Version=1.1.0`, `rpgmvp/v1.1.0` OK;
  `avif/v1.2.0` -> `asks for 1.2.0, but ... declares VERSION 1.1.0`; `heic/v1.0.0` -> no such
  CMakeLists; `v1.0.0` -> shape error. `release-notes.ps1 -Id rpgmvp -Zip <two zips> -OutFile`:
  489 bytes, no BOM, 0 CR, the `RPGMVP 1.1.0 14.09.2026` entry with its dashed underline and the two
  bullets, then `SHA-256:` and one `    <zip>  <hash>` line per zip; the AVIF entry stops before
  `AVIF 1.0.0 12.09.2026` although the 1.1.0 text mentions "1.0.0". `update-readme-downloads.ps1` on
  a README copy: `AVIF 1.1.0` -> `unchanged` and byte-identical file; `AVIF 1.2.0` -> `updated`
  (one row changed); `HEIC 0.1.0` -> `updated` (row appended); repeat -> `unchanged`; file stays
  UTF-8 without BOM, LF only.
- `actionlint 1.7.12` over `ci.yml`, `build.yml`, `release.yml`: `Found 0 errors in 3 files`
  (shellcheck/pyflakes rules disabled: not installed). `"$env:ImageOS-$env:ImageVersion"` ->
  `win25-20260907.255.1` (the `-` is not taken as part of the variable name). The runner reads
  `GITHUB_OUTPUT`/`GITHUB_ENV` with `File.ReadAllText` (actions/runner
  `FileCommandManager.cs`), which strips the UTF-8 BOM that Windows PowerShell's single
  `Out-File -Encoding utf8 -Append` in the `verify` step writes at offset 0.
- Pinned downloads: `sha256sum` of the task's `cppcheck-2.21.0-x64-Setup.msi` =
  `a86eef11...bbb24` and of the BinSkim nupkg = `69678989...0c447`, equal to `ci.yml:33,35`; both
  URLs answer (`cppcheck-opensource/cppcheck` release asset 302 -> 200, 23,777,280 bytes;
  api.nuget.org 200, 147,826,813 bytes); `cppcheck.exe --version` -> `Cppcheck 2.21.0`,
  `BinSkim.exe --version` -> `4.4.9.11`. `actions/checkout@v7`, `actions/cache@v6`,
  `actions/upload-artifact@v7`, `actions/download-artifact@v8` all exist (HTTP 200; latest releases
  v7.0.1 / v6.1.0 / v7.0.1 / v8.0.1). `gh 2.100.0` has `--verify-tag`, `--notes-file`, `--title`.
- Read as the runner: `uses: ./repo/.github/actions/toolchain` matches the `path: repo` checkout;
  `VCPKG_ROOT` -> the checked-out `vcpkg/` at `9e593bb1...`, `cmake/vcpkg-root.cmake` reads it;
  `VCPKG_DEFAULT_BINARY_CACHE` is the cached path, key `vcpkg-<arch>-<image>-<hashFiles(vcpkg.json,
  ports, triplets, cmake)>` with two restore keys; the release test presets pass `-C Release`, so the
  table checks run under `pwsh` (found first by `find_program`) with `PVDKIT_LLVM_DIR` from
  `GITHUB_ENV`; upload-artifact's root is the least common ancestor `build/<preset>/plugins`, so the
  download into `repo/build/release[-x86]/plugins` yields `plugins/<id>/<NAME>.pvd` and
  `plugins/<id>/package/...`, exactly what `pack.ps1:61-62,75` discovers; `shell: powershell` steps
  get the runner's `$ErrorActionPreference='stop'` prologue and `exit $LASTEXITCODE` epilogue, so
  `lint.ps1`'s `exit 1` and every `throw` fail the step; a push made with `GITHUB_TOKEN` does not
  trigger `ci.yml`, which is fine for a README-only commit. Roma's machine: no environment variable
  set anywhere in this review; every gate resolved the Build Tools directory.
- Only a real run can verify: `actions/cache` restore/save with `env.IMAGE_KEY` evaluated inside the
  composite action; the vcpkg port builds on the image (meson/NASM/Python for dav1d, x86 vcvars from
  VS Enterprise) and the cold-cache wall time against the 120-minute job timeout; `Install-Module
  PSScriptAnalyzer` under Windows PowerShell 5.1 on `windows-2025`; the two `Invoke-WebRequest`
  downloads and `msiexec /a` on the runner; leak-test noise on a 4-vCPU runner; `gh release create`
  with the job token; the README fetch/commit/push with the persisted checkout credentials and the
  race in finding 2; that `windows-latest` still resolves to an image with VS 2022 Enterprise
  (`Get-LlvmDir`'s fallback in the `release` job has no `PVDKIT_LLVM_DIR`).

## Verdict

`REJECT`
