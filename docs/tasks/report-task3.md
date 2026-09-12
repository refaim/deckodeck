Implemented and staged Task 3 under `src/adapters/**` and `tests/adapters/**`.

- Added UTF-8 conversion, extended-path handling, RAII file mapping, and `FileSource`.
- Added complete libavif decoder/factory, metadata/transforms, timing, pixel conversion, error mapping, limits, probing, and version reporting.
- Added comprehensive adapter tests, including all fixtures, exact pixels, hostile inputs, long/Unicode paths, strictness, threading, and every `avifResult`.
- `avifpvd_adapters` links `avif`, `yuv`, `avifpvd_core`; the imported `avif` target supplies the configuration-correct static `dav1d.lib`.
- Existing link smoke test remains unchanged.
- 13 owned files are staged; no commit was created.

Verification commands:

```powershell
$env:AVIFPVD_BUILD_SUFFIX='-t3'
rtk cmake --preset debug
rtk cmake --build --preset debug --target adapter_tests guard_tests
rtk ctest --preset debug -R "adapter|guard"
```

Result: `2/2 passed`; zero warnings under `/W4 /WX`.

Direct test counts:

- Adapter tests: `22/22`, `668/668` assertions.
- Guard tests: `11/11`, `109/109` assertions.

Coverage command:

```powershell
$env:AVIFPVD_BUILD_SUFFIX='-t3'
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File scripts/coverage.ps1
```

Result: `4/4` tests passed; overall `100%` lines and `100%` branches.

| Adapter file | Lines | Branches |
|---|---:|---:|
| `avif/Decoder.cpp` | 100.00% | 100.00% |
| `avif/Decoder.hpp` | 100.00% | — |
| `win/FileMapping.cpp` | 100.00% | 100.00% |
| `win/FileMapping.hpp` | 100.00% | — |
| `win/FileSource.cpp` | 100.00% | 100.00% |
| `win/Utf8.cpp` | 100.00% | 100.00% |

Not completed outside Task 3 ownership:

- No import table was produced because the current shared `src/pvd/CMakeLists.txt` still explicitly leaves the `avifpvd` shared target to Task 5.
- `tests/fixtures/SOURCES.md` should be corrected by its owner: libavif reports the assembled `sofa_grid1x5_420.avif` as `1024×770`, not `1024×154`, and `color_grid_alpha_nogrid.avif` as `80×80`, not `80×64`.
- libavif 1.4.2 rejects `clap_irot_imir_non_essential.avif` during parsing because a known `clap` association is non-essential. The adapter therefore tests it as `ParseFailed`; exposing metadata would require forbidden container rewriting/parsing.