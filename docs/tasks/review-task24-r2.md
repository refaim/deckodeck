# Review: Task 24 fix round 1 (branch `task24-kernel32`, commit `28013f2` on `1a1fd55` on `3fa65a4`)

Reviewer build suffix: `-review`. Fresh reviewer; everything below was run on this machine
(clang-cl 19.1.5, MSVC 14.44.35207, clang-tidy 19). The clang-tidy 22 verdict and the import table
on MSVC 14.51 are CI's to prove, as the brief says. Scope: only what the round touched (the guard
regexes and their samples, rule 13 wording, the LLVM discovery fallback, the clang-tidy 22 handling
and the report's round section); the round-1 verification of the tables' ownership, the joiner and
the scope classifier is not repeated.

## Substantive findings

None.

## Nits

1. `docs/ARCHITECTURE.md:878-879` (§7) and `tests/guard/GuardTests.cpp:312` — "`<syncstream>`
   [is] built on atomic wait/notify in the STL headers" / "`<syncstream>`'s locked pointer is an
   atomic wait" is not what this STL does. In MSVC 14.44 `<syncstream>` locks through
   `__std_acquire_shared_mutex_for_instance` plus `scoped_lock` (`syncstream:90-91, :163`; no
   `_Locked_pointer`, which lives in `<atomic>`, `<memory>` and `<stop_token>`), and libcpmt's
   `syncstream.obj` imports `_Smtx_lock_exclusive`/`_Smtx_unlock_exclusive` (SRWLOCK) — no
   `__std_atomic_wait_direct` (`llvm-nm libcpmt.lib`, quoted below). `<latch>`, `<barrier>`,
   `<semaphore>` and `stop_token` are atomic-wait based as stated. The prohibition is fine on its
   other leg (a newer toolset's imports cannot be proven here, nothing needs it); move
   `<syncstream>` to that sentence so the "observed vs by extension" split the round was asked
   for stays accurate.
2. `tests/guard/GuardTests.cpp:313-316` — the `<syncstream>` rule
   `::[ \t]*(?:basic_)?(?:osyncstream|syncbuf)\b` misses the wide aliases `std::wosyncstream` /
   `std::wsyncbuf` (declared in `<iosfwd>:266-267`, defined only in `<syncstream>`), probe
   `flagged=false` for both. Not a practical hole: the types are incomplete without
   `#include <syncstream>`, which the same rule catches, and only a quoted `#include "syncstream"`
   (blanked by the literal stripper, like every quoted include for the `forbiddenEverywhere`
   rules) would get past it. `(?:basic_|w)?` closes it in one token. Same family: `::[ \t]*`
   (unlike `\s*` in the `.wait(` rule) does not span a newline between `::` and the name
   (`std\n::\nfuture<int>` passes) — identical to the pre-existing latch/barrier/semaphore rules,
   and clang-format would never leave that shape.
3. Commit `28013f2`'s message says "the integer-induction rewrite of the threshold descent". The
   code (`src/core/colour/Pipeline.cpp:91-97`) is a `while` over the same float `previous`, and the
   report says so correctly ("no loop counter at all"); the brief for this review carried the
   commit message's wording forward. Cosmetic, but the message is what `git log` keeps.
4. `docs/ARCHITECTURE.md:783-784` (§5) — "Release is close to it (unloaded Release x64 ≈ 23 s,
   Release x86 ≈ 33 s ...)": 33 s is over the 30 s budget, not close to it. Say Release x86 misses
   it too (the round-1 nit asked for the budget status to be stated plainly).
5. `tests/guard/CMakeLists.txt:3` still says "(AGENTS.md rules 2-4)"; the Layout table in
   `AGENTS.md:136` was fixed to "2–4 and 13", this comment was not.
