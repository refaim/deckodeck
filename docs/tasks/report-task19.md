# Task 19 implementation report: EXIF orientation and ICC experiment removal

Date: 2026-09-14
Branch: `master`
Base commit: `81cf502658082096c224b291a4bde9ac8d28098f`
Build suffix: `-t19`
AVIF version: `1.2.0` (unchanged)

## Outcome

Task 19 is complete. The unused PictureView ICC extension experiment has been removed while both
codec adapters retain their tested `IDecoder::iccProfile()` implementations for a future colour
management pipeline. AVIF files now pass valid EXIF orientation 1..8 to PictureView through its
recovered decode-flags orientation code, provided the file has no `irot`, `imir`, or other transform
that pvdkit applies itself. Page metadata remains in decoded, unrotated dimensions because
PictureView performs the EXIF rotation.

The final x64 and x86 Debug/Release suites, x64 and x86 100% coverage gates, ASan suite, guard test,
package checks, all five lint analyzers, and direct Release DLL import/export inspection pass. No
compiler warnings were emitted.

## Implementation

### ICC experiment removal

- Removed the `PVDKIT_EXPERIMENT_ICC` CMake option and all related compile definitions.
- Removed the private `pvdInfoDecodeEx` layout, `PVD_IDF_ICC_PROFILE`, ICC-size/write helpers, and
  extension write from `Shim::pageDecode()`.
- Removed the e2e extension storage, experiment expectation macro, experiment-only tests, and the
  now-unused `DecodedPage::iccProfile` field.
- Kept `IDecoder::iccProfile()` and its AVIF and RPGMVP adapter tests and fixtures. This preserves
  the codec-side metadata boundary for a future CMS without exposing dead host switches.
- Kept the default-build canary as `Shim writes only the documented decode fields`.
- Updated `docs/ARCHITECTURE.md` section 3 and both plugin design documents. The recovered host facts
  remain in `docs/host/pictureview-abi.md`.

The required residue check was run exactly as follows:

```powershell
rtk git grep -n -E "EXPERIMENT_ICC|pvdInfoDecodeEx|IDF_ICC" -- ':!docs/tasks/**' ':!docs/host/**'
```

It exited 1 with no output, which is the expected `git grep` result for no matches.

### EXIF orientation

- `core::ImageMeta` now has `std::uint8_t exifOrientation = 0`; zero means absent or ignored.
- The AVIF adapter calls `avifGetExifOrientationOffset()`, reads the returned byte only when the
  offset is in range, and accepts only values 1..8. Parser errors, missing tags, and reserved values
  produce zero.
- AVIF `irot`/`imir` properties take precedence per MIAF/libavif. When either is present, the adapter
  deliberately records EXIF orientation as zero, so it cannot be applied twice.
- `pvd::DecodedPage` now carries `hostOrientation`. `FileSession::decodePage()` maps EXIF values to
  PictureView codes with the recovered table below, but only when `Transform::hasTransforms()` is
  false.

| EXIF value | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| PictureView code | 0 | 2 | 3 | 1 | 6 | 7 | 5 | 4 |

- `Shim::pageDecode()` ORs `hostOrientation << PVD_IDF_ORIENTATION_SHIFT` into `Flags`; the shift is
  4 and its declaration points to the recovered host-ABI document. The alpha flag remains
  independent.
- `pvdPageInfo` continues to expose unrotated decoded dimensions.
- The AVIF description appends `EXIF orientation N` only when that orientation will be applied, not
  when a pvdkit transform takes precedence.
- AVIF package documentation says EXIF orientation is honoured when no `irot`/`imir` is present.
  The existing `AVIF 1.2.0 14.09.2026` ChangeLog entry contains Roma's requested Russian line.
  The version was not bumped. RPGMVP has no EXIF behaviour change.

## Fixtures

The three fixtures were derived with ExifTool from existing repository AVIF files. The exact
generation commands are recorded in `plugins/avif/fixtures/SOURCES.md`:

