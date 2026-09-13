## Substantive findings

1. **`readme_en.txt` line endings are not validated.** In `cmake/pvdkit-package-docs.cmake:44`, the CRLF/final-line validation loop is limited to `readme_ru.txt` and `ChangeLog`; the English-file loop checks byte values only and explicitly permits both CR and LF independently. I removed the CR from the first CRLF in `plugins/avif/package/readme_en.txt` and ran the release configure. It exited 0:

   ```text
   === lone LF in readme_en exit=0 ===
   -- pvdkit plugins: avif;rpgmvp
   -- Configuring done (4.8s)
   -- Generating done (0.2s)
   ```

   The malformed file was consequently copied into the package stage, and `<id>_package_docs` would apply the same incomplete check. This violates the required ASCII-plus-CRLF contract. Apply the CRLF/final-terminator validation to all three documents (with the BOM scan offset kept only for the two BOM-bearing files).

2. **The obsolete name `README.txt` still has a live-tree reference.** The required search found `cmake/pvdkit-plugin.cmake:142`:

   ```cmake
   file(REMOVE "${package_dir}/README.txt")
   ```

   This does remove the stale staged file and `package.ps1` cannot package it, but it fails the explicit requirement that neither `README.txt.in` nor `README.txt` remain outside `docs/tasks/`. Clearing/recreating the generated package directory before writing the five current files would preserve the stale-file guarantee without retaining the obsolete name.

## Nits

None.

## Verified

- Reviewed `git status --short`, the complete tracked diff, and every untracked Task 17 file. No C++ production or test source changed.
- All six source documents passed a strict byte probe. The two English files are pure ASCII without a BOM; the other four are valid UTF-8 with `EF BB BF`; every file contains only CRLF line endings, ends in CRLF, and matches its scratchpad original after removing only BOM/EOL representation differences. Because the normalized texts are equal and each final CRLF is accounted for, no trailing text or bytes were added.

  | File | Bytes | CRLFs | SHA-256 | Scratchpad-normalized equal |
  |---|---:|---:|---|---|
  | `avif/readme_en.txt` | 1,349 | 32 | `B207999C8D2221D7F3ADC6CF16DAAEDB9C507AD1B1F714DAB9DCD6B4880B4091` | yes |
  | `avif/readme_ru.txt` | 2,137 | 32 | `B1F0508215594F885A4415C970C3928373E390CBE4FC538E13CEFF04D314B1D6` | yes |
  | `avif/ChangeLog` | 796 | 15 | `F3FCE43BF743F27080C6BA0C2D6C677C78DA054EEC8E3CA79627F6095BB1F812` | yes |
  | `rpgmvp/readme_en.txt` | 1,105 | 29 | `188FB774A537E8E4BB8AEA1AB41B7DB09853614451055359017FE7971EE58F25` | yes |
  | `rpgmvp/readme_ru.txt` | 1,717 | 29 | `A9B9AF231DE0184021BE0620F7D75872FBEA79EF65584950D095F68EC3C21E27` | yes |
  | `rpgmvp/ChangeLog` | 624 | 13 | `6F4281B777E6DFEFB5CC39ED81406BEFC05E357DF4E380C9CB665F4F0AAEA1A6` | yes |

- `.gitattributes` protects all six files: `git check-attr text` reported `text: unset` for each. On the CRLF English AVIF readme, dry-run add printed only `add 'plugins/avif/package/readme_en.txt'`, `git diff` produced no output, and filtered/unfiltered `git hash-object` both returned `b499c532155b5150d02c81539635f0b2169da4b0`; Git therefore preserves the working-tree bytes.
- Five independent configure-time negative probes failed correctly, and every edited source was restored to its original SHA-256 in a `finally` block:

  ```text
  missing file (exit 1):
    pvdkit_add_plugin(rpgmvp): required package document
    '.../plugins/rpgmvp/package/readme_ru.txt' does not exist

  wrong version (exit 1):
    pvdkit_add_plugin(avif): ChangeLog first line must be exactly 'AVIF 1.1.0
    DD.MM.YYYY'; got 'AVIF 9.9.9 13.09.2026'

  wrong date shape (exit 1):
    pvdkit_add_plugin(rpgmvp): ChangeLog first line must be exactly 'RPGMVP
    1.1.0 DD.MM.YYYY'; got 'RPGMVP 1.1.0 13.09.202X'

  non-ASCII English byte (exit 1):
    pvdkit_add_plugin(avif): readme_en.txt contains disallowed byte 0x80 at
    byte offset 0; only TAB, CR, LF and ASCII bytes 0x20-0x7E are allowed

  missing BOM (exit 1):
    pvdkit_add_plugin(rpgmvp): readme_ru.txt must start with the UTF-8 BOM (EF
    BB BF)
  ```

  The sixth probe, the lone LF in the English readme, is finding 1. A final valid configure restored the staged copy as well.
