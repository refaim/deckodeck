# Task 11 — libFuzzer harnesses for every plugin (x64, ASan), bounded runs, reproducers as tests

Goal: coverage-guided fuzzing of **our** code paths — the PVD marshalling (`pvd::Shim`), the
session/pixel-buffer arithmetic, the AVIF transform kernel (clap/irot/imir), the RPGMVP 32-byte
substitution and CRC, the R↔B swaps, and our handling of every library return code — on the
exact entry surface the DLL exports use. The codec libraries themselves (dav1d, libavif, libspng,
zlib) are on OSS-Fuzz and are built by vcpkg without instrumentation; they are not the target,
they are the environment. Roma's threat model: a crash in a `.pvd` is a crash of Far Manager
(in-process), so "never crash, never hang, never leak on any input" is the property.

## Deliverables
1. **Preset `fuzz`** (x64 only) in `CMakePresets.json`: inherits the `asan` preset's flags
   (`-fsanitize=address`, RelWithDebInfo, /MT) and adds `-fsanitize=fuzzer-no-link` for every
   target plus `-fsanitize=fuzzer` on the fuzz executables (clang-cl 19: check how the driver
   passes `clang_rt.fuzzer-x86_64.lib` to lld-link; the repository already resolves the ASan
   runtime libraries by path in the top-level `CMakeLists.txt` — follow the same pattern for the
   fuzzer runtime, `PVDKIT_FUZZ=ON`). The fuzz executables are not registered with ctest; the
   preset must still build every existing target and pass `ctest --preset fuzz` (the normal tests
   with fuzzer coverage instrumentation on) — or, if libFuzzer's instrumentation makes the normal
   tests impractically slow, restrict `-fsanitize=fuzzer-no-link` to the libraries the fuzz
   executables link and say so in the report.
2. **Shared driver** `tests/support/fuzz/FuzzDriver.{hpp,cpp}` (library `pvdkit_fuzzdriver`):
   `int fuzzOneInput(pvd::Shim &shim, std::span<const std::byte> input)` runs, in memory mode
   (`lFileSize == 0`, `pBuf` = the input, `cbBuf` = its size), the host sequence
   `init → pluginInfo → fileOpen → (pageInfo → pageDecode → pageFree) for the first
   `min(pageCount, 4)` pages → fileClose → exit`, with a decode callback that aborts on a
   deterministic subset of calls (derived from a hash of the input, so a given input always
   behaves the same). Every host struct the shim writes is checked for the invariants the real
   host relies on (non-null pixel pointer, pitch ≥ width × bpp, page count ≥ 1 on success,
   strings NUL-terminated within the returned buffers) — a violated invariant is `abort()`,
   i.e. a finding. The driver is unit-tested with doctest (`fuzzdriver_tests`: runs every
   fixture of both plugins through it, asserts the sequence completes and is deterministic).
3. **`makePlugin(const core::DecoderOptions &)`** overload in `src/pvd/PluginFactory.hpp`,
   implemented by every plugin's `DefaultPlugin.cpp` (`makePlugin()` delegates with the plugin's
   defaults; tests in both plugins' composition tests). The fuzz executables use it with
   fuzzing limits: `maxPixels = 4 Mi`, `maxDimension = 8192`, `maxThreads = 1`, so a mutated
   IHDR/ispe cannot spend the run on a 1 GiB allocation (the size-limit paths keep their unit
   tests at production values).
4. **Fuzz executables**, one per plugin, `plugins/<id>/tests/fuzz/FuzzCodec.cpp` →
   `<id>_fuzz`: `LLVMFuzzerTestOneInput` builds the plugin once (static), wraps it in a
   `pvd::Shim`, calls `fuzzOneInput`. Registered by a new `pvdkit_add_plugin_fuzz(id)` in
   `cmake/pvdkit-plugin.cmake`, built only when `PVDKIT_FUZZ`.
   - **AVIF structure-aware mutator**: `LLVMFuzzerCustomMutator` in `plugins/avif/tests/fuzz/`
     that, with probability ½, finds a `clap`, `irot`, `imir`, `ispe` or `pixi` box by its 4CC in
     the input and mutates its payload fields (clap numerators/denominators incl. 0 and
     0xFFFFFFFF and negative-as-unsigned, irot angle 0..255, imir axis, ispe width/height), else
     delegates to `LLVMFuzzerMutate`. Unit-tested (a doctest that the mutator finds and changes
     the box in a known fixture and never grows past `maxSize`).
   - RPGMVP: default mutator is enough (the 32-byte header is fully covered by the corpus).
5. **`scripts/fuzz.ps1`** `-Plugin <id> [-Minutes 30] [-Jobs 1] [-Regress]`: configures/builds
   the `fuzz` preset if needed (`--parallel 6`), seeds `build/fuzz/corpus/<id>/` from
   `plugins/<id>/fixtures/**` (every file, negatives included) and, if present, from
   `plugins/<id>/fixtures/crashes/`, runs `<id>_fuzz` at BelowNormal priority with
   `-max_len=262144 -timeout=10 -rss_limit_mb=2048 -max_total_time=<minutes*60>
   -artifact_prefix=build/fuzz/artifacts/<id>/ -print_final_stats=1`, one worker by default
   (CPU etiquette), and prints a summary: executions, execs/s, corpus size, cov/ft counters,
   artifacts found. `-Regress` runs the executable over `plugins/<id>/fixtures/crashes/*` only
   (no fuzzing) and exits non-zero on any finding — this is what `package.ps1` calls, after the
   asan block. `build/fuzz/` is gitignored.
6. **The runs**: 30 minutes per plugin, one after the other, one worker each. For every artifact
   (crash / timeout / OOM / ASan report): minimise it (`-minimize_crash=1`), find the root cause,
   write the failing regression test first (the reproducer goes to
   `plugins/<id>/fixtures/crashes/<short-name>.<ext>` with a line in `SOURCES.md` and an e2e /
   adapter assertion of the correct outcome), then fix. A finding inside a vcpkg library is
   reported with the stack, not fixed (note whether our limits could have prevented it). Rerun
   the affected plugin for 10 minutes after the fix.
7. Docs: `docs/ARCHITECTURE.md` §5 gets a "Fuzzing" paragraph (what is instrumented, what is
   not, how to run, what `-Regress` guards); `AGENTS.md` DoD gains "fuzz regression corpus
   green"; `README.md` one line.

## Rules
As `AGENTS.md` plus the Addendum in `docs/tasks/task8-rpgmvp.md` (lint gate, leak gate, CPU
etiquette). `PVDKIT_BUILD_SUFFIX=-t11`. Every gate stays green on x64 and x86 (the fuzz code is
x64-only but must not break the x86 configure). Coverage 100/100 stays for `src/**` and
`plugins/*/src/**` (the `makePlugin` overload included). No commits. Fuzzing itself never runs
more than one worker, never in parallel with a build or lint, and never longer than the minutes
above — Roma's machine is a desktop, not a fuzz farm. Do not run Far, do not touch
`C:\Tools\FarManager`.

## Report — `docs/tasks/report-task11.md`
What was built; the exact fuzz invocations with their final stats lines per plugin (execs,
execs/s, cov, ft, corpus, artifacts); every finding with minimised reproducer, root cause, the
regression test and the fix (or the reason it is a library finding); the gate outputs (ctest
all presets, coverage both archs, lint both archs, `fuzz.ps1 -Regress` both plugins); known
limitations (uninstrumented libraries, x64 only, what the mutator does not know about).
