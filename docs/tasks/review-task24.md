# Review: Task 24 — KERNEL32-only under MSVC 14.51 (branch `task24-kernel32`, commit `1a1fd55` on `3fa65a4`)

Reviewer build suffix: `-review`. Everything below was run by the reviewer on this machine
(clang-cl 19.1.5, MSVC 14.44.35207); the import table on MSVC 14.51 is, as the task says, CI's to prove.

## Substantive findings

1. `tests/guard/GuardTests.cpp:301-302` — the free-function spelling of the atomic wait/notify family
   misses the four `atomic_flag_*` functions. The rule is
   `\batomic_(?:wait|wait_explicit|notify_one|notify_all)\b`, so `std::atomic_flag_wait(&flag, false)`,
   `std::atomic_flag_wait_explicit(...)`, `std::atomic_flag_notify_one(&flag)` and
   `std::atomic_flag_notify_all(&flag)` pass the guard (verified with a probe TU that `#include`s
   `GuardTests.cpp` and scans those lines under `project/src/core/Sample.cpp`: `flagged=false` for
   all of them). In the MSVC STL these forward to `atomic_flag::wait/notify_*`, i.e. to
   `__std_atomic_wait_direct` / `__std_atomic_notify_*_direct` — exactly the symbols this task
   removes — so the rule 13 family ("no atomic wait/notify_one/notify_all") has a spelling that
   reintroduces the `api-ms-win-core-synch-l1-2-0.dll` import with the guard silent, and the test
   case named "caught in every spelling" does not cover it. Guard gap / test gap. Fix:
   `\batomic_(?:flag_)?(?:wait|wait_explicit|notify_one|notify_all)\b` (label
   `atomic_wait/atomic_flag_wait/atomic_notify_*`), the four flagged samples in the "every
   spelling" case and one in the polarity loop, and `atomic_flag_*` in ARCHITECTURE §5's token list.

## Nits

1. `AGENTS.md:76-78` / `docs/ARCHITECTURE.md:850-852` — rule 13 states as fact that MSVC ≥ 14.50
   "implements all of them" (`call_once`/`once_flag`, `condition_variable`, `<latch>`/`<barrier>`/
   `<semaphore>`, ...) over `WaitOnAddress`/`WakeByAddressAll`. Only two members were observed
   (`_Init_thread_*` for guarded statics, and `jthread`'s `stop_token` state via
   `__std_atomic_wait_direct`); `condition_variable` is `SleepConditionVariableSRW`-backed and
   `call_once` `InitOnceExecuteOnce`-backed in every STL the reviewer knows. The prohibition is
   fine (conservative), but the justification should say "observed for thread-safe statics and
   atomic wait/notify; the rest of the family is forbidden because it is built on the same
   primitives or cannot be proven otherwise", so the sentence is not read as a measured fact.
2. The family is closed textually but not semantically: `std::timed_mutex`,
   `std::recursive_timed_mutex`, `std::shared_timed_mutex` (header-implemented over
   `condition_variable` in the MSVC STL), `<future>` (`promise`/`future`/`async` over `_Cnd_t`)
   and `<syncstream>` (`_Locked_pointer` over `__std_atomic_wait_direct`) are not named by the
   guard. None is in the tree today. If rule 13 treats `condition_variable` as forbidden, its
   header-level users are the same risk; either add those tokens or say in §5 that they are left
   to review.
3. `README.md:182-195` — the rewritten CI paragraph says the image is "Visual Studio 2026 ...
   MSVC 14.51 and clang-cl 22" and, twelve lines later, that the release workflow's
   `verify`/`release` jobs "find the runner's LLVM through the VS 2022 layout fallback of
   `scripts/llvm-dir.ps1`". `Get-LlvmDir` (`scripts/llvm-dir.ps1:30-38`) knows only the five VS
   2022 layouts and then PATH; on the image the paragraph itself describes there is no VS 2022
   layout (the toolchain action needed its any-VS-major glob to find clang-cl 22), so `pack.ps1`
   there (`check-imports.ps1:10-11`, `check-exports.ps1:16-17`) resolves through PATH — the
   runner's standalone LLVM — not the VS 2022 fallback. Pre-existing Task 23 sentence, but the
   paragraph was rewritten in this task and is now self-contradictory; either fix the sentence
   or, better, give `llvm-dir.ps1`/`find-llvm.cmake` the same any-VS-major fallback the action has.
4. `AGENTS.md:128` — the Layout table still says `tests/guard/ source-scanning test enforcing
   rules 2–4`; it now enforces rule 13 as well.
