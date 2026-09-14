# Task 21 report — release text consolidation

## Done

- Set AVIF's single-source plugin identity and adapter assertion to `1.1.0`; RPGMVP's identity was
  already `1.1.0` and was left unchanged.
- Consolidated each plugin's `Changes` section into the requested `1.1.0` and original `1.0.0`
  lines.
- Rebuilt both package ChangeLogs with PowerShell `[IO.File]::ReadAllBytes` /
  `[IO.File]::WriteAllBytes`: each now has exactly the dated `1.1.0` entry and its unchanged
  original `1.0.0` entry.
- Checked all four package `readme_en.txt` / `readme_ru.txt` files: no semantic-version literals;
  no edits made.

## Shared-tree coordination

- Did not touch Task 20's `src/core/colour/**`, `src/core/FileSession.cpp`, `tests/core/colour/**`,
  or `plugins/avif/tests/e2e/E2eTests.cpp` work.
- The required version grep now finds only Task 20's two deferred AVIF assertions:
  `plugins/avif/tests/e2e/E2eTests.cpp:190` and `:680`, both still `"1.2.0"`. The orchestrator must
  change these to `"1.1.0"` after Task 20 lands.
- No commit was made.

## Verification

`rtk git grep -n '1\.2\.0' -- ':!docs/tasks' ':!docs/host'` — exit 0; only the two excluded E2E
matches above.

PowerShell byte verification using `[IO.File]::ReadAllBytes` — exit 0:

- AVIF: BOM present; LF 16 = CR 16; lone LF/CR 0/0; 2 entries; exact first line and underline;
  original 1.0.0 suffix unchanged; longest line 73.
- RPGMVP: BOM present; LF 12 = CR 12; lone LF/CR 0/0; 2 entries; exact first line and underline;
  original 1.0.0 suffix unchanged; longest line 71.

`rtk git grep -n -E '[0-9]+\.[0-9]+\.[0-9]+' -- plugins/avif/package/readme_en.txt plugins/avif/package/readme_ru.txt plugins/rpgmvp/package/readme_en.txt plugins/rpgmvp/package/readme_ru.txt`
— exit 1 with no output, meaning no matches.

`rtk git diff --stat` — exit 0 (shared worktree, including Task 20):

```text
plugins/avif/CMakeLists.txt                        |  2 +-
plugins/avif/README.md                             |  6 +-
plugins/avif/package/ChangeLog                     | 20 ++----
plugins/avif/tests/adapters/DefaultPluginTests.cpp |  2 +-
plugins/avif/tests/e2e/E2eTests.cpp                | 24 ++++++-
plugins/rpgmvp/README.md                           |  4 +-
plugins/rpgmvp/package/ChangeLog                   | 11 ++-
src/core/colour/Pipeline.cpp                       | 52 +++++++++++++-
src/core/colour/Pipeline.hpp                       |  5 ++
tests/core/colour/PipelineTests.cpp                | 80 ++++++++++++++++++++++
10 files changed, 171 insertions(+), 35 deletions(-)
```

An extra `rtk proxy git diff --check` exited 1 because Git reports the mandatory CR bytes on newly
added ChangeLog lines as trailing whitespace; the byte checks above confirm those files are the
required BOM + CRLF with no malformed newline.

## Not run

Per the task's shared-build-machine instruction, no CMake, build, ctest, coverage, guard, lint,
import-table, or export-table command was run. Test counts, coverage numbers, and import/export
results are therefore not available for this text-only task.
