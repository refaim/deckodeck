# Task 24 implementation report: KERNEL32-only under the newest MSVC toolset

Date: 2026-09-15

Branch: `master`

Starting and final observed `HEAD`: `3fa65a49a184a86f97d0470a35df167b37dbbf1e` (tree was clean; nothing
was committed, staged, reset, checked out or pushed; no worktree; nothing installed; only tools
already on the machine were used)

Build suffix: `-t24`

Toolchain on this machine (printed by the same step the CI now runs): clang-cl 19.1.5, MSVC toolset
14.44.35207 (the only one under `VC\Tools\MSVC`, and the one clang-cl resolves). The failing
toolset, MSVC 14.51.36231 / clang-cl 22.1.3, is not installed here and was not installed.

## Outcome

Every candidate that makes MSVC >= 14.50 import `api-ms-win-core-synch-l1-2-0.dll` is gone from
`src/**` and `plugins/*/src/**`, without a change in behaviour or output:

1. the function-local `static const SrgbOutputTables` is replaced by one instance owned by each
   plugin's composition root and injected by `const&` through `CodecPlugin` and `FileSession` into
   every `Presentation` (review-task20 option (a), ARCHITECTURE section 2);
2. the `std::jthread` band workers are `std::thread` objects joined by a scope-bound RAII joiner;
3. the guard test forbids the whole family (tokens and function-local statics) with tests for every
   token in both polarities; the tree walk passes on the new sources and fails on the HEAD
   `Pipeline.cpp` (both tokens named);
4. AGENTS.md rule 13, ARCHITECTURE sections 2/3.7/4/5/7, both `DESIGN.md`, README's CI paragraph
   and `build.yml` (a "Toolset versions" step) are updated.

`llvm-nm --undefined-only` on the release objects shows `_Init_thread_*`,
`__std_atomic_wait_direct` and `__std_atomic_notify_all_direct` gone on x64 and x86; both release
DLLs import `KERNEL32.dll` only on 14.44 (as before - that toolset never showed the problem). The
proof on 14.51 is the next CI run.

Cost, measured and documented: the tables are now built in every `pvdInit` (5.3 ms x64 / 15 ms
x86 in Release, 12 / 25 ms in Debug). The host calls `pvdInit` once per plugin load; the leak
scenarios that cycle it about 650 times pay it (details under "Behaviour and cost").

## What changed (file:line)

### 1. No process-wide state: the tables are owned by the composition root

- `src/core/colour/Pipeline.hpp:20-31` - `SrgbOutputTables` unchanged in contents; its comment now
  says who owns it. `:47-49` `Presentation(const Cicp&, std::optional<float>, const
  SrgbOutputTables&)`; `:68` `outputTables()` kept; `:83` the borrowed reference member. The
  free function `srgbOutputTables()` is deleted from the header and from `Pipeline.cpp` (the
  `static const SrgbOutputTables tables;` at former `Pipeline.cpp:108` and its
  `static_assert(is_trivially_destructible_v<...>)`, whose only purpose was the static's `atexit`
  story, are gone; `<type_traits>` replaced by `<utility>`).
- `src/core/colour/Pipeline.cpp:149-155` - the constructor stores the injected reference.
- `src/core/FileSession.hpp:26-29, :42, :50` and `src/core/FileSession.cpp:40-49, :144-147` - the
  session takes `const colour::SrgbOutputTables&` as its last constructor parameter, keeps it in
  `outputTables_`, builds its `Presentation` over it (`:47`) and exposes it through
  `outputTables()` so a test can pin the sharing (the same reason `Presentation::outputTables()`
  already existed).
- `src/core/CodecPlugin.hpp:17-20, :24-27, :41` and `src/core/CodecPlugin.cpp:12-16, :51-52` - the
  plugin takes the tables after the describer, holds the reference and hands it to every
  `FileSession` it opens.
