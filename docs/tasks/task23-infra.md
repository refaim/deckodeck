# Task 23 — zlib-ng for RPGMVP, `scripts/pack.ps1`, GitHub Actions CI and release workflows

Repository: `C:\Users\Roma\Dev\PictureView3\pvdkit` (product and GitHub repository: **deckodeck**,
`https://github.com/refaim/deckodeck`, currently private, no remote configured yet — do not add one,
do not push, do not commit/stage/reset; the orchestrator commits). Windows, clang-cl 19, C++23,
`/MT`, x64 + x86, static plugins for PictureView 3 / Far Manager. Tree clean at the HEAD you find.

Read completely before touching anything: `AGENTS.md`, `docs/ARCHITECTURE.md`, `README.md`,
`vcpkg.json`, `ports/libspng/*`, `triplets/*`, `cmake/*.cmake`, `CMakePresets.json`,
`scripts/build-all.ps1`, `scripts/package.ps1`, `scripts/lint.ps1`, `scripts/coverage.ps1`,
`scripts/check-imports.ps1`, `scripts/check-exports.ps1`, `plugins/rpgmvp/CMakeLists.txt`,
`plugins/rpgmvp/DESIGN.md`. Rules 1–12 of AGENTS.md apply (TDD for every C++ change, 100%
line+branch coverage, zero warnings, PSScriptAnalyzer-clean scripts, no new guard tokens). Never
edit `plugins/*/package/*` except byte-exactly through PowerShell `[IO.File]::ReadAllBytes` /
`WriteAllBytes` (CRLF; BOM on `readme_ru.txt` and `ChangeLog`; verify CR count == LF count, no lone
LF/CR, first ChangeLog line `<NAME> <VERSION> DD.MM.YYYY`). Never ask the user to run a command.

## Part A — zlib-ng behind libspng (RPGMVP)

Goal: faster inflate for large PNGs with no behaviour change. Steps:
1. Find out what vcpkg offers: `vcpkg search zlib-ng`, the port's `vcpkg.json`/`portfile.cmake` in
   the vcpkg tree (`C:\Users\Roma\scoop\apps\vcpkg\current\ports\zlib-ng`). We need zlib-ng in
   **ZLIB_COMPAT** mode so that libspng and `find_package(ZLIB)` keep working unchanged. If the
   port has no compat feature, add an overlay port `ports/zlib` that builds zlib-ng
   (pinned version, `-DZLIB_COMPAT=ON`, static, our triplets) and installs as `zlib`, so the
   manifest's `libspng` → `zlib` dependency resolves to it. Document the choice in the port and in
   `plugins/rpgmvp/DESIGN.md`. Keep the x86 triplet working (zlib-ng has SSE2/SSSE3/SSE4.2/AVX2
   runtime dispatch on both architectures — verify the dispatch is enabled, not compiled out).
2. Measure before/after with a skipped doctest timing case (like the AVIF ones: not run by CTest,
   `--no-skip=true` runs it): encode a large synthetic image (e.g. 4000×3000 RGBA8 with photo-like
   noise, plus a 16-bit variant) to PNG in memory with libspng's encoder at the test's start, then
   time the RPGMVP decode of it (median of 5) — no fixture files in the repository. Record x64 and
   x86 numbers for stock zlib and zlib-ng in the report. **Adopt zlib-ng only if the inflate-bound
   decode is at least 1.3× faster on x64**; otherwise revert to stock zlib and report the numbers.
3. If adopted: `LICENSES.txt` staging in `plugins/rpgmvp/CMakeLists.txt` must name zlib-ng and its
   licence (zlib licence) with the right copyright file; the RPGMVP info string
   (`pvdkit_plugin_identity` description) names zlib-ng and its version; the configure check that
   prints library versions still works; `plugins/rpgmvp/README.md` and `DESIGN.md` updated; one
   plain human Russian line in the 1.1.0 entry of `plugins/rpgmvp/package/ChangeLog` in the style
   of its neighbours (large PNGs open faster) — byte-exact edit as described above; mirror in
   `plugins/rpgmvp/README.md` Changes if that section lists per-version items. All existing RPGMVP
   tests (including the hostile corpus and leak tests) must pass unchanged — zlib-ng must be
   bit-compatible on output.

## Part B — `scripts/pack.ps1` (package without rebuilding)

New script: packages the plugins from **existing** release build directories, no configure/build,
no lint, no ASan. Parameters: `-Suffix` (default `$env:PVDKIT_BUILD_SUFFIX`), optional `-Plugins`
filter (ids), `-DistDir` (default `dist`). Behaviour: discover
`build/release<Suffix>/plugins/*/package/manifest.json` and `build/release-x86<Suffix>/...`, fail
if a plugin/architecture is missing; run `scripts/check-imports.ps1` and `check-exports.ps1` on each
DLL (they are seconds and gate the artefact); verify the DLL `FileVersion` equals the manifest
version; stage exactly `<NAME>.pvd`, `readme_en.txt`, `readme_ru.txt`, `ChangeLog`, `LICENSES.txt`
(never `manifest.json`); write `dist/<NAME>-<version>-<arch>.zip` (overwrite the same name, do not
delete other zips); print each zip path and its SHA-256, and also emit them as objects on the
pipeline. Then make `scripts/package.ps1` reuse it: package.ps1 = build-all + lint + asan + pack.ps1,
with its zip-cleanup preserved and the duplicated staging code removed. PSScriptAnalyzer clean
(`scripts/lint.ps1 -Tools psscriptanalyzer`). Document in README.md ("Packaging" section) and
AGENTS.md if it lists scripts.

