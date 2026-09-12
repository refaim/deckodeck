Implemented and staged all 6 substantive fixes and nits 1–11. No required review item remains unaddressed; nit 12 was explicitly a process note.

Key files include [overlay port](/C:/Users/Roma/Dev/PictureView3/avif/ports/libavif/portfile.cmake), [guard tests](/C:/Users/Roma/Dev/PictureView3/avif/tests/guard/GuardTests.cpp), [coverage gate](/C:/Users/Roma/Dev/PictureView3/avif/scripts/coverage.ps1), [fixture generator](/C:/Users/Roma/Dev/PictureView3/avif/scripts/make-synthetic-fixtures.ps1), and [Ninja discovery](/C:/Users/Roma/Dev/PictureView3/avif/cmake/find-ninja.cmake).

### Changes

- Added libavif 1.4.2#2 overlay port. Only `port-version` and patch registration differ from upstream.
- Patched libavif’s clang-cl static-library merge to use the MSVC branch and `llvm-lib /OUT:`.
- Removed the global archiver/rules override and deleted `clang-cl-rules.cmake`.
- Rewrote the guard scanner with boundary regexes, case-insensitive quoted/angle includes, safe apostrophe handling, and amended core layering.
- Generated a real 100/200/300 ms animation.
- Made garbage generation deterministic.
- Added and verified a 20-file SHA-256 manifest.
- Made the adapters target conditionally `STATIC`.
- Made coverage discover executables through CTest and verify source-file presence.
- Addressed flags, doctest scope, unused globs, README suffix, and Ninja discovery nits.

### TDD evidence

Guard red run:

```text
ctest: 0/1 passed, 1 failed
[doctest] test cases:  9 |  6 passed |  3 failed
[doctest] assertions: 82 | 70 passed | 12 failed
```

Final guard self-test:

```text
[doctest] test cases:  9 |  9 passed | 0 failed | 0 skipped
[doctest] assertions: 94 | 94 passed | 0 failed
[doctest] Status: SUCCESS!
```

The old coverage gate incorrectly passed with temporary `src/core/Untested.cpp`:

```text
TOTAL ... Lines 100.00% ... Branches 100.00%
Coverage gate passed: lines 100%, branches 100%.
```

After implementing completeness checking, the same temporary file produced:

```text
Coverage source completeness failed. Missing files:
  src/core/Untested.cpp
```

The file was then deleted. Final result:

```text
Coverage objects discovered from CTest: 4
Coverage source completeness passed: 2 executable source files present.
TOTAL ... Lines 100.00% ... Branches 100.00%
Coverage gate passed: lines 100%, branches 100%.
```

### Build and tests

Commands:

```powershell
$env:AVIFPVD_BUILD_SUFFIX='-fix1'
rtk cmake --preset debug
rtk cmake --build --preset debug
rtk ctest --preset debug

rtk cmake --preset release
rtk cmake --build --preset release
rtk ctest --preset release
```

Final results:

```text
Debug:  ctest: 4/4 passed
Release: ctest: 4/4 passed
```

No compiler warnings were emitted. Fresh vcpkg installation rebuilt doctest, dav1d, libyuv, libjpeg-turbo, and overlay libavif; four host/helper packages were restored from cache. Vcpkg did emit non-build warnings because it could not write some binary-cache archives, but every package installed successfully.

### Overlay/archive proof

```text
build\debug-fix1\CMakeCache.txt:
CMAKE_AR:FILEPATH=.../llvm-lib.exe
VCPKG_OVERLAY_PORTS:STRING=C:/Users/Roma/Dev/PictureView3/avif/ports
```

Installed artifacts:

```text
avif.lib    511638 bytes
dav1d.lib  3504828 bytes
yuv.lib    1204538 bytes
doctestConfig.cmake 203 bytes
```

Libavif build log:

```text
BUNDLE_TOOL:FILEPATH=.../llvm-lib.exe
CMAKE_AR:FILEPATH=.../llvm-lib.exe
... llvm-lib.exe /NOLOGO /OUT:.../avif.lib .../avif_internal.lib
```

Two forced core relinks, checked independently:

```text
Relink 1 - llvm-ar t:
CMakeFiles/avifpvd_core.dir/Error.cpp.obj
  count=1
Relink 1 - llvm-lib /list:
CMakeFiles/avifpvd_core.dir/Error.cpp.obj
  count=1

Relink 2 - llvm-ar t:
CMakeFiles/avifpvd_core.dir/Error.cpp.obj
  count=1
Relink 2 - llvm-lib /list:
CMakeFiles/avifpvd_core.dir/Error.cpp.obj
  count=1
```

### Animation proof

FFmpeg exposes the primary item as `v:0`, so the three-frame sequence track is `v:1`.

```text
stream_index=1|pts_time=0.000000|duration_time=0.100000
stream_index=1|pts_time=0.100000|duration_time=0.200000
stream_index=1|pts_time=0.300000|duration_time=0.300000

width=64
height=64
pix_fmt=gbrp
duration=0.600000
nb_frames=3
nb_read_frames=3
```

Decoded colors:

```text
frame 0 first RGB: 255,0,0
frame 1 first RGB: 0,255,0
frame 2 first RGB: 0,0,255
```

Repeated generation was byte-stable:

```text
garbage.bin stable: True
anim_3frames.avif stable: True
```

### Import policy

Command:

```powershell
./scripts/check-imports.ps1 build/release-fix1/tests/adapters/adapter_tests.exe
```

Result:

```text
Import {
  Name: KERNEL32.dll
}
Import policy passed: KERNEL32.dll is the only imported module.
```

The adapter test executable was used as requested; Task 1 does not yet define the Task 2 `AVIF.pvd` shared target.

### Fixture integrity and staging

```text
Verified SHA-256 for 20 pinned files at libavif commit
66663952a677bb8a13ea1530d5694775d7d143d4.
```

`git diff --cached --check` passed:

```text
ok 75 files changed, 2967 insertions(+)
```

All Task 1 files are staged. No commit was created. Existing untracked `AGENTS.md`, `CLAUDE.md`, and `docs/` files were intentionally left untouched.