- `plugins/avif/src/DefaultPlugin.cpp:10, :45-47, :52, :70-73` and
  `plugins/rpgmvp/src/DefaultPlugin.cpp:9, :43, :61-64` - the composition root owns
  `core::colour::SrgbOutputTables outputTables_` declared before `plugin_`, so it is constructed
  first and destroyed last (ARCHITECTURE section 2: injected by reference, outlives its users by
  construction). The object lives inside the `DefaultPlugin` that `std::make_unique` allocates, so
  the 384 KiB are one heap block per `pvdInit`, freed by `pvdExit`; every leak scenario passes on
  every preset.

Why at construction and not lazily on the first HDR session: the task offered both; lazily is
only safe with synchronisation, because `pvdFileOpen` runs on several threads at once - ARCHITECTURE
section 7 ("the host may decode several files at once"), the leak scenario "8 threads decoding
concurrently" (`tests/support/leak/LeakScenarios.cpp:431-455`, whose warm-up pass already opens HDR
fixtures from 8 threads) and the e2e case "four threads decode the same fixture concurrently" - and
the task forbade a lock, double-checked locking and atomics for this. Construction-time building
needs none of them. The alternative, should the `pvdInit` cost ever matter, is a lazy
`std::unique_ptr<const SrgbOutputTables>` in `CodecPlugin` behind the SRWLOCK-backed `std::mutex`
that AGENTS.md rule 13 permits ("if a lock is ever needed"); it is not implemented.

`Exports.cpp`'s `std::unique_ptr<ProcessState> processState` (namespace scope, constant-initialised,
non-trivial destructor) is untouched: it is the composition root itself, the one `atexit`
registration a plugin DLL makes (verified below), and `atexit` is static-CRT-internal - it adds no
import on any toolset. ARCHITECTURE section 7 and AGENTS.md rule 13 now name it as the exception.

### 2. `std::thread` instead of `std::jthread`

- `src/core/colour/Pipeline.cpp:35-69` - `BandWorkers` (anonymous namespace): a
  `std::vector<std::thread>` reserved to the band count, `start(Task&&)` emplaces, the destructor
  joins every thread; copy and move deleted. `:211-238` `applyImage` declares
  `BandWorkers workers{bandCount - 1}` before the band loop, so the workers are joined before the
  call returns on the normal path and, when a later `std::thread` constructor throws
  `std::system_error`, during unwinding - exactly what the `std::vector<std::jthread>` did, minus
  the `stop_token` state. No `stop_token`, no `request_stop`, no `catch (`. The last band still
  runs on the calling thread; the band arithmetic is untouched.

### 3. Guard

`tests/guard/GuardTests.cpp`:

- `:270-303` - the synchronisation family in `forbiddenEverywhere`, each rule preceded by its
  one-line reason ("MSVC >= 14.50 ... api-ms-win-core-synch-l1-2-0.dll ..."):

  | label | regex (on lowercased, comment- and literal-stripped code) |
  |---|---|
  | `jthread` | `\bjthread\b` |
  | `stop_token/stop_source/stop_callback` | `\bstop_(?:token\|source\|callback)\b` |
  | `call_once/once_flag` | `\b(?:call_once\|once_flag)\b` |
  | `latch` | `#include <latch>` (spaces allowed) or `::latch\b` |
  | `barrier` | `#include <barrier>` or `::barrier\b` |
  | `semaphore` | `#include <semaphore>` or `::(counting_\|binary_)?semaphore\b` |
  | `condition_variable` | `\bcondition_variable(?:_any)?\b` (also matches the include) |
  | `.wait(/.notify_one(/.notify_all(` | `(?:\.\|->)\s*(?:wait(?:_for\|_until)?\|notify_one\|notify_all)\s*\(` |
  | `atomic_wait/atomic_notify_*` | `\batomic_(?:wait\|wait_explicit\|notify_one\|notify_all)\b` |

  `::latch` rather than `\blatch\b` so an identifier merely called `latch_count` or `latches` is
  not the token; the member spelling catches `x.wait(`, `x .wait (`, `p->wait(`, `wait_for`,
  `wait_until`, and the free-function spelling the `std::atomic_wait` family.

