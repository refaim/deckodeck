# Task 13 report — plugin version 1.0.1

Baseline: clean worktree at `0965fc09bdfdbc24fa95bb006c3439b2395b2f7d`. The work remains uncommitted.

## What was done

- Bumped the single-source identities for AVIF and RPGMVP from `1.0.0` to `1.0.1` in their
  `pvdkit_plugin_identity` declarations.
- Updated all five plugin identity assertions: AVIF adapter, two AVIF e2e paths, RPGMVP adapter,
  and RPGMVP e2e.
- Changed the architecture example to the neutral `VERSION <version>` placeholder so it does not
  become a stale release-specific example.
- Added a `Changes` section to both plugin READMEs. Each records that 1.0.1 flags alpha to the host,
  that 1.0.0 displayed transparent images opaque, and that 1.0.0 was the initial release.
- Documented the optional RPGMVP `PVDKIT_RPGMVP_DEEP_OUTPUT=ON` build in one sentence.
- The required pre-edit `git grep` also found five unrelated `1.0.0` literals in shared test
  fixtures. Those literals were advanced to `1.0.1`; they are data-only changes. The final search
  outside `docs/tasks/` finds `1.0.0` only in the four intentional README history lines.
- Both package `README.txt.in` files were left unchanged: they contain only
  `@PVDKIT_PLUGIN_VERSION@`, not a version history, and therefore derive 1.0.1 automatically.

## Red-before-green evidence

The five product-version assertions were changed before the identities. Against the still-1.0.0
generated identities, the targeted x64 run failed exactly as intended:

```text
ctest: 0/4 passed, 4 failed
avif_adapter_tests:   35/36 cases, 3747/3748 assertions
avif_e2e_tests:       13/15 cases, 1702/1704 assertions (both 1.0.1 checks failed)
rpgmvp_adapter_tests: 16/17 cases, 581/582 assertions
rpgmvp_e2e_tests:      8/9 cases, 664/665 assertions
Observed value in every failed version check: 1.0.0; expected: 1.0.1.
```

After the identity bump, both complete Release test presets passed.

## Release verification

| Architecture | Configure | Build | Full ctest | Compiler/linker warnings |
|---|---:|---:|---:|---:|
| x64 | passed | passed | 19/19 passed in 23.99 s | 0 |
| x86 | passed | passed | 19/19 passed in 25.67 s | 0 |

The first configure of each fresh suffixed tree restored most ports from cache and built the
libspng overlay. vcpkg could not write the resulting libspng archive to its optional binary cache
and printed a non-fatal `Access is denied` cache-submission warning; dependency installation and
CMake configuration completed successfully. This was not a compiler or linker diagnostic.

The shared targeted VERSIONINFO test was then run directly for each binary. All four invocations
passed 1/1 test case and 49/49 assertions. For every binary it read:

- fixed numeric file/product version `1.0.1.0`;
- `FileVersion` string `1.0.1`;
- `ProductVersion` string `1.0.1`;
- runtime `pvdPluginInfo` version equal to the resource `FileVersion`, therefore `1.0.1`.

The Release policy gates were also rerun verbosely: 4/4 passed on x64 and 4/4 passed on x86.
Every DLL imports only `KERNEL32.dll`. Every DLL exports exactly these eight bare names:
`pvdExit`, `pvdFileClose`, `pvdFileOpen`, `pvdInit`, `pvdPageDecode`, `pvdPageFree`, `pvdPageInfo`,
and `pvdPluginInfo`.

## Release DLLs

| Architecture | Path | Version read back by tests | SHA-256 |
|---|---|---:|---|
| x64 | `build/release-t13/plugins/avif/AVIF.pvd` | 1.0.1 | `16C4700D134D2E1EE1E3B07AD1719F3B384C2F59325012CA3F7C9E44482529D6` |
| x64 | `build/release-t13/plugins/rpgmvp/RPGMVP.pvd` | 1.0.1 | `357F63544B224C876BCC3CFFA0F8770CCD96A48F41D0611A7636543FEE29E694` |
| x86 | `build/release-x86-t13/plugins/avif/AVIF.pvd` | 1.0.1 | `6674FA1DC6B126D0592D79E085E7ED672D1BE9F89C3F93C5675EF6982ABD7705` |
| x86 | `build/release-x86-t13/plugins/rpgmvp/RPGMVP.pvd` | 1.0.1 | `88C0393E9ACB7B22E71EC5D7CA91434337A55AF88AA0CDD27512AAEF827CFBC1` |

