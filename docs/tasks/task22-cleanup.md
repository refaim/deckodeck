# Task 22 — cleanups and documentation for the 1.1.0 release (product name: deckodeck)

Repository: `C:\Users\Roma\Dev\PictureView3\pvdkit` (the folder keeps this name for now; the product
and the GitHub repository are called **deckodeck**: `https://github.com/refaim/deckodeck`).
Windows, clang-cl 19, C++23, `/MT`, x64 + x86, static plugins for PictureView 3 / Far Manager.
HEAD is `19669d0`, tree clean. `build/` may be empty or stale — configure from scratch.

Read completely before touching anything: `AGENTS.md`, `docs/ARCHITECTURE.md`, `README.md`,
`docs/tasks/review-task20-r2.md` (its three nits are part of this task),
`cmake/pvdkit-package-docs.cmake` (the configure-time check for the package documents).
Rules 1–12 of AGENTS.md apply: TDD (failing test before production code, for every change),
100% line+branch coverage, zero warnings, no new guard tokens, no commits/staging/resets — the
orchestrator commits. Never ask the user to run a command.

## Changes

### 1. Remove `IDecoder::iccProfile()`
The host ignores ICC profiles (proven; the experiment code was already removed). Delete the virtual,
every override in `plugins/*/src/adapters/**`, every test and every document sentence that exists
only for it. Keep `ImageMeta::hasIcc` (the info line prints "ICC"). Remove tests first (they must fail
to compile against the removed API), then the code.

### 2. Info line: "EXIF orientation N" only for N in 2..8
`plugins/avif/src/core/Describe.cpp` prints "EXIF orientation 1", which means "no rotation" and is
noise. Print the item only for 2..8. Test first (orientation 1 → absent; 2 and 8 → present; the
existing irot/imir precedence unchanged).

### 3. Review nits from `docs/tasks/review-task20-r2.md`
- `src/core/colour/Pipeline.cpp`: `static_assert(std::is_trivially_destructible_v<SrgbOutputTables>)`
  next to the function-local static, with the one-line reason (no atexit entry in a plugin DLL).
- `tests/core/colour/PipelineTests.cpp`: the two comment clarifications (shared-tables test is not a
  race test — race-freedom rests on [stmt.dcl]; the exhaustive diagnostic's monotonicity count is
  part of the proof because the UCRT `pow` is dispatched per CPU).

### 4. Product name deckodeck
- Root `CMakeLists.txt`: `project(pvdkit …)` → `project(deckodeck …)`. Check nothing depends on
  `PROJECT_NAME` being `pvdkit` (grep `PROJECT_NAME`, `CMAKE_PROJECT_NAME`, `pvdkit` across
  `cmake/`, `scripts/`, `CMakePresets.json`, plugins).
- `README.md`, `AGENTS.md`, `docs/ARCHITECTURE.md`, `docs/**`, `scripts/*.ps1` comments,
  `plugins/*/README.md`, `plugins/*/DESIGN.md`: where "pvdkit" means the repository or the product,
  say "deckodeck"; where it means the shared kit layer (`src/`, `pvdkit_core`, `pvdkit_plugin_identity`,
  `PVDKIT_*` variables, presets, build-directory names), keep it and explain once in README.md that
  the shared PVD layer is still called pvdkit. **Do not rename any CMake target, variable, function,
  preset, script parameter, directory or file** — this is a documentation/name change only.
  README.md gets a one-paragraph intro naming the product, the two plugins, and the repository
  URL; the "release channel" is GitHub Releases plus the PictureView forum thread.

### 5. Package documents (byte-exact!)
`plugins/*/package/readme_en.txt` (ASCII, CRLF), `readme_ru.txt` and `ChangeLog` (UTF-8 with BOM,
CRLF) are `-text` in `.gitattributes` and byte-checked at configure time. Edit them **only** through
PowerShell `[IO.File]::ReadAllBytes` / `WriteAllBytes` (or an equivalent byte-exact method); never
`sed -i`, never Git Bash redirection, never an editor that may change the BOM or line endings. After
editing, verify bytes: BOM present where required, CR count == LF count, no lone LF/CR, first
ChangeLog line still `<NAME> <VERSION> DD.MM.YYYY`. Look at `C:\Users\Roma\Dev\burlak\dist` (Roma's
other Far plugin) for how the homepage line is placed and worded in his readmes and mirror it.
- Both plugins, `readme_en.txt` and `readme_ru.txt`: add the homepage/source line with
  `https://github.com/refaim/deckodeck`.
- `plugins/avif/package/ChangeLog`, the 1.1.0 entry: add one line in the same plain human Russian
  style as the existing lines saying that HDR images now open several times faster
  (Task 20: about 4× on the decode, e.g. 12-megapixel HDR 2.3 s → 0.55 s). Mirror the line in
  `plugins/avif/README.md` "Changes" if that section lists per-version items.
- RPGMVP ChangeLog: no new line in this task.

## Gates (all green before you report)
`PVDKIT_BUILD_SUFFIX=-t22`, sequentially, `--parallel 6`, one build at a time, never x64 and x86
concurrently: `cmake --preset debug` / `--build` / `ctest`; same for `release`, `debug-x86`,
`release-x86`, `asan`; `scripts/coverage.ps1 -Preset coverage` and `-Preset coverage-x86` (100%
lines and branches); `scripts/lint.ps1 -Jobs 6` for x64 and `-BuildDir build\debug-x86-t22
-ReleaseDir build\release-x86-t22` for x86 (`lint: clean`); `ctest --preset release -R
"_check_(imports|exports)$"` on both architectures. `pwsh` is not on PATH — use
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\<name>.ps1`. Zero warnings. Quote the
summary lines. A rare `leakcheck_tests` flake on x86 (ntdll critical-section self-test) is known:
re-run once, do not chase it.

## Report
`docs/tasks/report-task22.md`: what changed (file:line), TDD evidence (the red runs), the byte
verification of the package documents, the gate outputs. Final message: a short summary.