- `:262-268, :330-435` - the function-local static rule. `appendFunctionLocalStaticViolations`
  walks the stripped code, classifies every `{` by its header (the code since the previous `;`,
  `{` or `}`; `opensDeclarativeScope` blanks access specifiers, `[[attributes]]` and `alignas(...)`,
  `withoutTemplateHeads` removes balanced `template <...>` heads, then a header that starts with
  `namespace`/`class`/`struct`/`union`/`enum` (optionally after `inline`/`export`/`typedef`/`friend`)
  and contains no `(` - or is exactly `extern` after literal stripping, i.e. `extern "C" {` - is
  declarative; everything else is executable: function bodies, control statements, lambdas, brace
  initialisers). A `static` word (word boundaries, so `static_assert` and `static_cast` are other
  tokens) whose innermost scope is executable and whose next token is not `constexpr` is reported
  as `function-local static (only static constexpr is allowed)`. Namespace-scope and class-scope
  `static` are not this rule's business (documented; see "what only CI can prove").
  Known limits of the heuristic, all documented in the code: an elaborated-type return
  (`struct tm *f()`) is classified by the `(` rule, a `requires` clause before `struct` would be
  taken as executable (no static member functions exist in such a header today), raw string
  literals are not understood by the pre-existing stripper.
- `:593` - the rule runs on every scanned file.

Tests (`tests/guard/GuardTests.cpp`):

- `:678-704` - the existing three-polarity loop ("every unconditional rule has positive and negative
  scanner samples") gained 26 samples: each token is flagged as code, and not flagged inside a
  `//` comment or a string literal, plus `void f() { static const Tables tables; }`.
- `:1024-1106` "the synchronisation family behind the Windows 8 synch API set is caught in every
  spelling" - 36 flagged spellings (each checked under `src/core`, a plugin adapter and
  `src/pvd/Exports.cpp`) and 24 allowed ones (`std::thread`, `join`, `std::mutex`, `scoped_lock`,
  plain atomics `fetch_add`/`load`, `waiting`, `awaitable.await()`, `notify_owner()`,
  `host.notify_one_page()`, `latch_count`, `latches`, `barrier_free()`, `semaphore_like()`,
  `my_stop_token()`, `call_once_more()`, `jthreads_started`, a comment, a literal).
- `:1108-1194` "a function-local static is forbidden unless it is constexpr" - 27 flagged shapes
  (plain, `const`, `auto`, `= 0`, `{}`, direct-init `(1, 2)`, `thread_local`, split over lines,
  inside `if`/`for`/`do`/`switch` blocks, inside a lambda, after a brace initialiser, in a
  `template <class F>` function, in a `template <typename T, typename U = Pair<T, T>>` function, in
  `extern "C" UINT32 __stdcall pvdInit(void)`, in member functions of classes inside namespaces
  (`public:`, `final : Base`, `override`), after an `enum class` declaration, in a constructor with
  a mem-initialiser list, in a trailing-return function, in a default member initialiser lambda)
  and 22 allowed shapes (`static constexpr` scalar and array, `static_assert`, `static_cast`,
  static member functions in `class`/`struct`/`union`/local class/`template` class/`inline
  namespace`, `[[nodiscard]] static`, a static member function with a body, namespace-scope
  `static int x = 0`, `static int helper()`, `extern "C" { }`, an unbalanced `}`, comment, literal).

Result on the tree (evidence, run before the fix with the HEAD `Pipeline.cpp` copied back in
place and restored afterwards - file copy only):

```
$ guard_tests.exe -tc="the shared and every plugin source tree obey*"      (HEAD Pipeline.cpp)
  C:/.../src/core/colour/Pipeline.cpp: forbidden token jthread
  C:/.../src/core/colour/Pipeline.cpp: forbidden token function-local static (only static constexpr is allowed)
[doctest] test cases: 1 | 0 passed | 1 failed
$ guard_tests.exe                                                            (new sources)
[doctest] test cases:  21 |  21 passed | 0 failed | 0 skipped
[doctest] assertions: 565 | 565 passed | 0 failed |
```

### 4. Documentation and CI

- `AGENTS.md:74-86` - rule 13, the text the task asked for plus the `processState` exception and
  the pointer to the guard and the CI print.
