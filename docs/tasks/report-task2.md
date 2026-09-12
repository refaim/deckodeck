Implemented and staged Task 2 under `src/pvd/**` and `tests/pvd/**`.

Completed:

- Exception firewall with value/void overloads.
- Context ownership conversion helpers.
- Plugin factory declaration.
- Fully guarded PVD shim and marshalling.
- Eight process-wide C ABI exports with safe pre-init/post-exit behavior.
- Static-library/source-property CMake arrangement; no shared target created.
- Comprehensive doctest fakes covering errors, exceptions, nulls, callbacks, ownership, and all exports.
- `AVIF.def` exactly matches all eight definitions.
- Only `Firewall.hpp` contains `catch (...)`.

Key files: [Firewall.hpp](/C:/Users/Roma/Dev/PictureView3/avif/src/pvd/Firewall.hpp), [Shim.cpp](/C:/Users/Roma/Dev/PictureView3/avif/src/pvd/Shim.cpp), [Exports.cpp](/C:/Users/Roma/Dev/PictureView3/avif/src/pvd/Exports.cpp), [ShimTests.cpp](/C:/Users/Roma/Dev/PictureView3/avif/tests/pvd/ShimTests.cpp), [ExportsTests.cpp](/C:/Users/Roma/Dev/PictureView3/avif/tests/pvd/ExportsTests.cpp).

Verification, always with `AVIFPVD_BUILD_SUFFIX=-t2`:

```powershell
cmake --preset debug
cmake --build --preset debug --target pvd_tests guard_tests
ctest --preset debug -R "pvd|guard"
```

Result: 2/2 tests passed, zero compiler warnings.

Direct doctest counts:

- `pvd_tests`: 23 test cases, 207 assertions passed.
- `guard_tests`: 11 test cases, 109 assertions passed.

Coverage command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/coverage.ps1
```

All four test executables passed. PVD coverage:

| File | Lines | Branches |
|---|---:|---:|
| ContextHandle.cpp | 100% | — |
| Exports.cpp | 100% | 100% |
| Firewall.hpp | 100% | — |
| Plugin.hpp | 100% | — |
| Progress.cpp | 100% | 100% |
| Shim.cpp | 100% | 100% |
| Types.hpp | 100% | — |
| **PVD total** | **100%** | **100%** |

The overall coverage script exited nonzero because concurrent adapter code currently totals below 100%; repository totals were 95.82% lines and 93.29% branches. This does not affect the requested PVD gate.

`git add src/pvd tests/pvd` succeeded. No commit was created.

No import-table check was run because this task explicitly leaves the shared `AVIF.pvd` target for Task 5.