5. `docs/tasks/report-task24.md:61-62` — the `DefaultPlugin.cpp` line references (`:10`, `:9`)
   point at `#include <thread>`; the added `core/colour/Pipeline.hpp` include is at avif `:16` /
   rpgmvp `:15`. The guard sample counts are also slightly off (23 new polarity samples, not 26;
   28 flagged static shapes, not 27). Cosmetic.
6. `docs/ARCHITECTURE.md:769-776` — "`avif_leak_tests` met it there until Task 24: ~15 s in
   Release, ~24-27 s in Debug" next to "went from 46 s to 64 s in Debug x86": the two Debug
   figures come from different machine loads and the sentence reads as if the budget was met at
   46 s. Say which figure is the unloaded one (this review measured Debug x64 `avif_leak_tests`
   50.6 s, Release x64 22.9 s, Release x86 33.1 s — the plain-preset budget is no longer met in
   Debug either way, which §5 should state plainly rather than by implication).

## Verified

Scope files read line by line against AGENTS.md rules 1–13 and ARCHITECTURE §2/§3.7/§4/§5/§7;
`git diff master..task24-kernel32` (22 files, +1305/−170), `git diff --check` clean, working tree
clean, no lint configuration / preset / script / `NOLINT` / `#pragma` changes.

(1) Ownership of `SrgbOutputTables`: `plugins/avif/src/DefaultPlugin.cpp:67-74` and
`plugins/rpgmvp/src/DefaultPlugin.cpp:58-65` declare `outputTables_` after the describer and before
`plugin_`, so it is constructed before and destroyed after the `CodecPlugin` that borrows it;
`CodecPlugin` (`src/core/CodecPlugin.hpp:41`, const-ref member, copy/move deleted) hands the same
reference to every `FileSession` (`src/core/CodecPlugin.cpp:51-52`), which stores it
(`FileSession.hpp:50`) and passes it to its one `Presentation` (`FileSession.cpp:47`), which stores
it (`Pipeline.hpp:83`). Sessions live in host handles and are unreachable after `pvdExit` (every
export checks `processState`), so no session can touch its tables after the composition root dies.
Textual sweep of `src/**` and `plugins/*/src/**`: the only `static` keywords are `static constexpr`,
`static_assert`, `static_cast` and static member functions; the only namespace-scope object is
`std::unique_ptr<ProcessState> processState` (`src/pvd/Exports.cpp:33`). Symbol-level sweep over
every production `.cpp.obj` of the Release build, x64 and x86 (`llvm-nm --undefined-only`
filtered to `init_thread|atomic_wait|atomic_notify|WaitOnAddress|WakeByAddress|_Cnd_|_Thrd_|atexit|onexit`,
and `--defined-only` for `??__E`/`??__F`):

```
Pipeline.cpp.obj (x64):     U _Cnd_do_broadcast_at_thread_exit  U _Thrd_id  U _Thrd_join
Pipeline.cpp.obj (x86):     U __Cnd_do_broadcast_at_thread_exit U __Thrd_id U __Thrd_join
DefaultPlugin.cpp.obj x2:   U _Thrd_hardware_concurrency
Exports.cpp.obj x2:         U atexit ; defined: ??__FprocessState@?A0x589C843@@YAXXZ
every other object:         nothing from the family; no ??__E (dynamic initialiser) anywhere
```

So `processState` is constant-initialised (no dynamic initialiser) but not trivially
destructible: its `??__F` atexit destructor is the one `atexit` registration, exactly as AGENTS.md
rule 13 and ARCHITECTURE §7 document (`atexit` is UCRT, critical-section locked; no synch API set).
No `_Init_thread_*`, `__std_atomic_wait_direct` or `__std_atomic_notify_all_direct` remains in any
object. Import tables (`llvm-readobj --coff-imports`): `AVIF.pvd` and `RPGMVP.pvd`, x64 and x86
(`IMAGE_FILE_MACHINE_I386`): `Name: KERNEL32.dll` only; `RPGMVP.pvd` no longer imports
`SleepConditionVariableSRW` (the 14.44 atomic-wait fallback), `AVIF.pvd` keeps it for dav1d.
Tables identical: the 65,536-point grid test is in `core_tests` (green on every run below); the
exhaustive diagnostic on the Release build:
`exhaustive [0, 1]: 1065353217 floats, 0 mismatches, 0 exact-path monotonicity violations`;
`plugins/avif/tests/e2e/E2eTests.cpp` (the FNV hashes) is not in the diff and `avif_e2e_tests`
passes on x64 and x86. The eager `pvdInit` cost is documented in §3.7/§5/§7 and both DESIGN.md and
reproduces here (Release x64): `SrgbOutputTables construction ... best 5.2395 ms, mean 5.38608 ms`,
`Presentation ... over borrowed tables: best 1.7443 ms`; scalar 1024x428 81.5 ms, four bands 20.8 ms.