- `docs/ARCHITECTURE.md` - section 2 (`const colour::SrgbOutputTables&` among the injected
  references; "no other way to hold state across sessions"), section 3.7 (colour bullet: who builds
  the tables and at what cost, `std::thread` + `BandWorkers`, why not `jthread`; `CodecPlugin` ctor
  signature; `FileSession` members; the composition-root bullet), section 4 (the CI print), section
  5 (the guard's new tokens and the scope classifier; the leak-test cost since Task 24), section 7
  (the "single piece of process-wide state" paragraph is gone; in its place: the concurrency
  contract, the KERNEL32-only rule with the observed CI facts and what to use instead, and the
  tables as the worked example with the reason construction-time building needs no lock).
- `plugins/avif/DESIGN.md:10-14`, `plugins/rpgmvp/DESIGN.md:55-59` - the composition root owns the
  tables (RPGMVP: libspng reports identity CICP, so its tables are never used today).
- `README.md:182-207` - the CI paragraph no longer claims "VS 2022 with clang-cl 19": the image is
  VS 2026 with MSVC 14.51 / clang-cl 22 (observed on the first run), which is why the build jobs
  print the toolset; the `build.yml` description mentions the step.
- `.github/workflows/build.yml:39-60` - step "Toolset versions (MSVC, clang-cl)" before Configure:
  `clang-cl --version`, `VCToolsVersion` / `VCToolsInstallDir` when vcvars set them, the directory
  names under `<VS>\VC\Tools\MSVC` (the VS root is five levels above `PVDKIT_LLVM_DIR`), and the
  toolset clang-cl actually resolves, read from its `-###` driver line (`-internal-isystem
  ...\VC\Tools\MSVC\<ver>\include`; run through `cmd /c ... 2>&1` so stderr handling is the same in
  pwsh 7 and Windows PowerShell). Verified locally with the extracted step under Windows PowerShell
  and `$ErrorActionPreference = 'Stop'`:

  ```
  clang version 19.1.5
  Target: x86_64-pc-windows-msvc
  InstalledDir: C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin
  MSVC toolsets under C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools: 14.44.35207
  MSVC toolset resolved by clang-cl: 14.44.35207
  exit=0
  ```

  Not changed (out of scope, worth a follow-up): `.github/actions/toolchain/action.yml` still
  comments "the same LLVM 19 the reference machine builds with" while its fallback glob is what
  actually found VS 2026's clang-cl 22 on the runner.

## TDD evidence

Tests were written first; the failing runs before any production change:

```
$ cmake --build --preset debug --target guard_tests && guard_tests.exe        (rules not yet written)
[doctest] test cases:  21 |  18 passed |   3 failed | 0 skipped
[doctest] assertions: 565 | 350 passed | 215 failed |
TEST CASE:  the synchronisation family behind the Windows 8 synch API set is caught in every spelling
TEST CASE:  every unconditional rule has positive and negative scanner samples
TEST CASE:  a function-local static is forbidden unless it is constexpr

$ cmake --build --preset debug --target core_tests                             (API not yet changed)
tests\core\FileSessionTests.cpp(60,25): error: no matching constructor for initialization of 'FileSession'
tests\core\FileSessionTests.cpp(83,25): error: no matching constructor for initialization of 'FileSession'
... (every FileSession/CodecPlugin/Presentation construction with the injected tables)
```

Then the code, then green (Debug x64):

```
core_tests:  [doctest] test cases: 89 | 89 passed | 0 failed | 6 skipped   assertions: 332525 | 332525 passed
guard_tests: [doctest] test cases: 21 | 21 passed | 0 failed | 0 skipped   assertions: 565 | 565 passed
```

New and adjusted test cases:

- `tests/core/colour/PipelineTests.cpp:29-36` `testTables()` (one instance per test executable,
  standing in for the composition root's); `:201-233` "a Presentation borrows exactly the sRGB
  output tables it is given, on any thread" replaces the process-wide sharing case: two heap
  instances, three Presentations, identity checks against each, and four worker threads
  constructing a Presentation over the first instance; `:128-160` the construction timing case
  keeps its table row and times a Presentation over borrowed tables (no "first" row any more, the
  first construction is no longer special); the exhaustive diagnostic and every other case pass the
  tables explicitly (17 call sites).
- `tests/core/Fakes.hpp:328-336` `test::outputTables()`; `tests/core/FileSessionTests.cpp` (22 call
  sites) and `tests/core/FakeTests.cpp` (7) pass it; `FileSessionTests.cpp:319-356` "a FileSession
  borrows the sRGB output tables it is given and never builds its own": HDR and SDR sessions over
  one instance and a third session over another report the injected instance, and the HDR page
  decoded through the session equals the fake decoder's BGRA64 frame converted by a `Presentation`
  built directly over the same tables.
- `tests/core/CodecPluginTests.cpp:35-38, :42` the harness owns
  `std::unique_ptr<const SrgbOutputTables>` (heap, not the test stack); `:122-155` "every session
  of one CodecPlugin borrows the plugin's sRGB output tables": two sessions of one plugin and one
  of a second plugin (`dynamic_cast` to the concrete `FileSession` the plugin constructs), identity
  checks, then a 64-bit decode through the first session.
- `applyImage` band tests (`PipelineTests.cpp:248-286`, `FileSessionTests.cpp:358-380`) are
  unchanged in spirit and now exercise `std::thread` + `BandWorkers`; the four-thread and 8-thread
  DLL cases exercise the composition-root-owned tables concurrently.

Unchanged outputs, as required:

- the 65,536-point grid ("linear input quantization matches the exact sRGB OETF over all 16-bit
  samples") - part of `core_tests`, passing on every preset;
- the exhaustive diagnostic (Release x64, `--no-skip=true`):
  `exhaustive [0, 1]: 1065353217 floats, 0 mismatches, 0 exact-path monotonicity violations`;
- both whole-image FNV hashes in `plugins/avif/tests/e2e/E2eTests.cpp:436-448` are untouched
  (`14'703'790'622'216'699'421`, `5'389'512'495'027'945'087` x64 / `15'569'853'467'021'997'067`
  x86) and "HDR AVIF is presented as 64-bit sRGB through the DLL" passes on x64 and x86, Debug,
  Release, ASan and both coverage builds.

## Symbol dumps

`llvm-nm --undefined-only`, filtered to `init_thread|atomic|Thrd|Cnd|Mtx|stop|Wait|Wake|atexit|onexit`:

```
== HEAD (build/release-t23) src/core/.../colour/Pipeline.cpp.obj, x64
         U _Cnd_do_broadcast_at_thread_exit
         U _Init_thread_epoch
         U _Init_thread_footer
         U _Init_thread_header
         U _Thrd_id
         U _Thrd_join
         U __std_atomic_notify_all_direct
         U __std_atomic_wait_direct

== Task 24 (build/release-t24) Pipeline.cpp.obj, x64
         U _Cnd_do_broadcast_at_thread_exit
         U _Thrd_id
         U _Thrd_join
== Task 24 FileSession.cpp.obj, x64            (none)
== Task 24 CodecPlugin.cpp.obj, x64            (none)
== Task 24 Exports.cpp.obj (avif_plugin), x64  U atexit          <- processState, pre-existing
== Task 24 DefaultPlugin.cpp.obj (avif), x64   U _Thrd_hardware_concurrency

== HEAD (build/release-x86-t23) Pipeline.cpp.obj, x86
         U __Cnd_do_broadcast_at_thread_exit
         U __Init_thread_epoch
         U __Init_thread_footer
         U __Init_thread_header
         U __Thrd_id
         U __Thrd_join
         U ___std_atomic_notify_all_direct@4
         U ___std_atomic_wait_direct@16
== Task 24 (build/release-x86-t24) Pipeline.cpp.obj, x86
         U __Cnd_do_broadcast_at_thread_exit
         U __Thrd_id
         U __Thrd_join
== Task 24 FileSession.cpp.obj, x86            (none)
```

The three that remain are `std::thread`'s: `_Thrd_join` (`WaitForSingleObjectEx`), `_Thrd_id`
(`GetCurrentThreadId`) and `_Cnd_do_broadcast_at_thread_exit` (the thread epilogue's
`WakeAllConditionVariable`), all KERNEL32 on every toolset and all present in the HEAD DLLs already.
The full undefined list of the new `Pipeline.cpp.obj` additionally holds only `_beginthreadex`,
`powf`, `nextafterf`, `lroundf`, `memset`, the CRT exception/cookie symbols and the colour helpers.

`llvm-readobj --coff-imports` on the release DLLs (both architectures): the only module is
`KERNEL32.dll` for `AVIF.pvd` and `RPGMVP.pvd`. Imported-symbol diff HEAD -> Task 24 on 14.44:
`AVIF.pvd` identical; `RPGMVP.pvd` lost `SleepConditionVariableSRW`, the 14.44 STL's fallback path
of `__std_atomic_wait_direct` - the `jthread` was the last user (AVIF keeps it for dav1d's condition
variables).

## Gates

All with `PVDKIT_BUILD_SUFFIX=-t24`, `--parallel 6`, one build at a time, x64 and x86 never
concurrently; zero warnings in every build.

| preset | configure + build | ctest |
|---|---|---|
| `debug` | ok, 0 warnings | 17/17 passed (`avif_leak_tests` 46.5 s, `rpgmvp_leak_tests` 26.3 s) |
| `release` | ok, 0 warnings | 21/21 passed incl. `avif_check_imports`, `avif_check_exports`, `rpgmvp_check_imports`, `rpgmvp_check_exports` (`avif_leak_tests` 21.4 s) |
| `debug-x86` | ok, 0 warnings | 17/17 passed (`avif_leak_tests` 63.6 s, `rpgmvp_leak_tests` 33.1 s) |
| `release-x86` | ok, 0 warnings | 21/21 passed incl. both `_check_imports` / `_check_exports` (`avif_leak_tests` 32.5 s) |
| `asan` | ok, 0 warnings | 17/17 passed, no AddressSanitizer report (`avif_leak_tests` 59.0 s) |

Both x64 presets were rebuilt incrementally and re-tested after clang-format touched the new
sources (17/17 and 21/21 again). `guard_tests` is in every run.

Coverage (`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset ...`,
`CMAKE_BUILD_PARALLEL_LEVEL=6`):

```
coverage (x64):     17/17 tests passed
  src\core\CodecPlugin.cpp        21 0 100.00%   3 0 100.00%   33 0 100.00%   8 0 100.00%
  src\core\FileSession.cpp        84 0 100.00%  11 0 100.00%  105 0 100.00%  48 0 100.00%
  src\core\colour\Pipeline.cpp   108 0 100.00%  20 0 100.00%  176 0 100.00%  66 0 100.00%
  plugins\avif\src\DefaultPlugin.cpp    8 0 100.00%  7 0 100.00%  24 0 100.00%  0 0 -
  plugins\rpgmvp\src\DefaultPlugin.cpp  8 0 100.00%  7 0 100.00%  22 0 100.00%  0 0 -
  TOTAL                         1093 0 100.00% 261 0 100.00% 2147 0 100.00% 696 0 100.00%
  Coverage gate passed: lines 100%, branches 100%.
coverage-x86:       17/17 tests passed
  TOTAL                         1093 0 100.00% 261 0 100.00% 2147 0 100.00% 696 0 100.00%
  Coverage gate passed: lines 100%, branches 100%.
```

Lint (`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6`, and
`-BuildDir build\debug-x86-t24 -ReleaseDir build\release-x86-t24`):

```
x64: clang-format 0, clang-tidy 0 (340.6 s), cppcheck 0, PSScriptAnalyzer 0, BinSkim 0 -> lint: clean
x86: clang-format 0, clang-tidy 0 (307.8 s), cppcheck 0, PSScriptAnalyzer 0, BinSkim 0 -> lint: clean
```

No new suppression, `NOLINT`, pragma, `catch (`, `reinterpret_cast` or `#ifdef`; `git diff --check`
clean. The known x86 `leakcheck_tests` flake did not occur.

## Behaviour and cost

- Presentation timing (Release x64, skipped diagnostics run with `--no-skip=true`): 1024x428
  scalar 78.6 ms, four bands 20.56 ms (Task 20: 77.4 / 20.46 ms); cosmos `pvdPageDecode` 25.5 ms,
  cosmos per file 36.2 ms first / 36.6 ms mean (Task 20: 26.1 / 36.2 ms). The band split behaves
  as before.
- `SrgbOutputTables` construction: 5.33 ms best / 5.83 mean (Release x64), 12.1 ms (Debug x64),
  25.3 ms (Debug x86), 12.3 ms (coverage x64); a Presentation over borrowed tables 1.74 ms
  (Release x64) - the same as Task 20's "then best".
- Where it is paid now: once per `pvdInit`. The host calls `pvdInit` once per plugin load. The leak
  scenarios that cycle `pvdInit` about 650 times per run (disk and memory round trips, init/exit
  cycle, LoadLibrary/FreeLibrary) measure it directly, HEAD -> Task 24 on the same (loaded)
  machine: Debug x64 init/exit cycle 1 ms -> 2467 ms for 200 cycles, disk round trip 3864 -> 6469
  ms, memory round trip 4564 -> 6293 ms, LoadLibrary/FreeLibrary 477 -> 692 ms; Debug x86
  `avif_leak_tests` 46.4 s -> 63.6 s. RPGMVP pays it too although its sessions never present colour.
  ARCHITECTURE section 5 records this next to the former "< 30 s" figures.

## What only the CI run on MSVC 14.51 can prove

- That `AVIF.pvd` and `RPGMVP.pvd` built with vcruntime/STL 14.51 and clang-cl 22 import
  `KERNEL32.dll` only (`avif_check_imports`, `rpgmvp_check_imports`, x64 and x86). Locally, 14.44
  never imported the API set even before this task, so the local `check_imports` cannot fail for
  this reason; the local evidence is the object-level absence of `_Init_thread_*` and
  `__std_atomic_*` above, i.e. of the only two callers the failing import had.
- That nothing else in the 14.51 runtime reaches `WaitOnAddress` through code we still use
  (`std::thread` start/join, `_Cnd_do_broadcast_at_thread_exit`, the UCRT and the static vcruntime
  themselves, dav1d's Win32 threads). The CI import list showed exactly `WaitOnAddress` and
  `WakeByAddressAll` and nothing else from the API set, which matches the two removed callers; if
  14.51 imports it for some other reason, the new "Toolset versions" step and the unchanged
  `check_imports` output will say so.
- That the guard's namespace-scope blind spot is not hit by a future change: it forbids
  function-local statics textually; a namespace-scope object with a constructor (no `static`
  keyword needed) is caught only by review and by the import table on 14.51. Today the tree has one,
  `processState`, which is intended.
- That the "Toolset versions" step prints under the runner's pwsh 7 the way it does under Windows
  PowerShell here (the only pwsh-sensitive construct, native stderr, is routed through `cmd /c`).

## Commands (as run)

```
set PVDKIT_BUILD_SUFFIX=-t24
cmake --preset debug && cmake --build --preset debug --parallel 6 && ctest --preset debug
cmake --preset release && cmake --build --preset release --parallel 6 && ctest --preset release
cmake --preset debug-x86 && cmake --build --preset debug-x86 --parallel 6 && ctest --preset debug-x86
cmake --preset release-x86 && cmake --build --preset release-x86 --parallel 6 && ctest --preset release-x86
cmake --preset asan && cmake --build --preset asan --parallel 6 && ctest --preset asan
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage       (CMAKE_BUILD_PARALLEL_LEVEL=6)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\coverage.ps1 -Preset coverage-x86
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 -Jobs 6 -BuildDir build\debug-x86-t24 -ReleaseDir build\release-x86-t24
llvm-nm --undefined-only build\release{,-x86}-t24\src\core\CMakeFiles\pvdkit_core.dir\{colour\Pipeline,FileSession,CodecPlugin}.cpp.obj
llvm-readobj --coff-imports build\release{,-x86}-t24\plugins\{avif\AVIF,rpgmvp\RPGMVP}.pvd
core_tests.exe --test-case="diagnostic: exhaustive*,Presentation release timing: *" --no-skip=true   (release-t24)
```
