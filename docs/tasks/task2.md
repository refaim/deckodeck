# Task 2 — PVD boundary layer (`src/pvd`, `tests/pvd`)

Repository: current directory. Read `AGENTS.md` and `docs/ARCHITECTURE.md` completely first; §1, §3.2,
§3.3, §3.4 and §5 are your part. The infrastructure (CMake presets, vcpkg, doctest, guard test, canonical
headers `src/pvd/Types.hpp`, `src/pvd/Plugin.hpp`, `src/core/Error.hpp`) already exists — do not
restructure it. Other agents are working **at the same time** in `src/core`, `src/adapters`,
`tests/core`, `tests/adapters`. You own only `src/pvd/**` and `tests/pvd/**`. Do not edit anything else
(if you believe a shared file must change, describe the change in your final report instead).

Build isolation: set `AVIFPVD_BUILD_SUFFIX=-t2` in your environment before any cmake/ctest call so
your build directory is `build/<preset>-t2` and does not collide with the other agents.

## Deliverables (TDD: failing test first, every time)

1. `src/pvd/Firewall.hpp` — `guarded(f, fallback)` and a `void` overload, `noexcept`, the only
   `catch (...)` in `src/`. Tests: returns value on success; returns fallback on `std::bad_alloc`,
   `std::runtime_error`, thrown `int`; void overload swallows; lambdas returning `BOOL`/`UINT32`.
2. `src/pvd/ContextHandle.hpp/.cpp` — `toHost`, `fromHost`, `borrow` per §3.4. Tests: round trip
   preserves identity and ownership (destructor runs exactly once), `fromHost(nullptr)` yields empty,
   `borrow(nullptr)` yields null.
3. `src/pvd/PluginFactory.hpp` — `std::unique_ptr<IPlugin> makePlugin();` declaration only. The
   definition lives in the adapters layer later; **your tests provide their own definition** returning
   a fake plugin so that `Exports.cpp` can be linked into `pvd_tests`.
4. `src/pvd/Shim.hpp/.cpp` — per §3.4. Every method is `guarded`. Null host pointers → `FALSE`/no-op.
   Marshalling rules: `pvdInfoImage` strings point at `std::string` members owned by the session
   (`ImageInfo` is returned by const reference, so `c_str()` stays valid until `fileClose`);
   `pvdInfoPage` from `PageInfo`; `pvdInfoDecode` from `DecodedPage` (`pPalette = nullptr`,
   `nColorsUsed = 0`, `Flags = 0`, positive pitch); `pvdDecodeCallback` + context wrapped into a
   `Progress` lambda (`nullptr` callback → default `Progress`); `pageFree` calls
   `freePage(std::span{pImage, 0})`-style view (the length is unknown; match by data pointer).
   `fileOpen`: `lBuf` bytes at `pBuf` are the head; `lFileSize == 0` means memory mode.
   Tests with fake `IPlugin`/`IFileSession` (doctest): success path for every function; each
   `ErrorCode` from `open`/`pageInfo`/`decodePage` maps to `FALSE`; a fake that throws from each
   method → `FALSE`/no-op and no leak (count live sessions); null `pImageInfo`, `ppContext`,
   `pPageInfo`, `pDecodeInfo`, `pContext`; callback that returns `FALSE` reaches the fake as
   `Progress::report == false`; `pvdInfoImage.Flags` has `PVD_IIF_ANIMATED` iff `animated`;
   `pFormatName`/`pCompression`/`pComments` are the session's strings; `fileClose` destroys the session.
5. `src/pvd/Exports.cpp` — the 8 `extern "C" __stdcall` functions, each a one-liner forwarding to a
   process-wide `Shim` created in `pvdInit` from `makePlugin()` and destroyed in `pvdExit`, all wrapped
   in `guarded`. Before `pvdInit` / after `pvdExit`: `pvdPluginInfo` fills priority 10, name `AVIF`,
   version `1.0.0`, empty comments; other exports return `FALSE`/no-op; `pvdInit` returns 0 if
   `makePlugin()` throws or returns null. Tests link `Exports.cpp` directly into `pvd_tests` (with the
   test-provided `makePlugin()` that can be switched between "returns fake", "returns null",
   "throws") and call the exported functions as plain functions, covering every line and branch,
   including double `pvdInit`/`pvdExit` and calls in the wrong order.
6. `src/pvd/CMakeLists.txt`: turn `avifpvd_pvd` into a STATIC library of everything in `src/pvd`
   except `Exports.cpp`; add `Exports.cpp` as a separate source-list variable/target property that
   the (future) shared library target and `pvd_tests` both use. Do **not** create the shared library
   target yet; keep/adjust the TODO left by Task 1 so that Task 5 can add it.
7. `AVIF.def` already exists: verify the 8 names match your definitions exactly.

## Verify and report
`cmake --preset debug && cmake --build --preset debug --target pvd_tests guard_tests && ctest --preset debug -R "pvd|guard"`
then `scripts/coverage.ps1` (it may report other agents' directories at less than 100 %; your gate
is `src/pvd/**` at 100 % lines and 100 % branches — quote the per-file rows). Include exact commands
and outputs. `git add` your files; do not commit.
