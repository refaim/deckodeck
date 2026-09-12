Task 1 is complete and 69 deliverable files are staged. No product decoder logic or shared `AVIF.pvd` target was added.

Key deliverables include [CMakePresets.json](C:/Users/Roma/Dev/PictureView3/avif/CMakePresets.json), [canonical headers](C:/Users/Roma/Dev/PictureView3/avif/src/core/Error.hpp), [guard tests](C:/Users/Roma/Dev/PictureView3/avif/tests/guard/GuardTests.cpp), [fixture documentation](C:/Users/Roma/Dev/PictureView3/avif/tests/fixtures/SOURCES.md), and [build scripts](C:/Users/Roma/Dev/PictureView3/avif/scripts/coverage.ps1).

### Verification

Commands run:

```powershell
rtk cmake --preset debug
rtk cmake --build --preset debug
rtk proxy ctest --preset debug

rtk cmake --preset release
rtk cmake --build --preset release
rtk proxy ctest --preset release

rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\coverage.ps1
```

Debug:

```text
100% tests passed out of 4
Total Test time (real) = 0.45 sec
```

Release:

```text
100% tests passed out of 4
Total Test time (real) = 0.44 sec
```

Both builds completed with zero warnings. The custom `x64-windows-static-clang` triplet successfully built dav1d, libyuv, and libavif with clang-cl and `/MT[d]`; no stock-triplet fallback was used.

Coverage:

```text
Filename             Regions Miss Cover     Functions Miss Cover     Lines Miss Cover     Branches Miss Cover
core\Error.cpp            13    0 100.00%           1    0 100.00%      27    0 100.00%          24    0 100.00%
pvd\Progress.cpp           5    0 100.00%           2    0 100.00%       4    0 100.00%           4    0 100.00%
pvd\Types.hpp              1    0 100.00%           1    0 100.00%       1    0 100.00%           0    0       -
TOTAL                     19    0 100.00%           4    0 100.00%      32    0 100.00%          28    0 100.00%
```

### Import checks

Command:

```powershell
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-imports.ps1 -Path ..\plugin\WebP.pvd
```

Expected result, exit code 1:

```text
Name: KERNEL32.dll
Name: msvcrt.dll
Import policy violation. Expected only KERNEL32.dll; found: KERNEL32.dll, msvcrt.dll
```

The same script passed on the statically linked Release adapter test executable:

```text
Import policy passed: KERNEL32.dll is the only imported module.
```

### Fixtures

Downloaded files are pinned to libavif commit `66663952a677bb8a13ea1530d5694775d7d143d4`. No files were dropped.

```text
abc_color_irot_alpha_irot.avif                 10597
abc_color_irot_alpha_NOirot.avif               10596
alpha_noispe.avif                               1403
alpha_steps.avif                                 519
anim_3frames.avif                               2353
clap_irot_imir_non_essential.avif                653
clop_irot_imor.avif                              653
color_grid_alpha_nogrid.avif                    2373
colors_hdr_rec2020.avif                        24065
colors_sdr_srgb.avif                           18845
colors-animated-12bpc-keyframes-0-2-3.avif      2267
colors-animated-8bpc.avif                       1235
colors-animated-8bpc-alpha-exif-xmp.avif        7506
cosmos1650_yuv444_10bpc_p3pq.avif              37451
draw_points_idat_progressive.avif                600
extended_pixi.avif                               330
garbage.bin                                     4096
gray_400.avif                                    370
kodim03_yuv420_8bpc.avif                       25430
not_avif.bmp                                   12342
not_avif.png                                     213
paris_icc_exif_xmp.avif                        21132
quad_rgb_lossless.avif                           350
quad_yuv420.avif                                 352
sofa_grid1x5_420.avif                          25409
tenbit_444.avif                                  419
truncated.avif                                   211
weld_sato_12B_8B_q0.avif                       38100
white_1x1.avif                                   305
TOTAL: 250175 bytes across 29 binary fixtures
```

The SDK copy was decoded and compared after removing the added provenance line:

```text
SDK decoded-content equality after removing provenance line: True
```

### Intentional omissions and limitations

- `AVIF.pvd` was not produced or checked because Task 2 explicitly owns `Exports.cpp` and the shared-library target. The exact TODO and all eight exports are present.
- `anim_3frames.avif` uses the permitted constant 100 ms fallback.
- ffprobe 9.0.1 cannot parse `draw_points_idat_progressive.avif`; its exact “Not yet implemented” result is documented.
- `git diff --cached --check` passed. Pre-existing untracked `AGENTS.md`, `CLAUDE.md`, and `docs/` were preserved and not staged.