## Commands run and results

Preflight and version search:

```powershell
rtk git status --short
# no output before edits
rtk git rev-parse HEAD
# 0965fc09bdfdbc24fa95bb006c3439b2395b2f7d
rtk git grep -n "1\.0\.0" -- . ":(exclude)docs/tasks/**"
# 13 pre-edit hits: eight product/example assertions and five shared test-fixture literals
```

Red test setup and run:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t13'; rtk cmake --preset release
# passed
$env:PVDKIT_BUILD_SUFFIX = '-t13'; rtk cmake --build --preset release --parallel 6 --target avif_adapter_tests avif_e2e_tests rpgmvp_adapter_tests rpgmvp_e2e_tests
# passed; zero build warnings
$env:PVDKIT_BUILD_SUFFIX = '-t13'; rtk ctest --preset release --output-on-failure -R "^(avif_adapter_tests|avif_e2e_tests|rpgmvp_adapter_tests|rpgmvp_e2e_tests)$"
# expected red result: 0/4 passed, four version mismatches
```

Final x64 Release pipeline:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t13'; rtk cmake --preset release
# passed
$env:PVDKIT_BUILD_SUFFIX = '-t13'; rtk cmake --build --preset release --parallel 6
# passed; zero build warnings
$env:PVDKIT_BUILD_SUFFIX = '-t13'; rtk ctest --preset release --output-on-failure
# 19/19 passed in 23.99 s
```

Final x86 Release pipeline:

```powershell
$env:PVDKIT_BUILD_SUFFIX = '-t13'; rtk cmake --preset release-x86
# passed
$env:PVDKIT_BUILD_SUFFIX = '-t13'; rtk cmake --build --preset release-x86 --parallel 6
# passed; zero build warnings
$env:PVDKIT_BUILD_SUFFIX = '-t13'; rtk ctest --preset release-x86 --output-on-failure
# 19/19 passed in 25.67 s
```

Explicit VERSIONINFO and policy read-backs:

```powershell
rtk proxy .\build\release-t13\plugins\avif\tests\e2e\avif_e2e_tests.exe --test-case=*VERSIONINFO* --success=true
rtk proxy .\build\release-t13\plugins\rpgmvp\tests\e2e\rpgmvp_e2e_tests.exe --test-case=*VERSIONINFO* --success=true
rtk proxy .\build\release-x86-t13\plugins\avif\tests\e2e\avif_e2e_tests.exe --test-case=*VERSIONINFO* --success=true
rtk proxy .\build\release-x86-t13\plugins\rpgmvp\tests\e2e\rpgmvp_e2e_tests.exe --test-case=*VERSIONINFO* --success=true
# each: 1/1 test case and 49/49 assertions passed; resource/runtime version 1.0.1

$env:PVDKIT_BUILD_SUFFIX = '-t13'; rtk proxy ctest --preset release -V -R "_check_(imports|exports)$"
# 4/4 passed in 1.16 s
$env:PVDKIT_BUILD_SUFFIX = '-t13'; rtk proxy ctest --preset release-x86 -V -R "_check_(imports|exports)$"
# 4/4 passed in 1.15 s

rtk proxy powershell -NoProfile -Command 'Get-FileHash -Algorithm SHA256 build\release-t13\plugins\avif\AVIF.pvd,build\release-t13\plugins\rpgmvp\RPGMVP.pvd,build\release-x86-t13\plugins\avif\AVIF.pvd,build\release-x86-t13\plugins\rpgmvp\RPGMVP.pvd | Format-List Path,Hash'
# produced the four hashes above
```

Final textual checks:

```powershell
rtk git diff --check
# passed, no output
rtk git grep -n "1\.0\.0" -- . ":(exclude)docs/tasks/**"
# four intentional hits, all in plugins/*/README.md release histories
```

## Not run or changed

- Lint was not run.
- Coverage was not run.
- This follows the task's explicit exemption because every changed test line is a version literal;
  no test logic or production code changed.
- No package template or Far Manager/PictureView installation was touched, and no commit was created.