(2) `BandWorkers` (`src/core/colour/Pipeline.cpp:42-69`): `std::vector<std::thread>` reserved to
`bandCount - 1`, `start()` emplaces, the destructor joins every element; copy and move deleted;
declared before the band loop (`:227`), so a `std::system_error` from a later `std::thread`
constructor (`emplace_back` has no effect on failure) unwinds through the joiner while the started
bands finish on their disjoint subspans, which outlive the joiner (the `PixelBuffer` is
`decodePage`'s local, deeper in the stack). No `catch (`, no `stop_token`, the lambda and `apply`
are `noexcept`, `Presentation` is immutable after construction and the bands are disjoint row
ranges — no data race, no exception crosses a thread. Band policy unchanged:
`min(clamp(maxThreads, 1, 4), height)` with the 256 Ki pixel threshold (`:214-218`). Serial ==
banded is still pinned in `tests/core/colour/PipelineTests.cpp:251-290` (0/1/2/3/4/8 threads,
513-row image) and `tests/core/FileSessionTests.cpp:358-380` (1 vs 0/2/8 through `decodePage`).
The new sharing tests (`PipelineTests.cpp:201-233`, `FileSessionTests.cpp:319-356`,
`CodecPluginTests.cpp:122-155`) assert identity against two distinct heap instances, so they
cannot pass against a hidden shared object.

(3) Guard: `guard_tests.exe` (Release): `test cases: 21 | 21 passed`, `assertions: 565 | 565
passed`. Classifier (`GuardTests.cpp:345-435`) read in full. Adversarial probe TU (scratchpad
`GuardProbe.cpp`, `#include`s the repository's `GuardTests.cpp`, compiled with clang-cl against
vcpkg's doctest; repository untouched): the master `Pipeline.cpp` (from
`git show master:src/core/colour/Pipeline.cpp`) is rejected with exactly two violations, `jthread`
and `function-local static (only static constexpr is allowed)`; the six current scope files scan
clean; 30 false-negative shapes all flagged (function-call initialiser inside a lambda, bare nested
`{ }` and `{ { } }` scopes, after a `for(;;)` header, `if` with init-statement, lambda as a call
argument, after a local anonymous struct / local enum / namespace alias, member function of a
local class, after `{{{1}}}` brace-init, immediately invoked lambda in a default member
initialiser and in a mem-initialiser, trailing `requires`, `try` block, function-try-block,
`if constexpr`, `while`, `do`, `switch` case block, after `static_assert`+`static_cast` on the same
line, `constexpr` function body, `noexcept` lambda with captures, `struct tm *now()`, a class whose
base clause carries parentheses, structured binding, `inline namespace`, `static const std::array`,
`static thread_local`); 39 false-positive shapes all clean (`static constexpr` incl. split over
lines, `static_assert`, `static_cast`, static member functions after brace default-member-
initialisers / inline bodies / mem-init constructors / `enum class` / `friend`, `alignas(16)`,
`[[nodiscard]]`, `[[deprecated("x")]]`, `final : public`, template class, static data member,
`typedef struct`, `extern "C" { }`, namespace-scope `static`, `union`, local class, string/char
literals containing `{`/`}`, digit separators, a raw string with a brace). Probe run:
`assertions: 93 | 93 passed`. Family tokens: every listed spelling caught, including
`STD::JTHREAD`, `a\n.wait(0)`, `using std::latch;`; the `atomic_flag_*` free functions are the one
hole (Substantive 1).

(4) Docs: AGENTS.md rule 13, ARCHITECTURE §2 (injected refs incl. `const colour::SrgbOutputTables&`),
§3.7 (`CodecPlugin` ctor signature, `FileSession` members, `BandWorkers`, cost), §4 (toolset print),
§5 (token list and classifier description, leak-test cost), §7 (process-wide-state paragraph
replaced; `processState` named; construction-time reasoning), both DESIGN.md — all match the code.
`.github/workflows/build.yml:39-60`: actionlint v1.7.12
(`go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12 build.yml ci.yml release.yml`) → no
output, exit 0. The step extracted verbatim and run under Windows PowerShell with
`$ErrorActionPreference = 'Stop'` (GitHub's pwsh prelude), `PVDKIT_LLVM_DIR` = the local Build
Tools LLVM, `RUNNER_TEMP` = scratchpad:

```
clang version 19.1.5 ... InstalledDir: C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin
MSVC toolsets under C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools: 14.44.35207
MSVC toolset resolved by clang-cl: 14.44.35207
LASTEXITCODE=0
```

pwsh 7 passes arguments to `cmd.exe` in Legacy mode (as 5.1 does), so the `cmd /c "... 2>&1"`
quoting behaves identically there; `Get-ChildItem -ErrorAction SilentlyContinue` overrides the
`Stop` preference; a missing MSVC directory ends in the explicit `throw`.

(5) Report vs reality: every file:line claim spot-checked (Nit 5 lists the slips); the symbol
dumps, import tables, coverage rows, test counts, timing figures, the lint verdict and the "what
only CI can prove" list all reproduce.

Gates run (`PVDKIT_BUILD_SUFFIX=-review`, `--parallel 6`, one build or lint at a time):

```
cmake --preset debug && cmake --build --preset debug --parallel 6      exit 0, 0 warnings
ctest --preset debug                                                   100% tests passed out of 17 (guard_tests 6.89 s, avif_leak_tests 50.58 s, rpgmvp_leak_tests 25.61 s)
cmake --preset release && cmake --build --preset release --parallel 6  exit 0, 0 warnings
ctest --preset release                                                 100% tests passed out of 21 (avif_check_imports, avif_check_exports, rpgmvp_check_imports, rpgmvp_check_exports passed; avif_leak_tests 22.91 s)
cmake --preset release-x86 && cmake --build --preset release-x86 --parallel 6   exit 0, 0 warnings
ctest --preset release-x86                                             100% tests passed out of 21 (both _check_imports / _check_exports passed; avif_leak_tests 33.11 s)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/coverage.ps1 -Preset coverage   (CMAKE_BUILD_PARALLEL_LEVEL=6)
  100% tests passed out of 17
  plugins\avif\src\DefaultPlugin.cpp        8 0 100.00%   7 0 100.00%   24 0 100.00%   0 0 -
  plugins\rpgmvp\src\DefaultPlugin.cpp      8 0 100.00%   7 0 100.00%   22 0 100.00%   0 0 -
  src\core\CodecPlugin.cpp                 21 0 100.00%   3 0 100.00%   33 0 100.00%   8 0 100.00%
  src\core\FileSession.cpp                 84 0 100.00%  11 0 100.00%  105 0 100.00%  48 0 100.00%
  src\core\colour\Pipeline.cpp            108 0 100.00%  20 0 100.00%  176 0 100.00%  66 0 100.00%
  src\pvd\Exports.cpp                      42 0 100.00%  18 0 100.00%   90 0 100.00%  14 0 100.00%
  TOTAL                                  1093 0 100.00% 261 0 100.00% 2147 0 100.00% 696 0 100.00%
  Coverage gate passed: lines 100%, branches 100%.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/lint.ps1 -Jobs 6   (debug-review / release-review)
  clang-format: 0 finding(s) in 0.6 s
  clang-tidy: 0 finding(s) in 306.0 s
  cppcheck: 0 finding(s) in 1.4 s
  PSScriptAnalyzer: 0 finding(s) in 4.4 s
  BinSkim: 0 finding(s) in 1.0 s
  lint: clean
core_tests.exe --test-case="diagnostic: exhaustive*,Presentation release timing: *,..." --no-skip=true   (release-review)
  exhaustive [0, 1]: 1065353217 floats, 0 mismatches, 0 exact-path monotonicity violations
  SrgbOutputTables construction (once per plugin instance, in pvdInit): best 5.2395 ms, mean 5.38608 ms over 10 constructions
  Presentation(P3/PQ 1000 nit) construction over borrowed tables: best 1.7443 ms, mean 1.75933 ms over 10 constructions
guard_tests.exe (release-review)                                       test cases: 21 | 21 passed; assertions: 565 | 565 passed
GuardProbe.exe -tc="PROBE*" (scratchpad, includes tests/guard/GuardTests.cpp)   test cases: 5 | 5 passed; assertions: 93 | 93 passed
llvm-nm / llvm-readobj over build/release-review and build/release-x86-review   as quoted above
go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12 .github/workflows/{build,ci,release}.yml   exit 0, no findings
```

Not run: `debug-x86`, `asan`, `coverage-x86`, lint x86 (the report's figures for those are
consistent with everything above and the changed code has no architecture- or sanitizer-specific
path). Not provable here, as the task says: the import table on MSVC 14.51.

## Verdict

`REJECT` — one substantive finding (the `atomic_flag_*` hole in the guard), a one-line regex fix
plus samples; everything else in the task is verified as claimed.