## Part C — GitHub Actions

`.github/workflows/ci.yml` (push to master and pull requests) and `.github/workflows/release.yml`
(tags `avif/vX.Y.Z` and `rpgmvp/vX.Y.Z`). You cannot run GitHub Actions locally; write them
carefully, validate the YAML, dry-run every step's command locally where possible, and keep them
as simple as the gates allow. Design:
- Runner `windows-latest` (VS 2022 Enterprise with clang-cl/LLVM under
  `C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin`, Windows SDK,
  CMake, Ninja, vcpkg at `$env:VCPKG_INSTALLATION_ROOT`, `dotnet`, `gh`). Our scripts and CMake
  files must not assume Roma's scoop paths: make every hardcoded local path a fallback behind an
  environment variable or discovery (`VCPKG_ROOT` already exists — check `cmake/vcpkg-root.cmake`,
  `cmake/clang-cl*.toolchain.cmake`, `cmake/find-ninja.cmake`, `scripts/lint.ps1 -LlvmDir`,
  `scripts/coverage.ps1`, BinSkim/cppcheck discovery, `x86` vcvars if any). Keep the local defaults
  so nothing changes for Roma's machine. The vcpkg `builtin-baseline` must be available: check
  out `microsoft/vcpkg` at that baseline commit into the workspace (actions/checkout with `ref`) and
  point `VCPKG_ROOT` at it, rather than trusting the runner's copy.
- Caching: `actions/cache` on the vcpkg binary cache directory (`%LOCALAPPDATA%\vcpkg\archives` or
  a `VCPKG_DEFAULT_BINARY_CACHE` you set) keyed by the hash of `vcpkg.json`, `ports/**`,
  `triplets/**`, `cmake/**` and the runner image; restore-keys for partial hits.
- Jobs in `ci.yml`: `build-x64` and `build-x86` (configure the `release` / `release-x86` preset,
  build `--parallel`, `ctest` including the import/export table checks, upload the `.pvd` files
  and `package/` as artifacts), `lint` (needs the x64 debug configure for `compile_commands.json`
  plus the release DLLs; installs cppcheck via choco/winget, PSScriptAnalyzer via
  `Install-Module`, BinSkim via `dotnet tool install` or its GitHub release — whichever
  `scripts/lint.ps1` can be pointed at through parameters), `coverage` (x64 `coverage` preset via
  `scripts/coverage.ps1`, fails below 100%). No ASan job. Each job sets `PVDKIT_BUILD_SUFFIX` if the
  scripts need it. Concurrency group per ref, `timeout-minutes` on every job.
- `release.yml`: parse plugin id and version from the tag, fail unless
  `plugins/<id>/CMakeLists.txt` declares that VERSION and `plugins/<id>/package/ChangeLog` starts
  with `<NAME> <version>`; build x64 and x86 (reuse the CI build jobs through `workflow_call` if it
  keeps things simple, otherwise duplicate minimally), `scripts/pack.ps1 -Plugins <id>`, then create
  the GitHub Release with `gh release create <tag> <zips> --title "<NAME> <version>" --notes-file`
  where the notes hold the first ChangeLog entry (converted from CP-agnostic UTF-8 BOM text) and
  the SHA-256 of each zip. Draft: no. Prerelease: no.
- `README.md`: a "Continuous integration and releases" section: what runs where, how to cut a
  release (bump VERSION, ChangeLog entry, tag `avif/v1.2.0`, push tag).

## Gates (all green before you report)
`PVDKIT_BUILD_SUFFIX=-t23`, sequentially, `--parallel 6`, one build at a time, never x64 and x86
concurrently: `debug`, `release`, `debug-x86`, `release-x86`, `asan` (configure, build, ctest);
`scripts/coverage.ps1 -Preset coverage` and `-Preset coverage-x86` (100/100);
`scripts/lint.ps1 -Jobs 6` for x64 and `-BuildDir build\debug-x86-t23 -ReleaseDir
build\release-x86-t23` for x86 (`lint: clean`); `ctest --preset release -R "_check_(imports|exports)$"`
on both architectures; `scripts/pack.ps1 -Suffix -t23` producing four zips in a scratch `-DistDir`
(not `dist/`), listing their contents (`7z l`) to prove exactly five files each; `scripts/package.ps1`
is not run (it rebuilds everything). `pwsh` is not on PATH — use
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\<name>.ps1`. Zero warnings. The known
rare `leakcheck_tests` x86 flake: re-run once, do not chase.

## Report
`docs/tasks/report-task23.md`: decisions (zlib-ng route and numbers, adopted or not), every
file:line, TDD evidence, the byte checks of the package documents, the pack.ps1 run and zip
listings, the workflow design and what could not be verified locally, the gate outputs. Final
message: a short summary.