```powershell
Copy-Item plugins\avif\fixtures\kodim03_yuv420_8bpc.avif plugins\avif\fixtures\kodim03_exif_orientation_6.avif
C:\Users\Roma\scoop\shims\exiftool.exe -overwrite_original -Orientation=6 -n plugins\avif\fixtures\kodim03_exif_orientation_6.avif
Copy-Item plugins\avif\fixtures\kodim03_yuv420_8bpc.avif plugins\avif\fixtures\kodim03_exif_orientation_3.avif
C:\Users\Roma\scoop\shims\exiftool.exe -overwrite_original -Orientation=3 -n plugins\avif\fixtures\kodim03_exif_orientation_3.avif
Copy-Item plugins\avif\fixtures\abc_color_irot_alpha_irot.avif plugins\avif\fixtures\abc_color_irot_alpha_irot_plus_exif6.avif
C:\Users\Roma\scoop\shims\exiftool.exe -overwrite_original -Orientation=6 -n plugins\avif\fixtures\abc_color_irot_alpha_irot_plus_exif6.avif
```

| Fixture | Bytes | EXIF orientation | Expected host code | Decoded dimensions | SHA-256 |
|---|---:|---:|---:|---:|---|
| `kodim03_exif_orientation_6.avif` | 25,539 | 6 | 7 | 768 x 512 | `473182907469BA4DA616F228F4F68EEB29CC150D12D3E98A90207ABBF712FC79` |
| `kodim03_exif_orientation_3.avif` | 25,539 | 3 | 3 | 768 x 512 | `B338DBC677E552B70C138F79EB799C3A1256BE39FAECE9B99A103F91C06668AD` |
| `abc_color_irot_alpha_irot_plus_exif6.avif` | 10,694 | 6 | 0 | 256 x 512 | `577D7121F5A05AE0414195F704F50A9D54E86B3EDBBCCC866803E1F3DE40CBED` |

Final metadata verification:

```powershell
rtk proxy C:\Users\Roma\scoop\shims\exiftool.exe -Orientation -n plugins\avif\fixtures\kodim03_exif_orientation_6.avif plugins\avif\fixtures\kodim03_exif_orientation_3.avif plugins\avif\fixtures\abc_color_irot_alpha_irot_plus_exif6.avif
```

ExifTool reported orientations 6, 3, and 6 respectively and `3 image files read`; it exited 0. Its
Perl runtime also printed a harmless locale fallback warning.

## Tests and TDD evidence

Tests were added before production code for the new metadata fields, adapter parsing/precedence,
all eight mapping values and transform gating, shim flag packing, descriptions, and DLL behaviour.

- The first focused build was red because `ImageMeta::exifOrientation` and
  `DecodedPage::hostOrientation` did not yet exist.
- After the initial implementation, the first coverage run was red at 99.90% lines and 99.70%
  branches. The untested reserved-value normalization was extracted behind a testable helper; its
  test was red first because the helper did not exist, then passed with values 0, 1..8, 9, and 255.
- The first description-precedence test was red with two failed assertions because descriptions
  still mentioned EXIF orientation beside pvdkit transforms. Gating the comment through
  `Transform::hasTransforms()` made it green.
- An early focused target command also named a nonexistent `rpgmvp_composition_tests` target and
  failed before compilation; it was corrected to the repository's real targets.
- The first lint pass reported ten clang-format findings. Formatting was applied, and both final
  full lint passes are clean.

Coverage includes the AVIF adapter paths for parsed values 6 and 3, `irot`/`imir` precedence,
missing and malformed EXIF, no-orientation offset, valid-value normalization, and invalid values.
Core tests cover the complete EXIF-to-host table plus clap/irot/imir gating. Shim tests cover the
orientation nibble together with alpha and retain the documented-structure write canary. DLL e2e
tests assert codes 7, 3, and 0 and the unrotated dimensions listed above.

## Verification