6. `docs/tasks/report-task24.md:535` (nit 5's own corrections) — "the polarity loop at
   `:678-711`": that test case is at `tests/guard/GuardTests.cpp:707-764` (`:678-711` is
   `fakeIndex()` and "scanner strips comments"). And `:573-574` "every creation site ... `:230,
   :243, :266, :271, :294, :316, :336`" mixes `tree.file()` lines (229, 242, 266, 270, 293, 315,
   336) with `writeBytes` lines. Cosmetic.
7. `scripts/llvm-dir.ps1:34-35` — the VS 2022 `Program Files\...\2022\{Enterprise,Professional,
   Community}` layouts, previously hard-coded, now come from `ProgramFiles`, which a 32-bit
   PowerShell host reports as `C:\Program Files (x86)`; there the any-VS glob covers the same
   wrong root and those installs are found only through PATH. Not a configuration this project
   runs (64-bit pwsh on the runners, 64-bit Windows PowerShell here); `ProgramW6432` as the 64-bit
   root under WOW64 would keep the old behaviour. `find-llvm.cmake` is unaffected (cmake.exe is
   64-bit).

## Verified

Scope files read line by line: `tests/guard/GuardTests.cpp` (rules `:270-316`, `scan` `:592-628`,
the polarity loop `:707-764`, "every spelling" `:1045-1163`), `src/core/colour/Pipeline.cpp:71-97`,
`scripts/llvm-dir.ps1`, `cmake/find-llvm.cmake`, `.clang-tidy`, `tests/.clang-tidy` and both plugin
copies (`diff`: identical), `tests/adapters/WinAdapterTests.cpp` (`TempTree` `:42-102`, every
creation site), `tests/support/HostileCorpus.{hpp,cpp}`, `tests/e2e/PluginHost.cpp:21-34`,
`.github/actions/toolchain/action.yml`, `README.md:44-58, :185-199`, AGENTS.md rule 13 / toolchain
paragraph / layout table / definition of done, ARCHITECTURE §4 (lint, LLVM order), §5 (token list,
leak cost), §7. `git diff master..task24-kernel32` (34 files) and `git show HEAD` (18 files);
`git diff --check` clean; working tree clean, branch `task24-kernel32` left as found; every edited
text file valid UTF-8, the two scripts ASCII.

(1) Guard. `\batomic_(?:flag_)?(?:wait|wait_explicit|notify_one|notify_all)\b` catches the four
`atomic_flag_*` spellings and leaves `atomic_flag_test`, `_test_and_set`, `_clear`,
`_test_explicit` and `std::atomic_flag` alone; the timed-mutex, `<future>` and `<syncstream>` rules
match after `::` or on the angle-bracket include, so `timed_mutexes`, `int future`, `promise_type`,
`future_error`, `async_io`, `futures`, `osyncstreamer`, `shared_lock`, `shared_mutex`,
`recursive_mutex`, `this_thread::sleep_for` are not tokens. Sample counts as the report says: the
polarity loop 41 (12 + 23 + 6), "every spelling" 57 flagged / 37 allowed, each sample also checked
commented-out and inside a string literal. Nothing under `src/**` or `plugins/*/src/**` contains
`future|promise|async|syncbuf|syncstream|timed_mutex|atomic_flag|packaged_task` (grep). Adversarial
probe TU (scratchpad `r2-GuardProbe.cpp`, `#include`s the repository's `GuardTests.cpp` unchanged,
compiled with clang-cl against vcpkg's doctest):

```
r2-GuardProbe.exe -tc="PROBE*"      test cases: 4 | 3 passed | 1 failed; assertions: 37 | 36 passed | 1 failed
  flagged: STD::ATOMIC_FLAG_WAIT, atomic_flag_wait_explicit(..., memory_order_acquire), using std::atomic_flag_notify_one,
           std::launch::async, std::experimental::future, #include<future>, #\tinclude\t<future>, #include <syncstream> // x,
           shared_timed_mutex + shared_lock, struct S : std::timed_mutex, vector<promise<void>>, packaged_task<int()>{fn},
           basic_osyncstream<wchar_t>, ::future<int>, atomic_flag_notify_all (&flag)
  the one failure: "std\n::\nfuture<int> r;"  (nit 2)
  allowed (17/17 clean): atomic_flag_test/_test_and_set/_clear/_test_explicit, shared_lock, sleep_for, timed_mutex_count,
           my_timed_mutex, promise_type, coroutine_handle<promise_type>, future_error, async_io, futures.push_back,
           osyncstreamer, mutex+scoped_lock, atomic<bool> store/exchange, thread join/joinable
  informational, flagged=false: std::wosyncstream, std::wsyncbuf, #include "future", #include "syncstream",
           using namespace std; future<int>   (the last is documented in §5)
  master Pipeline.cpp -> exactly 2 violations (jthread, function-local static); HEAD Pipeline.cpp -> clean
```

STL facts behind nit 1 (`MSVC/14.44.35207/include`, `lib/x64/libcpmt.lib`):

```
latch:70      _Counter.wait(_Current, memory_order_relaxed)          barrier:126/142  _Current.wait(...)
semaphore:34  __std_atomic_wait_get_deadline(...)                    stop_token:71    _Locked_pointer<_Stop_callback_base> _Callbacks;
mutex:695-696 (timed_mutex)  mutex _My_mutex; condition_variable _My_cond;   shared_mutex:202  condition_variable _Read_queue{};
future:20,404 #include <condition_variable> ... condition_variable _Cond;
syncstream:90-91  _Mutex = __std_acquire_shared_mutex_for_instance(_Wrapped);   :163  scoped_lock _Guard(*_Get_mutex());
llvm-nm libcpmt.lib, syncstream.obj undefined:  _Smtx_lock_exclusive  _Smtx_unlock_exclusive  __std_calloc_crt  __std_free_crt  atexit  (no __std_atomic_wait_*)
iosfwd:266-267    using wsyncbuf = basic_syncbuf<wchar_t>;  using wosyncstream = basic_osyncstream<wchar_t>;
```

(2) Docs. AGENTS.md rule 13 and ARCHITECTURE §7 now separate the two observed users
(`_Init_thread_*`, `__std_atomic_wait_direct` via `stop_token`) from the family forbidden by
extension, and name the `atomic_flag_*` twins, the timed mutexes, `<future>` and `<syncstream>`;
§5's token list matches the regexes (including "`basic_` forms"); the Layout table says "rules 2–4
and 13"; §4 and AGENTS.md's toolchain paragraph give the same four-step LLVM order as the two
files; the README's two paragraphs no longer contradict each other; the action comment describes
both branches of its own lookup. §5's leak-cost sentence labels unloaded vs loaded figures (nit 4
on one word of it). Timings here, on a machine shared with other agents but with no other build:
Debug x64 `avif_leak_tests` 43.88 s, Release x64 21.11 s.

(3) LLVM discovery. Fake trees under the scratchpad (`r2-fake-pf86\...\2022\BuildTools`,
`r2-fake-pf\...\18\{Community,Enterprise}`, empty roots, a fake `clang-cl.exe` for PATH), the real
Build Tools present throughout. `scripts/llvm-dir.ps1`, dot-sourced, both roots redirected
in-process, PATH stripped of every directory holding a clang-cl:

```
1 nothing set (real machine)                    -> C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin
2 PVDKIT_LLVM_DIR set but wrong                 -> THROW: PVDKIT_LLVM_DIR=C:\definitely\not\here does not contain clang-cl.exe
2b -Requested wrong                             -> THROW: LLVM directory C:\definitely\not\here does not contain clang-cl.exe
2c PVDKIT_LLVM_DIR set and right (fake)         -> <scratchpad>\r2-fake-path
3a fake: 2022 BuildTools (x86 root) AND 18/*    -> <scratchpad>\r2-fake-pf86\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin
3b fake: only 18/Community + 18/Enterprise      -> <scratchpad>\r2-fake-pf\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin
3c fake: 18/* under the (x86) root only         -> <scratchpad>\r2-fake-pf\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin
4 fake roots with no VS, clang-cl not on PATH   -> THROW: clang-cl.exe was not found: set PVDKIT_LLVM_DIR to the LLVM bin directory of a Visual Studio installation (VC\Tools\Llvm\x64\bin) or put clang-cl on PATH
5 fake roots with no VS, fake clang-cl on PATH  -> <scratchpad>\r2-fake-path
6 both roots unset, fake clang-cl on PATH       -> <scratchpad>\r2-fake-path
7 both roots unset, nothing on PATH             -> THROW: clang-cl.exe was not found ...
8 both roots unset, PVDKIT_LLVM_DIR wrong       -> THROW: PVDKIT_LLVM_DIR=C:\definitely\not\here does not contain clang-cl.exe
```

`cmake/find-llvm.cmake` (`cmake -P` over a two-line script that includes it; only
`ProgramFiles(x86)` can be redirected for a child process — reproduced: `cmd /c echo %ProgramFiles%`
prints `C:\Program Files` under an overridden parent — and the real `C:\Program Files` holds no
Visual Studio here):

```
1 nothing set                                   -> exit=0 PVDKIT_LLVM_DIR=C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin
2 PVDKIT_LLVM_DIR (env) set but wrong           -> exit=1 PVDKIT_LLVM_DIR=C:\definitely\not\here (environment) does not contain clang-cl.exe
2c PVDKIT_LLVM_DIR set and right (fake)         -> exit=0 <scratchpad>/r2-fake-path
3a fake (x86) root: 2022 BuildTools present     -> exit=0 <scratchpad>/r2-fake-pf86/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin
3b fake (x86) root: only 18/Community + 18/Ent. -> exit=0 <scratchpad>/r2-fake-pf/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin
4 fake (x86) root, no VS, nothing on PATH       -> exit=1 clang-cl.exe was not found: set PVDKIT_LLVM_DIR (environment) to the LLVM ...
5 fake (x86) root, no VS, fake clang-cl on PATH -> exit=0 <scratchpad>/r2-fake-path
6 (x86) root unset, fake clang-cl on PATH       -> exit=0 <scratchpad>/r2-fake-path
7 (x86) root unset, nothing on PATH             -> exit=1 clang-cl.exe was not found ...
8 (x86) root unset, PVDKIT_LLVM_DIR wrong       -> exit=1 ... (environment) does not contain clang-cl.exe
-DPVDKIT_LLVM_DIR=C:/definitely/not/here        -> CMake Error at cmake/find-llvm.cmake:23: does not contain clang-cl.exe
```

The order is the documented one in both files, Roma's Build Tools stay first, set-but-wrong stays
loud. The env-derived VS 2022 list also works inside vcpkg's cleaned port environment: `vcpkg env`
shows `PROGRAMFILES` and `ProgramFiles(x86)` kept, and this review's `cmake --preset debug` ran
vcpkg's `detect_compiler` through the chainload toolchain (`buildtrees/detect_compiler/config-x64-windows-static-clang-rel-CMakeCache.txt.log`,
09:07:48): `PVDKIT_LLVM_DIR:PATH=C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin`.
(The ports themselves were "already installed": vcpkg hashes the chainload toolchain file, not the
file it `include`s, so `find-llvm.cmake` alone rebuilds nothing.) Sort order under both roots is
the same in `list(SORT)` and culture-aware `Sort-Object` (`(x86)` first) for the shapes tried.

(4) clang-tidy 22 handling. Root `.clang-tidy:23` disables `portability-avoid-pragma-once` with
the reason in its header comment; `tests/.clang-tidy:24-25` and the two identical plugin copies
disable `bugprone-random-generator-seed` and `bugprone-bitwise-pointer-cast` with reasons that
match the code (`HostileCorpusTests.cpp:62` `mt19937 rng{42}`, `:97` `rng{7}`,
`LeakScenarios.cpp:60` `kCorpusSeed = 20260912`; `PluginHost.cpp` `std::bit_cast` is the only
pointer `bit_cast` in the tree, none under `src/**`). Scoping: clang-tidy takes the configuration
of the TU's main file, and every relaxed TU (`tests/**`, `plugins/*/tests/**`) sits below one of
the three files; no `src/**` TU does. 39 + 1 + 2 + 1 + 2 = 45, the CI count. The descent loop
(`Pipeline.cpp:91-97`) walks the identical `nextafter`/`exactQuantize` sequence as the former
`for` (init, test, body, step); `bugprone-float-loop-counter` matches `forStmt` only. Byte
identity on the Release build:

```
core_tests --test-case="linear input quantization ...,the shared quantizer answers NaN*"   test cases: 2 | 2 passed; assertions: 262154 | 262154 passed
core_tests --test-case="diagnostic: exhaustive*" --no-skip=true
  exhaustive [0, 1]: 1065353217 floats, 0 mismatches, 0 exact-path monotonicity violations   (against exactSrgbCode, the pow path)
avif_e2e_tests --test-case="HDR AVIF is presented as 64-bit sRGB through the DLL" --success=true
  actualHash := 14703790622216699421   actualHash := 5389512495027945087   (the two pinned x64 hashes, E2eTests.cpp:440, :448)
  test cases: 1 | 1 passed; assertions: 58 | 58 passed
```

`~TempTree` and `~HostileCorpus` call only `std::filesystem::remove(path, error_code&)` (noexcept
by [fs.op.remove] and in this STL), `vector::size()`/`operator[]`/iterators (noexcept in the MSVC
STL) and an `error_code`; `remove_all` remains in the constructors. Every path the tests create
goes through `tree.file()` (`:229, :242, :266, :270, :293, :315, :336`; `createLongDirectory()` records
its five levels; an absolute `relative` replaces the root in `path_ / relative`, so the long-path
file is recorded once, in extended form), and the reverse walk removes the file before the five
directories before the root. After `adapter_tests` (`13 | 13 passed`, `2387 | 2387 passed`, run
under ctest and once more directly), both leak tests, the coverage run and the lint run, `%TEMP%`
holds no `pvdkit-adapter-*`, `pvdkit-hostile-*` or current-pid `pvdkit-huge-*` directory (only the
pre-existing `pvdkit-huge-104364` of 2026-09-12).

(5) Report vs reality: the regexes, labels, sample counts, test counts (21 / 659), the Pipeline.cpp
coverage row, the coverage total, the lint verdict, the probe answers, the `%TEMP%` check, the
`detect_compiler` fact and the "not re-run" list all reproduce; the slips are nits 3 and 6.

Gates run (`PVDKIT_BUILD_SUFFIX=-review`, `--parallel 6`, one build or lint at a time, x64 only —
the round's diff has no architecture- or sanitizer-specific path; round 1 covered x86):

```
cmake --preset debug && cmake --build --preset debug --parallel 6          exit 0, 0 warnings, vcpkg: packages already installed
ctest --preset debug                                                       100% tests passed out of 17 (guard_tests 6.94 s, avif_leak_tests 43.88 s, rpgmvp_leak_tests 24.59 s)
guard_tests.exe (debug-review)                                             test cases: 21 | 21 passed; assertions: 659 | 659 passed
cmake --preset release && cmake --build --preset release --parallel 6      exit 0, 0 warnings
ctest --preset release                                                     100% tests passed out of 21 (avif_check_imports, avif_check_exports, rpgmvp_check_imports, rpgmvp_check_exports passed; avif_leak_tests 21.11 s)
powershell ... scripts/coverage.ps1 -Preset coverage   (CMAKE_BUILD_PARALLEL_LEVEL=6)
  100% tests passed out of 17; plugin profile check passed for avif and rpgmvp (18/18 Exports.cpp functions each)
  plugins\avif\src\DefaultPlugin.cpp      8 0 100.00%   7 0 100.00%   24 0 100.00%   0 0 -
  plugins\rpgmvp\src\DefaultPlugin.cpp    8 0 100.00%   7 0 100.00%   22 0 100.00%   0 0 -
  src\core\CodecPlugin.cpp               21 0 100.00%   3 0 100.00%   33 0 100.00%   8 0 100.00%
  src\core\FileSession.cpp               84 0 100.00%  11 0 100.00%  105 0 100.00%  48 0 100.00%
  src\core\colour\Pipeline.cpp          107 0 100.00%  20 0 100.00%  177 0 100.00%  66 0 100.00%
  src\pvd\Exports.cpp                    42 0 100.00%  18 0 100.00%   90 0 100.00%  14 0 100.00%
  TOTAL                                1092 0 100.00% 261 0 100.00% 2148 0 100.00% 696 0 100.00%
  Coverage gate passed: lines 100%, branches 100%.
powershell ... scripts/lint.ps1 -Jobs 6   (debug-review / release-review)
  clang-format: 0 finding(s) in 0.6 s
  clang-tidy: 0 finding(s) in 311.0 s
  cppcheck: 0 finding(s) in 1.4 s
  PSScriptAnalyzer: 0 finding(s) in 4.4 s
  BinSkim: 0 finding(s) in 1.0 s
  lint: clean
core_tests / avif_e2e_tests (release-review)                               as quoted under (4)
r2-GuardProbe.exe, llvm-dir.ps1 / find-llvm.cmake probes, vcpkg env         as quoted under (1) and (3)
```

Not run: `debug-x86`, `release-x86`, `asan`, `coverage-x86`, lint x86. Not provable here: clang-tidy
22 on the five handled checks and the import table on MSVC 14.51 (CI run 34901531321 already
proved the latter green per the report; this review did not re-derive that).

## Verdict

`ACCEPT` — the substantive finding of round 1 is closed (the `atomic_flag_*` twins are caught, with
samples), the family extension is tested in both polarities, the discovery fallback behaves as
documented under every probe, the tables and both image hashes are byte-identical, the test
destructors are non-throwing and still clean up, and every gate that can run on this machine is
green. The seven nits are wording, a commit message, a stale comment, line references and two
regex touch-ups that no code in the tree needs.
