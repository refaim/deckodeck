Implemented and staged Task 4 under [src/core](C:/Users/Roma/Dev/PictureView3/avif/src/core) and [tests/core](C:/Users/Roma/Dev/PictureView3/avif/tests/core):

- `PixelBuffer`, transforms, metadata description, `FileSession`, and `AvifPlugin`.
- Comprehensive fakes and TDD coverage for success, errors, exceptions, limits, ownership, animation, transforms, and aborts.
- `avifpvd_core` remains a static library.
- No files outside the assigned directories were edited.
- No commit was created.

### Verification

```powershell
$env:AVIFPVD_BUILD_SUFFIX='-t4'
rtk cmake --preset debug
rtk cmake --build --preset debug --target core_tests guard_tests
rtk ctest --preset debug -R 'core|guard' --output-on-failure
```

Result: **2/2 tests passed**, zero warnings.

Direct doctest result:

```text
test cases:  46 | 46 passed
assertions: 644 | 644 passed
```

Core-only llvm-cov results:

| File | Lines | Branches |
|---|---:|---:|
| AvifPlugin.cpp | 29/29 (100%) | 8/8 (100%) |
| Describe.cpp | 73/73 (100%) | 38/38 (100%) |
| Error.cpp | 27/27 (100%) | 24/24 (100%) |
| FileSession.cpp | 81/81 (100%) | 30/30 (100%) |
| IDecoder.hpp | 2/2 (100%) | — |
| IFileSource.hpp | 2/2 (100%) | — |
| PixelBuffer.cpp | 41/41 (100%) | 8/8 (100%) |
| PixelBuffer.hpp | 2/2 (100%) | — |
| Transform.cpp | 159/159 (100%) | 58/58 (100%) |
| **Core total** | **416/416 (100%)** | **166/166 (100%)** |

Repository coverage command:

```powershell
$env:AVIFPVD_BUILD_SUFFIX='-t4'
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts/coverage.ps1
```

All four integrated tests passed, but the repository-wide gate exited 1 at **95.82% lines / 93.29% branches** because the concurrently developed adapters remain below 100%. Every `src/core/**` row was 100%.

Staging:

```powershell
rtk git add src/core tests/core
rtk git diff --cached --check -- src/core tests/core
```

Succeeded; 24 owned files are staged and the diff check is clean.

No import table was produced: the shared `AVIF.pvd` target is still marked TODO for Task 5 in `src/pvd/CMakeLists.txt`, so Release/import verification is not yet available.