All final builds used clang-cl 19.1.5, lld-link, C++23, `/W4 /WX`, the static CRT, and six build
jobs. The configure/build/test sequence was run one preset at a time with
`PVDKIT_BUILD_SUFFIX=-t19`:

```powershell
rtk cmake --preset debug
rtk cmake --build --preset debug --parallel 6
rtk ctest --preset debug --parallel 6 --output-on-failure

rtk cmake --preset release
rtk cmake --build --preset release --parallel 6
rtk ctest --preset release --parallel 6 --output-on-failure

rtk cmake --preset debug-x86
rtk cmake --build --preset debug-x86 --parallel 6
rtk ctest --preset debug-x86 --parallel 6 --output-on-failure

rtk cmake --preset release-x86
rtk cmake --build --preset release-x86 --parallel 6
rtk ctest --preset release-x86 --parallel 6 --output-on-failure
```

| Preset | Result | Total time | Compiler warnings |
|---|---:|---:|---:|
| `debug` x64 | 17/17 passed | 50.74 s | 0 |
| `release` x64 | 21/21 passed | 24.09 s | 0 |
| `debug-x86` | 17/17 passed | 73.64 s | 0 |
| `release-x86` | 21/21 passed | 35.90 s | 0 |

The Release totals include the import and export policy tests for both plugins. Package-document
tests and the source guard passed in every applicable suite.

Coverage was run one architecture at a time with the suffix above and
`CMAKE_BUILD_PARALLEL_LEVEL=6`:

```powershell
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86
```

| Preset | Tests | Regions | Functions | Lines | Branches |
|---|---:|---:|---:|---:|---:|
| `coverage` x64 | 17/17 passed in 85.00 s | 1,048/1,048 (100%) | 254/254 (100%) | 2,080/2,080 (100%) | 666/666 (100%) |
| `coverage-x86` | 17/17 passed in 136.41 s | 1,048/1,048 (100%) | 254/254 (100%) | 2,080/2,080 (100%) | 666/666 (100%) |

Both runs found all 24 executable production sources. Both loaded plugins produced runtime profiles
and reported 18/18 `Exports.cpp` functions executed.

The final sanitizer commands were:

