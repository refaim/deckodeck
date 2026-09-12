# Review of Task 1 (Opus) — verdict REJECT

## Substantive findings

1. **`cmake/clang-cl-rules.cmake:2-3` — `llvm-ar qc` never truncates the target, so static archives accumulate stale and duplicate members.** CMake's normal GNU-`ar` handling uses `CMAKE_<LANG>_ARCHIVE_CREATE`, for which the Ninja generator emits a `cmake -E rm -f <TARGET>` before `ar qc`. By overriding `CMAKE_<LANG>_CREATE_STATIC_LIBRARY` instead, that delete is gone. The generated rule is literally `llvm-ar.exe qc $TARGET_FILE $LINK_FLAGS $in` and `q` is quick-append. Reproduced: after two forced relinks of `avifpvd_core` the archive contains `Error.cpp.obj` three times; after deleting `b.cpp` and rebuilding in an isolated project, `b.cpp.obj` and its symbol are still in the archive. With `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` renaming a source is normal, so this will silently link dead code or produce duplicate-symbol failures.

2. **The global `CMAKE_AR=llvm-ar` + rules override is the wrong lever for the libavif problem; an overlay port is the targeted fix.** Confirmed: libavif 1.4.2 `cmake/Modules/merge_static_libs.cmake:96` tests `CMAKE_C_COMPILER_ID MATCHES "^(Clang|GNU|Intel|IntelLLVM)$"` before `elseif(MSVC)` at line 116, so clang-cl takes the `${CMAKE_AR} -M <script.ar` MRI path. The archive output of the workaround is fine (real COFF archives, lld-link and link.exe both consume them, dav1d unaffected), but the blast radius is every CMake port plus all our own targets, and it introduces finding 1. An overlay port patching `merge_static_libs.cmake` to test `MSVC` before the Clang branch lets `CMAKE_AR` stay at clang-cl's default `llvm-lib`, keeps CMake's truncating `/OUT:` archive rule, and lets `CMAKE_STATIC_LINKER_FLAGS` keep `-machine:x64` instead of being force-cleared (`cmake/clang-cl.toolchain.cmake:14-18`).

3. **`tests/guard/GuardTests.cpp:107-142` — the guard has false negatives on the most common spellings of the very constructs rules 2–5 forbid.** Driven directly:
   ```
   delete[] p;                        flagged=false
   new\n  Foo();                      flagged=false
   new\tFoo();                        flagged=false
   catch(...) no space                flagged=false
   #include <Windows.h> capital W     flagged=false
   #include "windows.h" quotes        flagged=false
   std::free (p)                      flagged=false
   digit separator 100'000 then delete p;   flagged=false
   baseline delete p; / malloc        flagged=true
   ```
   `stripCommentsAndLiterals` (`GuardTests.cpp:39-41`, `:67-76`) treats any `'` in code as a character-literal opener, so a single C++14 digit separator (e.g. `268'435'456`) or any lone apostrophe blanks the remainder of the file and silently disables the guard for that file. Fix: match on token boundaries with a regex (`\bdelete\s*(\[\s*\])?\b`, `\bnew\b`, `\bcatch\s*\(`), lowercase the code as well as the path for the include checks (and accept the `"..."` include form), and make the `'` handling digit-separator-aware (only enter char-literal state when the previous non-space char is not alphanumeric/`_`).

4. **`scripts/make-synthetic-fixtures.ps1:51-58` and `tests/fixtures/SOURCES.md:40-42,49` — `anim_3frames.avif` has 100/100/100 ms, not the contract's 100/200/300 ms.** ARCHITECTURE §6 specifies three distinct durations precisely so the future `frameTiming(p)`/`PageInfo::frameTimeMs` tests can prove the right frame's duration is returned; with three identical durations a decoder that always returns frame 0's timing passes. Verified with `ffprobe -select_streams v:1 -show_frames`: 0.100000 / 0.100000 / 0.100000. Nothing in AGENTS.md or ARCHITECTURE.md permits a "constant 100 ms fallback". Produce variable sample durations or get an explicit contract amendment.