- Package staging is constrained correctly apart from the obsolete textual reference noted above. `scripts/package.ps1:84` names exactly `readme_en.txt`, `readme_ru.txt`, `ChangeLog`, `LICENSES.txt`, and `manifest.json`; its temporary staging directory is removed/recreated and receives only the plugin plus those five files. Before configure I placed a stale `README.txt` in the AVIF build-stage directory; configure removed it. After the final configure, both plugin package directories contained exactly the five named files, no `README.txt`, and all three static staged documents were byte-identical to their source copies.
- `LICENSE` is the standard MIT text and carries the repository identity `Copyright (c) 2026 Roman Kharitonov`, consistent with `CMakeLists.txt`.
- Search found no `README.txt.in` reference outside `docs/tasks/`; the sole remaining `README.txt` reference is finding 2.
- The requested x64 Release pipeline used `PVDKIT_BUILD_SUFFIX=-review` and a six-job build. Configure and build exited 0; Ninja completed 79 actions with no compiler/linker warnings. Full CTest result:

  ```text
  100% tests passed out of 21
  Total Test time (real) = 24.41 sec
  ```

  This includes `avif_package_docs` and `rpgmvp_package_docs`, both passed, as well as `guard_tests` and both plugins' Release import/export checks. Direct read-back of both x64 DLLs confirmed `KERNEL32.dll` is the only imported module and the export table is exactly the eight bare `pvd*` names.
- Principal commands run and results:

  ```powershell
  rtk git status --short
  rtk git diff
  rtk git diff --check                         # passed, no output
  rtk git check-attr text -- <six document paths>
  rtk proxy git add -n -- plugins/avif/package/readme_en.txt
  rtk proxy git diff -- plugins/avif/package/readme_en.txt
  rtk proxy git hash-object --no-filters plugins/avif/package/readme_en.txt
  rtk proxy git hash-object --path=plugins/avif/package/readme_en.txt plugins/avif/package/readme_en.txt
  rtk rg -n "README\\.txt\\.in|README\\.txt" -g '!docs/tasks/**' .

  $env:PVDKIT_BUILD_SUFFIX = '-review'
  rtk proxy cmake --preset release             # valid configure passed; also run for each negative probe
  rtk proxy cmake --build --preset release --parallel 6  # passed
  rtk proxy ctest --preset release             # 21/21 passed in 24.41 s

  rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-imports.ps1 -Path <each x64 DLL>
  rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-exports.ps1 -Path <each x64 DLL>
  ```

  The byte comparisons, package-stage listing, stale-file setup, and reversible negative mutations were run in PowerShell via `-EncodedCommand`; all source hashes were checked after restoration. Per the review scope, x86, ASan, coverage, lint, and the full packaging pipeline were not run. No network access was used.

## Verdict

REJECT — the implementation does not reject lone-LF English readmes, and one forbidden obsolete filename reference remains outside `docs/tasks/`.

### Orchestrator decision after Fix round 1 (2026-09-13)
Both findings fixed and evidenced in `report-task17.md` "Fix round 1": the CRLF check now covers
`readme_en.txt` (negative test quoted, file restored to SHA-256 B207999C…), the package directory is
recreated instead of naming the obsolete file (grep shows `README.txt` only under `docs/tasks/`),
`ctest --preset release` 21/21. Accepted without a further review round — the change is two
CMake edits with direct evidence.