```powershell
rtk powershell -NoProfile -Command "`$env:PVDKIT_BUILD_SUFFIX='-t19'; cmake --build --preset asan --parallel 6"
rtk powershell -NoProfile -Command "`$env:PVDKIT_BUILD_SUFFIX='-t19'; ctest --preset asan --parallel 6 --output-on-failure"
```

The ASan build completed with zero warnings; 17/17 tests passed in 70.13 s.

The final lint commands were:

```powershell
rtk cmd /d /c "set PVDKIT_BUILD_SUFFIX=-t19&& powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6"
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -BuildDir build\debug-x86-t19 -ReleaseDir build\release-x86-t19 -Jobs 6
```

| Architecture | clang-format | clang-tidy | cppcheck | PSScriptAnalyzer | BinSkim |
|---|---:|---:|---:|---:|---:|
| x64 | 0 in 0.6 s | 0 in 333.4 s | 0 in 1.6 s | 0 in 4.5 s | 0 in 1.2 s |
| x86 | 0 in 0.7 s | 0 in 363.4 s | 0 in 2.0 s | 0 in 5.1 s | 0 in 1.3 s |

Both ended with `lint: clean`.

```powershell
rtk git diff --check -- . ':!plugins/avif/package/ChangeLog' ':!plugins/avif/package/readme_en.txt' ':!plugins/avif/package/readme_ru.txt'
```

The command exited 0 with no output.
Those three byte-exact package files are excluded because their required CRLF lines are intentionally
reported as whitespace by Git's check; their encoding and first-version-line checks passed through
`avif_package_docs` in all final suites.

One attempted ASan refresh used nested PowerShell quoting that expanded the environment variable in
the parent shell. It printed an `=-t19` error and refreshed the pre-existing unsuffixed `build/asan`
directory; that run is not used as verification evidence. The correct `build/asan-t19` commands and
results are the ones reported above. A vcpkg binary-cache submission printed `Access is denied`, but
the cached dependency restore/install and all configure/build operations completed successfully; no
network was used.

## Release DLLs and ABI checks

The final hashes were collected with:

```powershell
rtk powershell -NoProfile -Command "Get-FileHash -Algorithm SHA256 -LiteralPath @('build/release-t19/plugins/avif/AVIF.pvd','build/release-t19/plugins/rpgmvp/RPGMVP.pvd','build/release-x86-t19/plugins/avif/AVIF.pvd','build/release-x86-t19/plugins/rpgmvp/RPGMVP.pvd') | Format-List Path,Hash"
```

| Architecture | DLL | SHA-256 |
|---|---|---|
| x64 | `build/release-t19/plugins/avif/AVIF.pvd` | `73C0CD2E972111CC2EB2214417D4AA3823AD094CF59D6C393BF4030291258472` |
| x64 | `build/release-t19/plugins/rpgmvp/RPGMVP.pvd` | `2C14E8210F30A9D2D4A492A0B6A5ACF3D7CBE7AC80F3E8A7C9DA56EF1526196E` |
| x86 | `build/release-x86-t19/plugins/avif/AVIF.pvd` | `D0914760919D25559D60B47B0FD855180622DB484E319117AD136475BE8DA57E` |
| x86 | `build/release-x86-t19/plugins/rpgmvp/RPGMVP.pvd` | `7A6CEE41CBAB741067BA8064B80022AD53DE89C9FE530A694E2C313D5B794D07` |

Direct inspection used these commands:

```powershell
rtk proxy "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-readobj.exe" --coff-imports build\release-t19\plugins\avif\AVIF.pvd build\release-t19\plugins\rpgmvp\RPGMVP.pvd build\release-x86-t19\plugins\avif\AVIF.pvd build\release-x86-t19\plugins\rpgmvp\RPGMVP.pvd
rtk proxy "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-readobj.exe" --coff-exports build\release-t19\plugins\avif\AVIF.pvd build\release-t19\plugins\rpgmvp\RPGMVP.pvd build\release-x86-t19\plugins\avif\AVIF.pvd build\release-x86-t19\plugins\rpgmvp\RPGMVP.pvd
```

Both exited 0. Each DLL imports exactly one module, `KERNEL32.dll`, and exports exactly these eight
bare names:

1. `pvdExit`
2. `pvdFileClose`
3. `pvdFileOpen`
4. `pvdInit`
5. `pvdPageDecode`
6. `pvdPageFree`
7. `pvdPageInfo`
8. `pvdPluginInfo`

## What Roma should open in Far

Use the x64 `build\release-t19\plugins\avif\AVIF.pvd` with x64 Far/PictureView and open:

1. `plugins\avif\fixtures\kodim03_exif_orientation_6.avif` — it must appear correctly rotated by
   PictureView for EXIF orientation 6 (host code 7).
2. `plugins\avif\fixtures\kodim03_exif_orientation_3.avif` — it must appear rotated 180 degrees
   for EXIF orientation 3 (host code 3).
3. `plugins\avif\fixtures\abc_color_irot_alpha_irot_plus_exif6.avif` — it must look exactly like
   the existing `abc_color_irot_alpha_irot.avif`; its `irot` remains authoritative and PictureView
   must not apply an additional EXIF rotation (host code 0).

## Not done

- Far/PictureView was not launched and `C:\Tools\FarManager` was not touched; the three visual
  observations above remain for Roma.
- No ICC bytes are forwarded to PictureView and no CMS was added. Only the tested adapter accessors
  and their fixtures/provenance remain for future work.
- AVIF was not bumped beyond 1.2.0 because 1.2.0 is unpublished and this change is folded into it.
- RPGMVP decoding behaviour and package files were not changed.
- No files were committed, staged, pushed, reset, checked out, or cleaned.
