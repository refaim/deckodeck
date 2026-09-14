# Task 23 implementation report: zlib-ng measurement, `scripts/pack.ps1`, GitHub Actions

Date: 2026-09-15

Branch: `master`

Starting and final observed `HEAD`: `9667ceab31b51a6f0f08705e65554b0a5da0ce1a` (unchanged)

Build suffix: `-t23`

No commit, staging operation, reset, checkout, clean or worktree was performed; no remote was
added and nothing was pushed. Working-tree edits are the deliverable. Network access: the
zlib-ng 2.3.3 source tarball (vcpkg's download during the measurement builds), the cppcheck
2.21.0 MSI, the BinSkim 4.4.9.11 NuGet package and actionlint 1.7.12 (dry-runs of the workflow
steps, all into the session scratchpad), the GitHub API / raw content for the runner image
inventory and the action versions.

## 1. Part A: zlib-ng behind libspng - measured, **not adopted**

### 1.1 What vcpkg offers and the route taken

At the manifest baseline (`9e593bb18ea69cc5095e012465dcd675a822ed0d`) vcpkg ships `zlib-ng`
2.3.3 as its own port (`ports/zlib-ng/portfile.cmake`: `ZLIB_COMPAT` is a triplet variable,
default OFF, not a feature) that installs under the name `zlib-ng`. It cannot serve here:
libspng depends on the port `zlib`, so the manifest would still install stock zlib 1.3.2 next to
it, and in compat mode the two would collide on `include/zlib.h`. The route the task prescribed
for that case was taken: an overlay port `ports/zlib` (vcpkg.json name `zlib`, version 2.3.3;
portfile derived from vcpkg's zlib-ng portfile with `-DZLIB_COMPAT=ON -DBUILD_TESTING=OFF
-DWITH_RUNTIME_CPU_DETECTION=ON -DWITH_OPTIM=ON -DWITH_NATIVE_INSTRUCTIONS=OFF`, static only,
`vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/ZLIB)`, a `vcpkg-cmake-wrapper.cmake` naming
`zlibstatic.lib` / `zlibstaticd.lib`, `usage`, `LICENSE.md` as the copyright). With it,
`find_package(ZLIB)` resolved to `vcpkg_installed/<triplet>/lib/zlibstatic.lib`, `ZLIB_VERSION`
read `1.3.1` (zlib-ng's compat header: `ZLIB_VERSION "1.3.1.zlib-ng"`, `ZLIBNG_VERSION "2.3.3"`),
libspng rebuilt against it unchanged, `zlibVersion()` at run time answered `1.3.1.zlib-ng`, and
every RPGMVP test passed on both architectures (release presets, hostile corpus and leak tests
included) - bit-compatible output as far as the fixtures go.

Dispatch verification (vcpkg's configure logs of the port,
`buildtrees/zlib/config-{x64,x86}-windows-static-clang-out.log`): `HAVE_SSE2_INTRIN`,
`HAVE_SSSE3_INTRIN`, `HAVE_SSE41_INTRIN`, `HAVE_SSE42_INTRIN`, `HAVE_PCLMULQDQ_INTRIN`,
`HAVE_AVX2_INTRIN`, `HAVE_AVX512_INTRIN`, `HAVE_AVX512VNNI_INTRIN`, `HAVE_VPCLMULQDQ_INTRIN` all
`Success` under clang-cl on **both** triplets (clang-cl accepts the GNU `-mavx2 -mbmi2` style
flags zlib-ng's `detect-intrinsics.cmake` uses for `CMAKE_C_COMPILER_ID MATCHES "Clang"`); the
architecture-specific source list carried `chunkset_sse2/ssse3/avx2/avx512`, `adler32_*`,
`crc32_pclmulqdq`, `crc32_vpclmulqdq`; the feature summary listed `WITH_RUNTIME_CPU_DETECTION`,
`WITH_OPTIM`, `WITH_SSE2` ... `WITH_VPCLMULQDQ` enabled and `WITH_NATIVE_INSTRUCTIONS` disabled;
the ninja log shows `chunkset_avx2.c` compiled with `-mavx2 -mbmi2` on x86 as well. So the
numbers below are zlib-ng with its SIMD kernels dispatched at run time, on a Ryzen 5 5500X3D
(AVX2, no AVX-512).

### 1.2 The instrument: `plugins/rpgmvp/tests/adapters/DecodeTimingTests.cpp` (new, kept)

Two skipped doctest cases (`* doctest::skip()`, lines 239-247; never run by CTest;
`rpgmvp_adapter_tests --no-skip=true -tc="RPGMVP release timing*"` runs them), modelled on the
timing cases in `tests/core/colour/PipelineTests.cpp`:

- `synthesiseRgba` (`:62-92`): a 4000x3000 RGBA image, three linear gradients plus xorshift32
  noise of +-3 levels on the 8-bit samples (`kNoiseMask8 = 0x07`, `:49`) and opaque alpha; the
  16-bit variant puts the 8-bit value in the high byte and 6 bits of noise in the low byte
  (`kNoiseMaskLow16 = 0x3F`, `:50`). A first draft with +-31 levels of noise compressed to 70 %
  (RGBA8) and 92 % (RGBA16) of the raw size - closer to a stored stream than to a photograph;
  its numbers are in 1.3 too.
- `encodePng` (`:95-118`): libspng's encoder in memory (`SPNG_CTX_ENCODER`,
  `SPNG_ENCODE_TO_BUFFER`, `spng_encode_image(..., SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE)`,
  `spng_get_png_buffer`; the malloc'd buffer sits in a `unique_ptr` with a `free` deleter -
  test code, the guard scans `src/` only).
- `wrapRpgmvp` (`:121-133`): the RPG Maker container (16-byte header, PNG bytes 0..15 XOR-ed
  with a fake key - the decoder substitutes the invariant prefix - then the PNG from byte 16).
- `timingInput` (`:149-175`): synthesises on demand; with `PVDKIT_RPGMVP_TIMING_CACHE=<dir>`
  (`cacheDirectory`, `:136-146`, `GetEnvironmentVariableW` - `std::getenv` is a `/WX`
  deprecation error under the UCRT) the encoded file is written once and reused, so two builds
  decode **identical bytes** (deflate streams differ between zlib implementations, which would
  otherwise confound the comparison).
- `measureHostDecode` (`:192-205`): the production plugin (`pvd::makePlugin(options)`), open in
  memory mode, `decodePage(0)`, `freePage` - what the host pays, the fresh 48/96 MB page's
  first-touch faults included; `measureAdapterDecode` (`:208-219`): `DecoderFactory::create` +
  `decodeFrame` into a buffer that already exists (parse, inflate, unfilter, R/B swap only).
  Both: median of 5 runs after one warm-up (`medianMilliseconds`, `:178-189`). `reportTiming`
  (`:221-236`) prints the compressed size, its ratio to the decoded page, both medians, MB/s of
  output and `libraryVersions()` so the log names the zlib in use.

No fixture file was added to the repository.

### 1.3 Numbers (Release presets, `--parallel 6` builds, machine otherwise idle)

Identical cached inputs for stock and zlib-ng. RGBA8: 22,076,192 bytes compressed (46.0 % of
the 48,000,000-byte page); RGBA16: 51,920,598 bytes (54.1 % of 96,000,000). Two runs each; the
adapter number is the inflate-bound one, the host number is what the user sees.

| Architecture | Input | stock zlib 1.3.2 host / adapter (run 1; run 2) | zlib-ng 2.3.3 host / adapter (run 1; run 2) | speed-up (adapter) |
| --- | --- | --- | --- | --- |
| x64 | RGBA8 | 192.2 / 188.4 ms; 198.4 / 185.9 ms | 160.8 / 154.5 ms; 171.0 / 158.6 ms | 1.17-1.22x |
| x64 | RGBA16 | 546.0 / 535.4 ms; 548.2 / 532.7 ms | 465.6 / 452.0 ms; 471.6 / 470.5 ms | 1.13-1.18x |
| x86 | RGBA8 | 204.1 / 195.7 ms; 205.9 / 195.8 ms | 247.1 / 232.2 ms; 242.4 / 230.3 ms | **0.84-0.85x (slower)** |
| x86 | RGBA16 | 627.3 / 615.8 ms; 669.8 / 617.7 ms | 667.4 / 654.8 ms; 668.3 / 652.7 ms | **0.94-0.95x (slower)** |

The first draft's noisier input (70 % / 92 % ratios, host path only, one run each): x64 RGBA8
190.2 ms -> 154.4 ms (1.23x), RGBA16 518.0 ms -> 424.3 ms (1.22x); x86 stock 200.7 / 558.6 ms.

Decision: the bar was 1.3x on x64 for the inflate-bound decode; the best x64 figure is 1.22x
and the x86 build is slower with zlib-ng than with stock zlib. **zlib-ng is not adopted.** The
overlay port was deleted again, the `-t23` build directories were removed and rebuilt from
scratch on stock zlib, and none of the step-3 changes (LICENSES, info string, README, ChangeLog)
was made. The decode is not inflate-bound: the host and adapter numbers differ by only 4-15 ms
(allocation and page faults are cheap), so the ~30-35 ms zlib-ng saves on x64 is the inflate
share of a decode whose larger share is libspng's own row handling (unfiltering, format
conversion, per-row `inflate` calls) plus the adapter's byte-wise R/B swap
(`plugins/rpgmvp/src/adapters/spng/Decoder.cpp:298-302`). If the decode is ever to be made
faster, that swap loop (a per-byte `std::swap` the compiler does not vectorise well) and
libspng's 16-bit path are where the time is; a faster inflate alone caps at ~1.25x. Recorded in
`plugins/rpgmvp/DESIGN.md` (new section "Deflate implementation: stock zlib (zlib-ng measured
and not adopted)" after the libspng CMP0091 paragraph). The timing cases stay in the tree as the
instrument for a later comparison.

A note for anyone re-trying: a build directory configured before an overlay `zlib` port existed
keeps FindZLIB's cached `ZLIB_LIBRARY_RELEASE`/`_DEBUG` (`zs.lib`), which no longer exist after
the switch; configure from scratch (as the CI does) rather than in place.

## 2. Part B: `scripts/pack.ps1`, `scripts/package.ps1` refactored

### 2.1 `scripts/pack.ps1` (new)

Parameters: `-Suffix` (default `$env:PVDKIT_BUILD_SUFFIX`), `-Plugins` (ids, separate arguments
or comma-separated - `powershell -File` hands `-Plugins a,b` over as one string, split at `:39`),
`-DistDir` (default `dist`; a relative path resolves against the repository root, `:40-43`).

Behaviour, in order: `Get-BuiltPluginList` (`:51-100`) per architecture over
`build/release<Suffix>/plugins` and `build/release-x86<Suffix>/plugins` - throws if the
directory is missing (`:59`), discovers `plugins/<id>/package/manifest.json` (the manifest must
sit exactly two levels below `plugins/`), applies the filter, checks the manifest architecture,
finds exactly one `<file>` under the plugin directory, requires the four documents staged next
to the manifest, throws for a requested id without a manifest or for an empty list; then the id
sets of the two architectures must agree (`:109-121`, "built for x64 but not for x86"). For
every entry: `check-imports.ps1` and `check-exports.ps1` through a child of the same PowerShell
edition (`:46`: `pwsh` on the runner, `powershell` here), `FileVersion` == manifest version
(`:135-138`), staging of exactly `<NAME>.pvd`, `readme_en.txt`, `readme_ru.txt`, `ChangeLog`,
`LICENSES.txt` in `%TEMP%\pvdkit-pack-<pid>` (`:125`, removed in `finally`),
`Compress-Archive` (`:153`) to `<DistDir>/<NAME>-<version>-<arch>.zip` after deleting only that
name, the zip path and SHA-256 on the information stream (`:155-156`, `Write-Information
-InformationAction Continue`, as `build-all.ps1` prints), and one object (`Name`, `Version`,
`Architecture`, `Zip`, `Sha256`) per zip, emitted at the end (`:171`).

### 2.2 `scripts/package.ps1` (rewritten around it)

Unchanged: the zip cleanup before anything is built (`:18-21`), `build-all.ps1 -Suffix -Clean`
(`:22-29`), `lint.ps1` on both release directories (`:34-41`), the `asan` preset from scratch
(`:48-77`). Removed: the FileVersion check, the staging directory under `dist/`, the
`Compress-Archive` loop and the SHA-256 printing (all now in `pack.ps1`). New last line
(`:83`): `& pack.ps1 -Suffix $Suffix -DistDir <repo>/dist`, whose objects flow to the caller's
pipeline. `package.ps1` itself was not run (it rebuilds everything from scratch); its parts were
run separately by the gates in section 5 and `pack.ps1` on the gate builds.

### 2.3 Runs

`scripts/pack.ps1 -Suffix -t23 -DistDir <scratchpad>\dist-t23` on the final gate builds (each
zip preceded by the full `llvm-readobj` import/export listing, "Import policy passed: KERNEL32.dll
is the only imported module." and "Export policy passed: the eight PVD entry points are exported
under their bare names."):

```
...\dist-t23\AVIF-1.1.0-x64.zip
  SHA-256: 21219ceb4ab4343f53fc79ec1bfa987f3bf1c4e4c8ba977f39dfe97f04c61fd9
...\dist-t23\RPGMVP-1.1.0-x64.zip
  SHA-256: 011758c0118dd3dfb9f468200265bd2d74765c7fe0fc72d7d7662c2c15b98912
...\dist-t23\AVIF-1.1.0-x86.zip
  SHA-256: 9047de00000e97d3fd4ba2aab887595c432114decfcd6ef1bf586fd977b250a6
...\dist-t23\RPGMVP-1.1.0-x86.zip
  SHA-256: ede3f1efe9d3175a4396c94c0564b42dd82ee3c873ddfb2d99c38dc94e3f4530
```

followed by the object table (Name, Version, Architecture, Zip, Sha256). `7z l` on each:

```
AVIF-1.1.0-x64.zip:   AVIF.pvd 2574336, ChangeLog 1202, LICENSES.txt 24394, readme_en.txt 1822, readme_ru.txt 3051   -> 5 files
AVIF-1.1.0-x86.zip:   AVIF.pvd 1670144, ChangeLog 1202, LICENSES.txt 24394, readme_en.txt 1822, readme_ru.txt 3051   -> 5 files
RPGMVP-1.1.0-x64.zip: ChangeLog 580, LICENSES.txt 2866, readme_en.txt 1204, readme_ru.txt 1856, RPGMVP.pvd 320512    -> 5 files
RPGMVP-1.1.0-x86.zip: ChangeLog 580, LICENSES.txt 2866, readme_en.txt 1204, readme_ru.txt 1856, RPGMVP.pvd 275456    -> 5 files
```

No `manifest.json` in any of them. An earlier run on the same DLLs (before the gates
re-configured and re-staged the package documents, which changes their timestamps and therefore
the zip bytes) produced `c707f4e7...`, `116e729a...`, `6ffedaff...`, `62bd5214...`; a repeat with
`-Plugins rpgmvp` rewrote only the two RPGMVP zips with identical SHA-256 (`Compress-Archive` is
deterministic for identical inputs) and left the AVIF zips untouched; `-Plugins nosuch` ->
"Plugin 'nosuch' has no package manifest under ...\build\release-t23\plugins (x64)", non-zero
exit; `-Suffix -nonexistent` -> "...\build\release-nonexistent\plugins does not exist: build the
release preset for x64 first", non-zero exit. Nothing was written to `dist/`.

## 3. Part C: GitHub Actions

### 3.1 Every hard-coded machine path became a default behind discovery

- `cmake/find-llvm.cmake` (new): resolves the LLVM bin directory into the cache entry
  `PVDKIT_LLVM_DIR` - explicit `-DPVDKIT_LLVM_DIR`, then `$ENV{PVDKIT_LLVM_DIR}`, then
  `C:/Program Files (x86)/.../2022/BuildTools` (Roma's, first so nothing changes locally),
  `C:/Program Files/.../2022/{Enterprise,Professional,Community,BuildTools}`
  (`VC/Tools/Llvm/x64/bin` in each), then `find_program(clang-cl)`; `FATAL_ERROR` otherwise.
- `cmake/clang-cl.toolchain.cmake`, `cmake/clang-cl-x86.toolchain.cmake`:
  `include(find-llvm.cmake)` and `${PVDKIT_LLVM_DIR}/clang-cl.exe` / `lld-link.exe` /
  `llvm-rc.exe` instead of the literal path (the rest unchanged).
- `triplets/x64-windows-static-clang.cmake`, `triplets/x86-windows-static-clang.cmake`:
  `set(VCPKG_ENV_PASSTHROUGH_UNTRACKED PVDKIT_LLVM_DIR)` - vcpkg builds every port in a cleaned
  environment, so the chainload toolchain's first choice must be let through; untracked because
  the compiler itself is already in every port's ABI hash through vcpkg's compiler detection.
  (Changing the toolchain/triplet files rebuilt every port once, as documented.)
- `CMakePresets.json`: the three `CMAKE_C_COMPILER` / `CMAKE_CXX_COMPILER` / `CMAKE_LINKER`
  literals removed from the `base` preset - the chainload toolchain (included by vcpkg.cmake for
  the project too) supplies them. Verified: the from-scratch `release-t23` cache reads
  `PVDKIT_LLVM_DIR:PATH=C:/Program Files (x86)/.../BuildTools/VC/Tools/Llvm/x64/bin` and
  `CMAKE_CXX_COMPILER`, `CMAKE_LINKER`, `CMAKE_RC_COMPILER` under it - the same values as before.
- `scripts/llvm-dir.ps1` (new, dot-sourced): `Get-LlvmDir [-Requested <dir>]` with the same
  order (an explicit directory must contain `clang-cl.exe`; `$env:PVDKIT_LLVM_DIR`; the VS 2022
  layouts from `%ProgramFiles(x86)%` / `%ProgramFiles%`; `clang-cl.exe` on PATH).
- `scripts/check-imports.ps1:10-11`, `scripts/check-exports.ps1:16-17`,
  `scripts/coverage.ps1:109-110`: the literal LLVM directory replaced by `Get-LlvmDir`.
- `scripts/lint.ps1`: `-LlvmDir` defaults to the resolved directory (`:18-20`, `:53-54`); new
  `-CppcheckDir` and `-BinSkimDir` (`:21-25`, normalised `:55-60`, consumed by `Get-Tool` at
  `:239` and `:324`) so the CI can point at tools that are not on PATH; PATH stays the default.
- `cmake/vcpkg-root.cmake` and `cmake/find-ninja.cmake` already honoured `VCPKG_ROOT`; Ninja is
  found on PATH first (the runner has it); BinSkim and cppcheck had no hard-coded paths. The
  only remaining literal machine paths are the documented defaults.

Local defaults verified: `Get-LlvmDir` with nothing set returns the Build Tools directory; every
gate in section 5 ran with no environment variable set.

### 3.2 The workflows (`.github/`)

- `.github/actions/toolchain/action.yml` (composite; expects the repository at `repo/`): reads
  `builtin-baseline` from `repo/vcpkg.json` (fails unless a 40-hex commit), checks out
  `microsoft/vcpkg` at that commit into `vcpkg/` (`actions/checkout@v7` with `ref:`), runs
  `bootstrap-vcpkg.bat -disableMetrics`, creates `vcpkg-binary-cache/`, finds
  `C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\Llvm\x64\bin\clang-cl.exe` (any VS
  major as a fallback), prints `clang-cl --version`, exports `VCPKG_ROOT`,
  `VCPKG_DEFAULT_BINARY_CACHE`, `PVDKIT_LLVM_DIR` and `IMAGE_KEY=<ImageOS>-<ImageVersion>`
  through `GITHUB_ENV`, then `actions/cache@v6` on `vcpkg-binary-cache` with the key
  `vcpkg-<arch>-<image>-<hashFiles(repo/vcpkg.json, repo/ports/**, repo/triplets/**, repo/cmake/**)>`
  and the restore keys `vcpkg-<arch>-<image>-`, `vcpkg-<arch>-`.
- `.github/workflows/build.yml` (reusable, `workflow_call` with `arch`): one job on
  `windows-latest`, `timeout-minutes: 120`, `working-directory: repo`: checkout, the toolchain
  action, `cmake --preset release|release-x86`, `cmake --build --preset ... --parallel`,
  `ctest --preset ...` (the release test presets carry `-C Release`, so `<id>_check_imports` /
  `<id>_check_exports` run), `actions/upload-artifact@v7` of `build/<preset>/plugins/*/*.pvd`
  and `build/<preset>/plugins/*/package/**` as `plugins-<arch>` (artifact root = `plugins/`, so a
  download lands as `plugins/<id>/<NAME>.pvd` + `plugins/<id>/package/...`, what `pack.ps1`
  discovers).
- `.github/workflows/ci.yml` (`push` to `master`, `pull_request`; `concurrency: ci-<ref>`,
  cancel-in-progress): `build-x64`, `build-x86` (`uses: ./.github/workflows/build.yml`); `lint`
  (`needs: build-x64`, 60 min): the toolchain action, `cmake --preset debug` (configure only:
  `compile_commands.json`, the generated headers, the vcpkg headers), download `plugins-x64`
  into `repo/build/release/plugins`, cppcheck 2.21.0 from its release MSI (SHA-256 pinned,
  `msiexec /a ... TARGETDIR=` administrative extract, no system install ->
  `PFiles\Cppcheck\cppcheck.exe`), BinSkim 4.4.9.11 from its NuGet package (SHA-256 pinned,
  `Expand-Archive`, `tools\net9.0\win-x64\BinSkim.exe` - the same payload the scoop install
  here uses; `Microsoft.CodeAnalysis.BinSkim` is not a `dotnet tool` package),
  `Install-Module PSScriptAnalyzer` under Windows PowerShell, then
  `scripts\lint.ps1 -BuildDir build\debug -ReleaseDir build\release -CppcheckDir ... -BinSkimDir ... -Jobs 4`
  under `shell: powershell` (the edition the scripts are developed and gated with here);
  `coverage` (120 min): the toolchain action, `scripts\coverage.ps1 -Preset coverage` under
  `shell: powershell` (configure, build, test, fail below 100/100). No ASan job. No
  `PVDKIT_BUILD_SUFFIX` on CI (the scripts default to the unsuffixed directories).
- `.github/workflows/release.yml` (tags `avif/v*`, `rpgmvp/v*`; `concurrency: release-<ref>`;
  `permissions: contents: write`): `verify` (10 min; `scripts/release-tag.ps1 -Tag <ref_name>`,
  outputs `id`, `name`, `version`), `build-x64` / `build-x86` (`needs: verify`, the reusable
  workflow), `release` (30 min; `needs` all three): checkout, download both artifacts into
  `repo/build/release/plugins` and `repo/build/release-x86/plugins`,
  `scripts\pack.ps1 -Plugins <id> -DistDir dist` (must yield exactly two objects; its table
  gates run through `check-*.ps1`, whose `Get-LlvmDir` falls back to the runner's `Enterprise`
  layout - no toolchain action in this job), `scripts\release-notes.ps1 -Id <id> -Zip <the two
  zips> -OutFile dist\notes.md`, `gh release create <tag> <zips> --title "<NAME> <version>"
  --notes-file dist\notes.md --verify-tag` (`GH_TOKEN` = `github.token`; not a draft, not a
  prerelease), then the README row: `git fetch --depth=1 origin master`, `git checkout -B master
  origin/master`, `scripts\update-readme-downloads.ps1 -Name <NAME> -Version <v> -Tag <tag>
  -RepositoryUrl <server>/<repository>`; when it prints `updated`: the `github-actions[bot]`
  identity, `git add README.md`, `git commit -m "README: <NAME> <version> released"`,
  `git push origin master`; when it prints `unchanged`, the step ends there (a row already
  current is tolerated).
- New helper scripts (PSScriptAnalyzer-clean, dry-runs in 3.3):
  - `scripts/release-tag.ps1 -Tag <id>/vX.Y.Z`: parses the tag, requires
    `plugins/<id>/CMakeLists.txt`, extracts `NAME` and `VERSION` from its
    `pvdkit_plugin_identity(<id> NAME ... VERSION ...)` call, requires equality with the tag,
    requires the ChangeLog (UTF-8 BOM) to open with `<NAME> <version> DD.MM.YYYY`; emits
    `{Id, Name, Version, Tag}`.
  - `scripts/release-notes.ps1 -Id <id> -Zip <paths> [-OutFile]`: the first ChangeLog entry
    (BOM dropped, CRLF -> LF, everything before the next `<NAME> X.Y.Z DD.MM.YYYY` header,
    trailing blank lines trimmed) followed by `SHA-256:` and one `    <zip name>  <sha256>` line
    per zip; UTF-8 without BOM.
  - `scripts/update-readme-downloads.ps1 -Name -Version -Tag [-RepositoryUrl] [-Readme]`:
    replaces the row `| <NAME>.pvd | ... |` between `<!-- downloads:begin -->` and
    `<!-- downloads:end -->` (appends one for a new plugin), writes only when the row changed
    (UTF-8 without BOM, the file's LF endings preserved), prints `updated` or `unchanged`.
- `README.md`: the "Downloads" section with the anchored table seeded at
  `AVIF.pvd | 1.1.0 | avif/v1.1.0` and `RPGMVP.pvd | 1.1.0 | rpgmvp/v1.1.0`, linking to
  `https://github.com/refaim/deckodeck/releases/tag/<tag>`; the LLVM/vcpkg discovery paragraph
  under "Build"; the "Packaging" subsection; `lint.ps1`'s new parameters; the "Continuous
  integration and releases" section (what runs where; how to cut a release: bump `VERSION`,
  ChangeLog entry, merge, `git tag avif/v1.2.0`, `git push origin avif/v1.2.0`); the layout
  inventory. `AGENTS.md` (toolchain bullet: the directory is the first default of the two
  resolvers, never hard-code it elsewhere). `docs/ARCHITECTURE.md` (section 0 script inventory;
  section 4: the `pack.ps1` / `package.ps1` / workflows / LLVM-discovery paragraph replacing the
  old `package.ps1` description) - design-document edits limited to describing the new scripts;
  no interface changed.

### 3.3 What was verified locally and what could not be

Verified:

- `actionlint 1.7.12` over `.github/workflows/*.yml`: 0 errors, with a negative control in a
  scratch repository (an undefined reusable-workflow input, an unknown `needs` and an undefined
  `needs.<job>` output were all reported, so the tool really checked, `build.yml`'s
  `inputs.arch` contract included); all four YAML files (the composite action included) parsed
  with Deno's `@std/yaml`.
- The composite action's commands: the baseline read (`9e593bb1...`); the `Get-ChildItem`
  wildcard-in-the-middle glob (it matches Roma's `2022\BuildTools\VC\Tools\Llvm\x64\bin` under
  `Program Files (x86)`; the any-major glob matches it too).
- The lint job's tool acquisition, byte for byte: the cppcheck MSI downloads with SHA-256
  `a86eef1180dc18fde46b08f4e4b12f2a1ead8cfc0dd6c294aac8757748cbbb24`, `msiexec /a` extracts to
  `PFiles\Cppcheck`, `cppcheck.exe --version` -> `Cppcheck 2.21.0`; the BinSkim nupkg downloads
  with SHA-256 `69678989cbc273b5b50fcf98fb0fd978e1e35a3f844acb24254b31f0ce90c447`,
  `tools\net9.0\win-x64\BinSkim.exe --version` -> `4.4.9.11`; then
  `lint.ps1 -Tools cppcheck,binskim -BuildDir build\debug-t23 -ReleaseDir build\release-t23
  -CppcheckDir <extracted> -BinSkimDir <extracted>`: `cppcheck: 0 finding(s)`, `BinSkim: 0
  finding(s)`, `lint: clean`.
- The release job's scripts: `release-tag.ps1` accepts `avif/v1.1.0` and `rpgmvp/v1.1.0` and
  rejects `avif/v1.2.0` ("asks for 1.2.0, but ... declares VERSION 1.1.0"), `heic/v1.0.0` (no
  such plugin) and `v1.0.0` (shape); `release-notes.ps1` on the RPGMVP zips wrote the expected
  Markdown (the `RPGMVP 1.1.0 14.09.2026` entry, then the two `SHA-256` lines; UTF-8, no BOM, no
  CR); `update-readme-downloads.ps1` on a copy of README.md: `AVIF 1.1.0` -> `unchanged` and no
  write, `AVIF 1.2.0` -> `updated` with exactly that row changed, `HEIC 0.1.0` -> `updated` with
  a new row appended, the file still UTF-8/LF; `gh release create --help` lists
  `--verify-tag`, `--notes-file`, `--title` (gh 2.100.0 here and on the runner image).
- The runner inventory (`actions/runner-images`, `Windows2025-Readme.md`, image 20260907.255.1,
  what `windows-latest` resolves to today): VS Enterprise 2022 17.14 with `VC.Llvm.Clang`
  (clang-cl 19.1.5, the same as here), MSVC 14.44, CMake 3.31.6, Ninja 1.13.2, Python 3.12,
  PowerShell 7.6.5 and Windows PowerShell, gh 2.100.0, 7zip, Chocolatey; a standalone LLVM
  20.1.8 is also installed, which is why the discovery prefers the VS layout over PATH. The
  action versions used are the current majors (checkout v7, cache v6, upload-artifact v7,
  download-artifact v8).

Not verifiable here (no remote, no runner): an actual run of the three workflows -
`actions/cache` behaviour, the vcpkg port builds on the runner (dav1d's meson needs the runner's
Python and NASM, which vcpkg downloads), the wall time (a cold cache builds every port once per
architecture; estimate 20-40 minutes per build job, then cached), the leak tests' noise on a
2-4 vCPU runner (the `LoadLibrary/FreeLibrary` view count flaked once here under ASan, section
5), `Install-Module` on the runner, the `gh release create` call, and the `git push` of the
README row (branch protection would reject it; none is configured; a commit pushed with
`GITHUB_TOKEN` does not trigger CI, which is fine for a README-only change). The composite
action's `Out-File -Encoding utf8 -Append` on `GITHUB_ENV` / `GITHUB_OUTPUT` is the documented
PowerShell idiom. `windows-latest` currently is `windows-2025`; if GitHub moves the label to a
VS 2026 image, the any-major glob will find that clang-cl and the build may then need a
newer-compiler pass - pin `runs-on: windows-2025` in the three files if that happens first.

## 4. TDD evidence

- Part A: the timing cases were written first (the instrument), then the port was added and the
  measurement taken; the decision is recorded and no production code changed.
  `DecodeTimingTests.cpp` is compiled into `rpgmvp_adapter_tests` on every preset (its cases are
  skipped, so CTest counts are unchanged; `--no-skip=true` reports "2 passed, 18 skipped"). No
  line under `src/**` or `plugins/*/src/**` changed; coverage stays at 100/100 on both
  architectures (section 5). The first compile of the test hit `/WX` on `std::getenv`
  (deprecated under the UCRT) and was changed to `GetEnvironmentVariableW`.
- Parts B/C are PowerShell, CMake and YAML, exercised by running them (sections 2.3, 3.3, 5) and
  by `scripts/lint.ps1 -Tools psscriptanalyzer` (0 findings after one iteration: the first run
  reported `PSReviewUnusedParameter` for `-CppcheckDir`/`-BinSkimDir`, whose only uses were
  inside functions; normalising them at the top level fixed it).

## 5. Gates

All with `PVDKIT_BUILD_SUFFIX=-t23`, `--parallel 6`, one build at a time, never x64 and x86
concurrently (a driver script in the session scratchpad ran them sequentially and logged every
step; every build log has zero `warning` lines).

| Gate | Result |
| --- | --- |
| `cmake --preset debug` / build / `ctest --preset debug` | configure 11 s, build 43 s, **100% tests passed, 17/17** (74 s) |
| `release` (configure / build no-op / ctest) | **100% tests passed, 21/21** (32 s; incl. `avif_check_imports/exports`, `rpgmvp_check_imports/exports`, `*_package_docs`) |
| `debug-x86` | configure 14 s, build 45 s, **100% tests passed, 17/17** (85 s) |
| `release-x86` | **100% tests passed, 21/21** (39 s) |
| `asan` (configure / build / ctest) | first run 16/17: `rpgmvp_leak_tests` "LoadLibrary / FreeLibrary cycled": `views +2` in the first pass, `+0` in the second (the ASan runtime's own `MEM_MAPPED` regions; no heap block, byte or handle moved; all other scenarios +0); re-run once as the task allows for this kind of flake: **100% tests passed, 17/17** |
| `scripts/coverage.ps1 -Preset coverage` | `TOTAL 1087 regions, 258 functions, 2139 lines, 694 branches - 100.00% / 100.00% / 100.00% / 100.00%`; plugin profile check passed for `avif` (18/18 Exports.cpp functions) and `rpgmvp` (18/18); source completeness 24/24; **Coverage gate passed: lines 100%, branches 100%** (122 s) |
| `scripts/coverage.ps1 -Preset coverage-x86` | the same totals, **lines 100%, branches 100%** (160 s) |
| `scripts/lint.ps1 -Jobs 6` (x64: `build/debug-t23`, `build/release-t23`) | clang-format 0, clang-tidy 0 (311 s), cppcheck 0, PSScriptAnalyzer 0, BinSkim 0 -> **lint: clean** |
| `scripts/lint.ps1 -Jobs 6 -BuildDir build\debug-x86-t23 -ReleaseDir build\release-x86-t23` | clang-format 0, clang-tidy 0 (314 s), cppcheck 0, PSScriptAnalyzer 0, BinSkim 0 -> **lint: clean** |
| `ctest --preset release -R "_check_(imports|exports)$"` | 4/4 passed (`avif_check_imports`, `avif_check_exports`, `rpgmvp_check_imports`, `rpgmvp_check_exports`) |
| `ctest --preset release-x86 -R "_check_(imports|exports)$"` | 4/4 passed |
| `scripts/pack.ps1 -Suffix -t23 -DistDir <scratch>` + `7z l` | four zips, five files each (section 2.3) |
| `scripts/package.ps1` | not run (rebuilds everything), as instructed |

The timing cases themselves (`--no-skip=true`) ran on every measurement build: `Status: SUCCESS`
each time (section 1.3).

## 6. Package documents

Not touched: Part A was not adopted, so the ChangeLog line ("large PNGs open faster") was not
added, and `plugins/*/package/*` are byte-identical to `HEAD` (`git status` does not list them).
No byte check was therefore needed; the configure-time `pvdkit_check_package_docs` and the
`<id>_package_docs` ctest entries ran on every preset regardless.

## 7. Files

Modified: `AGENTS.md`, `CMakePresets.json`, `README.md`, `cmake/clang-cl-x86.toolchain.cmake`,
`cmake/clang-cl.toolchain.cmake`, `docs/ARCHITECTURE.md`, `plugins/rpgmvp/DESIGN.md`,
`scripts/check-exports.ps1`, `scripts/check-imports.ps1`, `scripts/coverage.ps1`,
`scripts/lint.ps1`, `scripts/package.ps1`, `triplets/x64-windows-static-clang.cmake`,
`triplets/x86-windows-static-clang.cmake`.

New: `.github/actions/toolchain/action.yml`, `.github/workflows/build.yml`,
`.github/workflows/ci.yml`, `.github/workflows/release.yml`, `cmake/find-llvm.cmake`,
`plugins/rpgmvp/tests/adapters/DecodeTimingTests.cpp`, `scripts/llvm-dir.ps1`,
`scripts/pack.ps1`, `scripts/release-notes.ps1`, `scripts/release-tag.ps1`,
`scripts/update-readme-downloads.ps1`, this report.

Created and removed again during the task: `ports/zlib/` (the zlib-ng overlay port),
`build/release-t23stock`, `build/release-x86-t23stock` (the stock-zlib comparison builds). The
`-t23` build directories remain under `build/` (ignored).

## Fix round 1 (after `docs/tasks/review-task23.md`)

Same rules as the task: no git state change, no remote, no push; `plugins/*/package/*` untouched.
Every item of the review, in its numbering.

### Substantive

1. Permissions. `.github/workflows/ci.yml:13-14` and `.github/workflows/build.yml:15-16`: a
   workflow-level `permissions: contents: read` (with a one-line comment each); a caller job of a
   reusable workflow cannot grant more than it holds, so the build jobs run read-only from both
   callers. `.github/workflows/release.yml:23-24`: workflow-level `contents: read`; the `release`
   job alone carries `permissions: contents: write` (`:65-66`) for `gh release create` and the
   README push. `verify`, `build-x64` and `build-x86` therefore hold read-only tokens.
   `actions/checkout` needs `contents: read`; `actions/cache`, `upload-artifact` and
   `download-artifact` (same run) use the runtime token, not `GITHUB_TOKEN`.
2. README push race and re-run safety (`release.yml:104-149`).
   - "Create the GitHub Release (kept as is when it already exists)" (`:104-120`): `gh release
     view <tag> --json assets --jq '.assets[].name'` first; if the release exists, the step
     checks that both zip names are among its assets and returns without creating or uploading
     anything (a missing asset is an error with the instruction to `gh release delete` and
     re-run - the notes' SHA-256 keep describing the assets that were published, which
     re-uploading a rebuilt zip would silently break); otherwise `gh release create ...
     --verify-tag`, its exit code checked explicitly. Documented in the step comment and in the
     README's release bullet.
   - "Point the README "Downloads" row at this release" (`:126-149`): the bot identity is set
     once; then up to five attempts (`$attempts = 5`, `:131`), each `git fetch --depth=1 origin
     master`, `git checkout -B master origin/master` (a fresh master every time - the previous
     attempt's unpushed commit is discarded, so the row is re-applied on top of whatever landed),
     `update-readme-downloads.ps1` on it, return when it prints `unchanged` (a re-run, the other
     job already updated the row, or a newer version is listed), else commit and `git push origin
     master`; success returns, a rejected push sleeps `5 * attempt` seconds (`:147`) and retries;
     `throw` after the fifth failure (`:149`). `$PSNativeCommandUseErrorActionPreference = $false`
     at the top of both steps (`:107`, `:128`) so a non-zero `gh release view` / `git push` is
     seen by the explicit `$LASTEXITCODE` checks rather than turning into a terminating error
     under a future pwsh default. The per-tag concurrency group is unchanged (`:20-21`).

### Nits

1. `scripts/lint.ps1:21-25`: the `-CppcheckDir` / `-BinSkimDir` comments now say "PATH when empty
   (the CI passes the directory it extracted from the pinned cppcheck MSI / BinSkim NuGet
   package)".
2. `README.md:183-192`: "Every job that builds (`build-x64`, `build-x86`, `lint`, `coverage`)
   starts with the composite action ...; the release workflow's `verify` and `release` jobs build
   nothing and skip the action; the table gates `pack.ps1` runs there find the runner's LLVM
   through the VS 2022 layout fallback of `scripts/llvm-dir.ps1`", plus the permissions sentence.
   `AGENTS.md:76-79`: "the CI build, lint and coverage jobs set it to the runner's VS 2022 LLVM,
   the release job relies on the VS 2022 layout fallback" and the set-but-wrong rule.
3. `scripts/llvm-dir.ps1:22-27`: `$env:PVDKIT_LLVM_DIR` without `clang-cl.exe` throws
   `PVDKIT_LLVM_DIR=<dir> does not contain clang-cl.exe` instead of falling through.
   `cmake/find-llvm.cmake:15-25`: a cached/`-D` `PVDKIT_LLVM_DIR` or the environment variable
   without `clang-cl.exe` is `FATAL_ERROR` (`... does not contain clang-cl.exe`, the environment
   case labelled `(environment)`). Verified: with `PVDKIT_LLVM_DIR=C:\nonexistent\llvm\bin`,
   `check-imports.ps1` -> exit 1 `PVDKIT_LLVM_DIR=C:\nonexistent\llvm\bin does not contain
   clang-cl.exe`; `lint.ps1 -Tools clang-format` -> exit 1, same message; `cmake -P
   cmake\find-llvm.cmake` -> exit 1 `PVDKIT_LLVM_DIR=C:\nonexistent\llvm\bin (environment) does
   not contain clang-cl.exe`; `cmake -DPVDKIT_LLVM_DIR=C:/nonexistent -P cmake\find-llvm.cmake`
   -> exit 1 `PVDKIT_LLVM_DIR=C:/nonexistent does not contain clang-cl.exe`; with nothing set ->
   exit 0 (the Build Tools default); with the variable set to the Build Tools directory the
   scripts run as before.
4. `plugins/rpgmvp/DESIGN.md:39-47`: the paragraph now quotes the adapter-only (inflate-bound)
   pairs of the report table with the same rounding - x64 RGBA8 185.9-188.4 -> 154.5-158.6 ms
   (1.17-1.22x), RGBA16 532.7-535.4 -> 452.0-470.5 ms (1.13-1.18x), x86 RGBA8 195.7-195.8 ->
   230.3-232.2 ms (0.84-0.85x), RGBA16 615.8-617.7 -> 652.7-654.8 ms (0.94-0.95x) - and says
   which figures they are; the report table's speed-up column below is the same set. It also
   records that the measurement used generator version 1 of the 16-bit input (nit 5).
5. `plugins/rpgmvp/tests/adapters/DecodeTimingTests.cpp:46-53`: the comment says "0..7 levels
   added" and "0..63 in the low byte of the 16-bit colour samples; alpha is opaque at either
   depth"; `:86-95`: the sample loop is indexed so the alpha channel's low byte is `0xFF`
   (0xFFFF = opaque) while the colour channels keep their noise; `kSynthesisVersion = 2` (`:53`)
   is part of the cache file name (`:158-159`, `...-noise7-v2.rpgmvp`), so a cached version-1
   input is never reused for a version-2 run. The reported numbers were measured with version 1
   (alpha low byte 0x00..0x3F) and stand as measured; only 1 of every 8 bytes of the 16-bit
   input changes compressibility, the 8-bit input is byte-identical, and the decision (x64 max
   1.22x, x86 slower) does not depend on it. A re-measurement is a fresh
   `PVDKIT_RPGMVP_TIMING_CACHE` away. clang-format `--dry-run --Werror`: clean; `clang-tidy -p
   build\debug-t23` on the file: 0 diagnostics.
6. `scripts/release-tag.ps1:17` (`-cnotmatch`, message mentions the lower-case id) and `:44`
   (the ChangeLog first-line check, `-cnotmatch` as well). `AVIF/v1.1.0` -> `Tag 'AVIF/v1.1.0' is
   not <plugin id>/vMAJOR.MINOR.PATCH (lower-case id, as the directory under plugins/)`.
7. `release.yml:43`: `.\scripts\release-tag.ps1 -Tag $env:GITHUB_REF_NAME`; the README step
   builds the repository URL from `$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY` (`:135`) - no
   `${{ }}` interpolation into PowerShell anywhere in the job.
8. `scripts/update-readme-downloads.ps1:33,50-58`: the row regex captures the version cell and
   ends in `[^\r\n]*` (no `$`: under `(?m)` .NET's `$` sits before `\n` only, so a CRLF row would
   not have matched); `[version]` comparison - a row naming a newer version than the one released
   prints `unchanged` (reason on the verbose stream) and the file is not written; the appended
   row uses the file's own line ending (`:35`). Verified on copies of README.md: LF copy - `AVIF
   1.1.0` -> `unchanged`, `AVIF 1.2.0` -> `updated` (that row only), `AVIF 1.0.1` -> `unchanged`
   (`VERBOSE: the row already names 1.2.0, newer than 1.0.1; left alone`), `AVIF 1.10.0` ->
   `updated` (numeric, not lexical: 1.10.0 > 1.2.0), `HEIC 0.1.0` -> `updated` (row appended);
   0 CR, 311 LF, no BOM. CRLF copy (every line `\r\n`): `RPGMVP 1.2.0` -> `updated` with the row
   replaced in place (no duplicate), repeat -> `unchanged`, `HEIC 0.1.0` -> appended; afterwards
   CR = LF = 311, no BOM.
9. `scripts/pack.ps1:125-138`: a first loop runs `check-imports.ps1`, `check-exports.ps1` and the
   `FileVersion` check for every plugin and architecture; the staging/zip loop (`:140-171`)
   starts only after all of them passed, so a failing gate leaves no zip behind (header comment
   `:17-18`). Verified: in the run below the last "policy passed" line (600) precedes the first
   zip line (601).
10. `ci.yml:101-105`: `coverage` has `needs: build-x64` with the trade-off in a comment (the x64
    ports come from the cache build-x64 just saved instead of being built a second time in
    parallel on a cold cache; coverage starts later on every run).
11. `README.md:34-35`: an HTML comment above the anchors says the rows are seeded for the 1.1.0
    releases and each link resolves once its tag (`avif/v1.1.0`, `rpgmvp/v1.1.0`) has been pushed
    and published. The rows themselves are unchanged.
12. `cmake/find-llvm.cmake:1-13`: the comment prefers the environment variable and explains why
    (`try_compile` projects re-run the toolchain with a fresh cache); `:14` adds
    `list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PVDKIT_LLVM_DIR)`, CMake's mechanism for
    forwarding a toolchain variable into its own check projects, so a `-D` value now reaches
    them too (vcpkg's port builds still see only the environment, as documented).
13. `scripts/release-notes.ps1:15-17,34`: ` * ` and ` + ` bullets become `- `, so GitHub renders
    one list. Output for RPGMVP (`-OutFile`, UTF-8 without BOM, LF):

    ```
    RPGMVP 1.1.0 14.09.2026
    -----------------------
    - прозрачность теперь показывается: в 1.0.0 прозрачные места были
       чёрными
    - 16-битные PNG отдаются PictureView как 16-битные, а не ужимаются
       до 8

    SHA-256:

        RPGMVP-1.1.0-x64.zip  011758c0118dd3dfb9f468200265bd2d74765c7fe0fc72d7d7662c2c15b98912
        RPGMVP-1.1.0-x86.zip  ede3f1efe9d3175a4396c94c0564b42dd82ee3c873ddfb2d99c38dc94e3f4530
    ```

    (the AVIF entry likewise: five `- ` items, stops before `AVIF 1.0.0 12.09.2026`).

Also updated for consistency: the README release bullet (`README.md:211-221`: idempotent
creation, the retry loop, the newer-version rule) and the table in section 1.3 above, whose
speed-up column now reads 1.17-1.22x / 1.13-1.18x / 0.84-0.85x / 0.94-0.95x like DESIGN.md.

### Re-runs

- `actionlint 1.7.12 -verbose` over the three workflows: `Found 0 parse errors` for each,
  `Found 0 errors in 3 files`, exit 0; all four YAML files (composite action included) parse:
  `build.yml -> name, on, permissions, jobs`, `ci.yml -> name, on, permissions, concurrency,
  jobs`, `release.yml -> name, on, concurrency, permissions, jobs`.
- `scripts/lint.ps1 -Tools psscriptanalyzer`: `PSScriptAnalyzer: 0 finding(s) in 4.7 s`,
  `lint: clean`.
- `scripts/pack.ps1 -Suffix -t23 -DistDir <scratchpad>\dist-t23` (zips deleted first): exit 0;
  eight gate lines (`Import policy passed` / `Export policy passed` for AVIF x64, RPGMVP x64,
  AVIF x86, RPGMVP x86) all before the first zip line; zips and SHA-256 identical to section
  2.3 (`21219ceb...`, `011758c0...`, `9047de00...`, `ede3f1ef...`); `7z l`: `AVIF-1.1.0-x64.zip`
  2604805 bytes / 5 files, `AVIF-1.1.0-x86.zip` 1700613 / 5 files, `RPGMVP-1.1.0-x64.zip`
  327018 / 5 files, `RPGMVP-1.1.0-x86.zip` 281962 / 5 files.
- `release-tag.ps1`: `avif/v1.1.0` -> `avif AVIF 1.1.0 avif/v1.1.0`, `rpgmvp/v1.1.0` OK,
  `AVIF/v1.1.0` rejected (nit 6), `avif/v1.2.0` -> `asks for 1.2.0, but ... declares VERSION
  1.1.0`. `release-notes.ps1`: above. `update-readme-downloads.ps1`: nit 8 above.
- `cmake --preset debug` (5 s), `cmake --build --preset debug --parallel 6`: 3 steps
  (`DecodeTimingTests.cpp.obj`, link `rpgmvp_adapter_tests.exe`), 0 warnings; `ctest --preset
  debug`: `100% tests passed out of 17`, 68.57 s; `rpgmvp_adapter_tests` on that build: `18
  passed | 0 failed | 2 skipped`.
- `scripts/coverage.ps1 -Preset coverage`: `TOTAL 1087 0 100.00% 258 0 100.00% 2139 0 100.00%
  694 0 100.00%`, plugin profile checks passed for `avif` and `rpgmvp`, `Coverage source
  completeness passed: 24 executable source files present.`, `Coverage gate passed: lines 100%,
  branches 100%.` (89 s).
- Not re-run, as instructed: x86 presets, ASan, the full lint (only the skipped timing test
  changed among C++ files; clang-format and clang-tidy were run on it directly, above).
