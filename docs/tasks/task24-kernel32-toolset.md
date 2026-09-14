# Task 24 — keep the plugins KERNEL32-only under the newest MSVC toolset

Repository: `C:\Users\Roma\Dev\PictureView3\pvdkit` (product deckodeck, `https://github.com/refaim/deckodeck`,
remote `origin` configured — **do not push, do not commit/stage/reset**; the orchestrator commits and
pushes). HEAD `3fa65a4`, tree clean. Windows, C++23, `/MT`, x64 + x86, static plugins for
PictureView 3 / Far Manager. Read completely first: `AGENTS.md`, `docs/ARCHITECTURE.md` (§2 composition
root and lifetimes, §3.7 presentation, §7 process-wide state), `src/core/colour/Pipeline.{hpp,cpp}`,
`src/core/FileSession.{hpp,cpp}`, `src/core/CodecPlugin.{hpp,cpp}`, `src/pvd/Exports.cpp`,
`plugins/*/src/DefaultPlugin.cpp`, `tests/guard/*` (the forbidden-token guard),
`scripts/check-imports.ps1`, `docs/tasks/report-task20.md` and `review-task20*.md` (why the tables are
shared), `.github/workflows/build.yml`.

## The problem (observed, not hypothetical)

The first CI run on GitHub (`windows-latest` = Windows Server 2025 image with **Visual Studio 2026
Enterprise, MSVC 14.51.36231, clang-cl 22.1.3**) built and passed every test except the import gates:

```
Import {
  Name: api-ms-win-core-synch-l1-2-0.dll
  Symbol: WaitOnAddress (56)
  Symbol: WakeByAddressAll (58)
}
Import policy violation. Expected only KERNEL32.dll; found: api-ms-win-core-synch-l1-2-0.dll, KERNEL32.dll
```

for **both** `AVIF.pvd` and `RPGMVP.pvd`, x64 and x86. On Roma's machine (MSVC 14.44, clang-cl 19.1.5)
the same sources import only `KERNEL32.dll`. The newer vcruntime/STL no longer loads `WaitOnAddress`
dynamically with a fallback; it imports it directly from the Windows 8+ API set. Candidates in our
code that reach that pair (`WaitOnAddress` + `WakeByAddressAll`, nothing else from the API set):

1. the guarded function-local `static const SrgbOutputTables` in `Pipeline.cpp` (`_Init_thread_wait`
   / `_Init_thread_notify` in the new vcruntime use WaitOnAddress/WakeByAddressAll) — RPGMVP links
   `Pipeline.cpp.obj` too because `FileSession` uses `Presentation`;
2. `std::jthread` in `Presentation::applyImage` — its `stop_token` state uses `atomic::wait`/`notify_all`
   in the new STL, which map to the same pair;
3. anything else from the same family: `atomic<>::wait/notify_*`, `<latch>`, `<barrier>`,
   `<semaphore>`, `std::call_once`/`once_flag`, `std::stop_token`, `std::condition_variable`
   (check what the 14.5x STL does), thread-safe statics anywhere in `src/**` and `plugins/*/src/**`.

We cannot reproduce locally (only MSVC 14.44 is installed; do not install anything). The proof will be
the next CI run, which the orchestrator triggers. Your job is to remove every candidate so that the
DLLs are KERNEL32-only regardless of toolset, without changing behaviour or output.

## Changes

