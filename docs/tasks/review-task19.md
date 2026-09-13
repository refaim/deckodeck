## Substantive findings

None.

## Nits

None.

## Verified

- Read `AGENTS.md`, `docs/ARCHITECTURE.md`, the changed/new files, the PVD SDK and reference decoders, the host ABI notes, and `docs/tasks/report-task19.md`. The architecture interfaces and the implementation agree: `ImageMeta::exifOrientation` and `DecodedPage::hostOrientation` are the only new cross-layer fields, while `IDecoder::iccProfile()` remains.

- Configured and built the isolated x64 debug tree with the required job limit:

  ```powershell
  $env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --preset debug
  $env:PVDKIT_BUILD_SUFFIX='-review'; rtk cmake --build --preset debug --parallel 6
  ```

  Both commands exited 0; configure wrote `build/debug-review`, and the build completed all 79 steps with no warnings. `build/debug-review/compile_commands.json` shows clang-cl with `-MTd /clang:-std=c++23 /W4 /WX`.

- Ran the requested debug suite and a focused package-document rerun:

  ```powershell
  $env:PVDKIT_BUILD_SUFFIX='-review'; rtk ctest --preset debug
  $env:PVDKIT_BUILD_SUFFIX='-review'; rtk ctest --test-dir build/debug-review -R package_docs --output-on-failure
  ```

  Key output:

  ```text
  ctest: 17/17 passed (85.51 sec)
  ctest: 2/2 passed (0.38 sec)
  avif_package_docs 0.20 sec
  rpgmvp_package_docs 0.12 sec
  ```

  The package checks validate the required CRLF/BOM conventions and byte-for-byte copies of the three package documents.

- Ran coverage serially after the debug build, with six build jobs:

  ```powershell
  $env:PVDKIT_BUILD_SUFFIX='-review'; $env:CMAKE_BUILD_PARALLEL_LEVEL='6'; rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage
  ```

  It exited 0 after `100% tests passed out of 17`. Relevant production rows were:

  | File | Lines | Branches |
  |---|---:|---:|
  | `plugins\avif\src\adapters\avif\Decoder.cpp` | 296/296 (100%) | 70/70 (100%) |
  | `plugins\avif\src\adapters\avif\Decoder.hpp` | 1/1 (100%) | none |
  | `plugins\avif\src\core\Describe.cpp` | 120/120 (100%) | 60/60 (100%) |
  | `src\core\FileSession.cpp` | 103/103 (100%) | 48/48 (100%) |
  | `src\core\IDecoder.hpp` | 2/2 (100%) | none |
  | `src\pvd\Shim.cpp` | 260/260 (100%) | 46/46 (100%) |
  | `src\pvd\Types.hpp` | 1/1 (100%) | none |

  Overall output was `2080` lines with `0` missed and `666` branches with `0` missed, followed by `Coverage source completeness passed: 24 executable source files present.` and `Coverage gate passed: lines 100%, branches 100%.` The coverage run also showed `guard_tests` passing.

- Inspected cached libavif 1.4.2 `src/exif.c`. `avifGetExifOrientationOffset()` accepts tag `0x0112` only as TIFF `SHORT` (`0x03`) with count 1 and value 1..8. Truncated reads return `AVIF_RESULT_INVALID_EXIF_PAYLOAD`; a `LONG` tag is ignored and produces the no-tag sentinel `offset == exifSize`. For a valid tag it selects the least-significant value byte with `littleEndian ? 4 : 3`; the adapter additionally requires `offset < image.exif.size` before reading. Thus both byte orders and malformed/truncated payloads are safe.

- Confirmed the MIAF rule in libavif's cached `apps/shared/avifjpeg.c` (ISO/IEC 23000-22:2024 section 7.3.10.1): "There should be no image transformations expressed by Exif (rotation, mirroring, etc.) indicated in the Exif metadata." The adapter gives `irot`/`imir` precedence, and `FileSession` emits a host orientation only when `Transform::hasTransforms()` is false, so the shared kernel and PictureView cannot both rotate the page.

- Compared the mapping `{1->0, 2->2, 3->3, 4->1, 5->6, 6->7, 7->5, 8->4}` and shift 4 against `docs/host/pictureview-abi.md`. `Shim` composes `(hasAlpha ? 2 : 0) | (hostOrientation << 4)`. Unit tests cover the full table, every kernel-transform gate, and alpha plus orientation; DLL e2e tests cover host codes 7, 3, and 0 while retaining unrotated EXIF page dimensions. The `irot`+EXIF fixture also retains alpha without an orientation nibble.

- Verified fixture metadata and hashes. ExifTool reported orientations `6`, `3`, and `6`, all with `ExifByteOrder: MM`; the SHA-256 values exactly match `plugins/avif/fixtures/SOURCES.md`:

  ```text
  577D7121F5A05AE0414195F704F50A9D54E86B3EDBBCCC866803E1F3DE40CBED  abc_color_irot_alpha_irot_plus_exif6.avif
  B338DBC677E552B70C138F79EB799C3A1256BE39FAECE9B99A103F91C06668AD  kodim03_exif_orientation_3.avif
  473182907469BA4DA616F228F4F68EEB29CC150D12D3E98A90207ABBF712FC79  kodim03_exif_orientation_6.avif
  ```

- Removal and text-integrity checks:

  ```powershell
  rtk git grep -n -E "EXPERIMENT_ICC|pvdInfoDecodeEx|IDF_ICC" -- ":!docs/tasks/**" ":!docs/host/**"
  rtk git diff --check -- . ":(exclude)plugins/avif/package/ChangeLog" ":(exclude)plugins/avif/package/readme_en.txt" ":(exclude)plugins/avif/package/readme_ru.txt"
  ```

  The residue grep exited 1 with no output (no matches); `git diff --check` exited 0 with no output. Strict UTF-8 decoding of `docs/tasks/report-task19.md` succeeded for all 14,241 bytes, with no U+FFFD replacement characters or mojibake patterns found. Its base commit `81cf502658082096c224b291a4bde9ac8d28098f` matches `HEAD`, and its x64 debug/coverage totals agree with this review run.

- Ran `scripts/check-imports.ps1` and `scripts/check-exports.ps1` on both isolated debug `.pvd` files. Both import checks reported `KERNEL32.dll is the only imported module`; both export checks reported exactly the eight bare PVD entry points.

- Per the review instruction, x86, ASan, and lint were not rerun. No concrete reason arose to expand into those builds; their recorded commands/results in the implementation report were checked for consistency with the current tree.

## Verdict

ACCEPT