5. **`src/adapters/CMakeLists.txt:3-6` — `avifpvd_adapters` is an INTERFACE library; ARCHITECTURE §4 specifies a static library.** `target_sources(avifpvd_adapters INTERFACE ...)` means every consumer recompiles the adapter TUs into itself rather than linking one archive. `tests/e2e/CMakeLists.txt:3` already shows the empty-glob guard pattern; apply it here and keep the target STATIC.

6. **`scripts/coverage.ps1:41-56,82-84` — the gate cannot detect a `src/**` file that is in no test binary.** The numeric side is sound (`99.9 -ne 100` throws), but `llvm-cov` only reports files it finds in the linked objects, so a source compiled into nothing yields no row and TOTAL stays 100%. The hard-coded four-executable list also excludes `e2e_tests`. Add an assertion that every `src/**/*.cpp` appears in the coverage export's file list before checking percentages.

## Nits

1. `CMakeLists.txt:20-21` — the justification comment for `-Wno-unused-command-line-argument` doesn't name the flag. It is `/Zc:preprocessor`. Name it; consider suppressing only where needed.
2. `CMakeLists.txt:23` — `$<$<CONFIG:Release>:/Zi>` combined with `/DEBUG:NONE` produces compile-time PDBs that are discarded; `/O2` also appears twice in the release FLAGS line.
3. `src/core/Error.hpp:8` — `static_assert(__cplusplus == 202302L)` is an extra constraint; `>=` (line 7) already satisfies rule 1 and `==` will fail spuriously on a future toolchain.
4. `src/core/IDecoder.hpp:10` — `core` including `pvd/Types.hpp` — the contract (ARCHITECTURE §2) has since been clarified: `pvd/Types.hpp` and `pvd/Plugin.hpp` are shared boundary contracts that core may include. No code change needed; the guard must encode the clarified rule (core must not include `pvd/Shim.hpp`, `pvd/ContextHandle.hpp`, `pvd/Firewall.hpp`, `pvd/PluginFactory.hpp`).
5. `scripts/make-synthetic-fixtures.ps1:75-82` — `garbage.bin` is regenerated from a CSPRNG on every run, so re-running dirties a committed fixture. Use a fixed seed (deterministic PRNG) or skip if present.
6. `scripts/fetch-fixtures.ps1` — no checksum verification; add a SHA-256 manifest and verify.
7. `scripts/coverage.ps1:43` — `$matches` shadows PowerShell's automatic `$Matches`; rename. `$env:LLVM_PROFILE_FILE` leaks into the caller's session; restore it.
8. `CMakePresets.json:19,26` — hard-coded `ninja-1.13.2-windows`; discover Ninja (PATH, then vcpkg `downloads/tools/ninja-*`) with the pinned path as fallback.
9. `src/CMakeLists.txt:1`, `tests/CMakeLists.txt:1`, `tests/fixtures/CMakeLists.txt:1` — unused glob variables.
10. `CMakeLists.txt:36` — `find_package(doctest CONFIG REQUIRED)` at top level even with `BUILD_TESTING=OFF`; move under tests.
11. `README.md:30` — path ignores `AVIFPVD_BUILD_SUFFIX`.
12. TDD ordering unverifiable without commits (process note, no code change).

## Verified by the reviewer (for reference)
Debug and release: 4/4 tests pass, zero warnings; coverage 100/100 on the three src files; import policy passes on the release adapter binary (KERNEL32.dll only); SDK header byte-equal after decoding; fixture spot checks match SOURCES.md; every dependency built with -MT/-MTd (dav1d meson b_vscrt=mt/mtd); libyuv has 234 AVX2 + 201 SSSE3 + 116 SSE2 + 8 AVX512BW symbols under clang-cl.