### 1. No process-wide dynamic-initialised state
Replace the function-local static: the `SrgbOutputTables` are built **once per plugin instance by the
composition root** and injected by `const&` into `FileSession` → `Presentation` (this was option (a) of
`review-task20.md` and fits ARCHITECTURE §2: "injected by reference, outlives its users by
construction"). Constraints: no function-local statics with dynamic initialisation and no namespace-
scope objects with non-trivial constructors/destructors anywhere in `src/**`/`plugins/*/src/**`
(no `_Init_thread_*`, no `atexit` entries in a plugin DLL); build the tables at most once per plugin
object (at construction or lazily on first HDR session — your choice, but no double-checked locking,
no atomics, no mutex: the host calls the plugin from one thread at a time per ARCHITECTURE; if you
build lazily, say in the doc why it is safe); table contents identical (the 65,536-point grid test,
the exhaustive skipped diagnostic and both whole-image FNV hashes in `plugins/avif/tests/e2e/E2eTests.cpp`
must pass unchanged); no heap-free requirement any more — a `std::unique_ptr<const SrgbOutputTables>`
member is fine. Update ARCHITECTURE §3.7/§7 (there is no process-wide state any more) and
`plugins/avif/DESIGN.md`. TDD: adjust/add tests first (sharing across sessions of one plugin,
lifetime), then the code. Keep the construction-time timing case working.

### 2. `std::thread` instead of `std::jthread`
Band workers become `std::thread` with an exception-safe join (a small RAII joiner that joins in its
destructor, or a `std::vector<std::thread>` guarded by a scope guard — no `catch (` outside the
firewall, the guard forbids it). No `stop_token`, no `request_stop`. Behaviour identical (last band on
the calling thread, joins on all paths including unwinding). Tests unchanged in spirit.

### 3. Sweep and guard
Grep `src/**` and `plugins/*/src/**` for the whole family listed above; remove any other use. Then
extend the forbidden-token guard (`tests/guard`) for `src/**` and `plugins/*/src/**` with the tokens
that would reintroduce the import: `jthread`, `stop_token`, `stop_source`, `call_once`, `once_flag`,
`<latch>`, `<barrier>`, `<semaphore>`, `condition_variable`, `.wait(` / `.notify_one(` / `.notify_all(`
on atomics (choose a robust spelling; tests must prove the guard catches each token), and a check
that no function-local `static` with a non-constexpr initialiser exists (if that is hard to detect
textually, at least forbid `static const` / `static ` followed by a type at function scope in those
directories — think about false positives such as `static constexpr` and `static_assert`, which must
stay allowed). Each token gets a one-line reason in the guard's list ("MSVC ≥ 14.50 imports
api-ms-win-core-synch-l1-2-0.dll for this").

### 4. Documentation and CI
- `AGENTS.md`: add the rule to the coding rules ("no thread-safe statics, no jthread/stop_token, no
  atomic wait/notify, no latch/barrier/semaphore/call_once/condition_variable in plugin code — the
  newest MSVC runtime imports the Windows 8+ synch API set for them; use std::thread + join, SRWLOCK-
  backed std::mutex if a lock is ever needed, and composition-root ownership instead of statics").
- `docs/ARCHITECTURE.md`: the same in the relevant sections; remove the "single piece of process-wide
  state" paragraph.
- `.github/workflows/build.yml`: print the MSVC toolset version (`VCToolsVersion` from the vcvars
  environment or the directory name under `VC\Tools\MSVC`) and the clang version at the start of the
  build step so future failures are attributable at a glance.

## Gates (all green before you report)
`PVDKIT_BUILD_SUFFIX=-t24`, sequentially, `--parallel 6`, one build at a time, never x64 and x86
concurrently: `debug`, `release`, `debug-x86`, `release-x86`, `asan` (configure, build, ctest —
`guard_tests` and both `_check_imports`/`_check_exports` included); `scripts/coverage.ps1 -Preset
coverage` and `-Preset coverage-x86` (100/100); `scripts/lint.ps1 -Jobs 6` x64 and
`-BuildDir build\debug-x86-t24 -ReleaseDir build\release-x86-t24` x86 (`lint: clean`). `pwsh` is not
on PATH — `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\<name>.ps1`. Zero warnings.
Additionally dump the release DLLs' undefined/imported symbols locally (`llvm-readobj --coff-imports`)
and, for the objects, `llvm-nm --undefined-only` on `Pipeline.cpp.obj` and `FileSession.cpp.obj` to
show that `_Init_thread_*` and any `__std_atomic_wait*` / `_Thrd_*`-stop symbols are gone; quote it.
Known: rare `leakcheck_tests` x86 flake (re-run once), ASan LoadLibrary mapped-view noise.

## Report
`docs/tasks/report-task24.md`: what changed (file:line), TDD evidence, the symbol dumps, the guard
tokens and their tests, gate outputs, and an explicit list of what only the CI run on MSVC 14.51 can
prove. Final message: a short summary.
