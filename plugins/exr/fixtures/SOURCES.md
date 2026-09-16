# Fixture sources and verified expectations

Every file is committed so builds are offline; the directory stays below 3 MB (2.1 MB today).
Two origins: the openexr-images corpus, pinned to one commit and re-downloadable with
`scripts/fetch-fixtures.ps1` against `scripts/openexr-images.sha256`, and synthetic files that
`scripts/make-synthetic-fixtures.ps1` regenerates byte for byte (OpenEXR 3.4.13 Python binding
through `uv`, pinned versions). The SHA-256 columns attest to the bytes in this repository.

Expectations were established independently of the plugin: the header facts and the raw
scene-linear samples come from the OpenEXR Python binding (`make_synthetic_fixtures.py` writes them
to `tests/support/ReferencePixels.hpp` for every accepted fixture, corpus files included, at seven
positions each), the 99.99th-percentile peak is computed there in double precision from every
display-window pixel, and `tests/support/ReferencePipeline.hpp` turns those into the expected PQ
codes (adapter tests, within one code: float against double at a code boundary) and host sRGB
codes (e2e tests) with its own double-precision implementation of ST 2084, the primaries matrix
(the column form, Bradford-adapted to D65 unless the white is E), BT.2390 and the sRGB OETF.
Luminance/chroma files are pinned against the C++ library's own `Imf::RgbaInputFile` instead
(every pixel, worst difference one 16-bit code). A chromaticities attribute the plugin cannot use
(a white with y = 0, collinear primaries) is Rec.709 in the reference too, as it is in the plugin.

## Pinned openexr-images files

Repository [`AcademySoftwareFoundation/openexr-images`](https://github.com/AcademySoftwareFoundation/openexr-images)
at commit [`e38ffb0790f62f05a6f083a6fa4cac150b3b7452`](https://github.com/AcademySoftwareFoundation/openexr-images/commit/e38ffb0790f62f05a6f083a6fa4cac150b3b7452),
license BSD-3-Clause (the repository's `LICENSE`: Copyright (c) 2004, Industrial Light & Magic;
the files carry `owner` attributes naming ILM where set).

| File | Origin | Bytes | Exercises | Verified expectation | SHA-256 |
|---|---|---:|---|---|---|
| `t01.exr` | [`DisplayWindow/t01.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/DisplayWindow/t01.exr) | 20,829 | display and data window identical | 400x300 half RGB PIZ; display 400x300 at 0,0; peak 100 nit (SDR); accepted | `adc51278b0ceaa3475f58a9abc5cb5dec79c4590b9db773009e6ffcfdfd81643` |
| `t02.exr` | [`DisplayWindow/t02.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/DisplayWindow/t02.exr) | 20,829 | display window shifted by one pixel | 400x300 data; display 400x300 at 1,1; the last row and column are black | `0ef9bf41902af9ae8cc981068cd5948cb9629bc65c4943ae5b6daec4fcdcd0e1` |
| `t05.exr` | [`DisplayWindow/t05.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/DisplayWindow/t05.exr) | 20,829 | display window inside the data window | display 340x260 at 30,20 inside data 400x300 at 0,0: cropped | `ab242bfa21826af2cba883c0257866cfe2300e5c97b7e6687d0e8dc3dc4851dc` |
| `t07.exr` | [`DisplayWindow/t07.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/DisplayWindow/t07.exr) | 20,829 | display window larger than the data window | display 481x371 at -40,-40 around data 400x300 at 0,0: 40-pixel black border | `11310a48a95814b197ae3306da280930d6f6b47b650a0ab48375f1201b6a914e` |
| `t09.exr` | [`DisplayWindow/t09.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/DisplayWindow/t09.exr) | 20,829 | disjoint windows | display 200x300 at 400,0, data 400x300 at 0,0: the whole page is opaque black | `4b57630afbe015efd01998857f033dd26236be60a042827faba7afa2c853390c` |
| `t13.exr` | [`DisplayWindow/t13.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/DisplayWindow/t13.exr) | 20,829 | one pixel in common (lower right of the data) | display 101x101 at 399,299: the data pixel lands at the upper left | `e779baaed0563955213f99daf07f903727f9100c3b6ea342302d0f0a32607d2a` |
| `t14.exr` | [`DisplayWindow/t14.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/DisplayWindow/t14.exr) | 20,829 | one pixel in common (upper left of the data) | display 101x101 at -100,-100: the data pixel lands at the lower right | `35c8089aab3a1d6a6feba00b51d8d4ce90ad67fc1eff68750167c00c5bc89597` |
| `AllHalfValues.exr` | [`TestImages/AllHalfValues.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/TestImages/AllHalfValues.exr) | 70,769 | every half value incl. NaN, +/-inf, denormals, negatives | 256x256 half RGB PIZ; sanitizing: NaN/negative -> 0, +inf -> 10000 nit; peak 10000 nit | `eede573a0b59b79f21de15ee9d3b7649d58d8f2a8e7787ea34f192db3b3c84a4` |
| `BrightRingsNanInf.exr` | [`TestImages/BrightRingsNanInf.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/TestImages/BrightRingsNanInf.exr) | 151,163 | values over 1000 with NaN and inf pixels near the centre | 800x800 half RGB ZIP; peak 10000 nit; the 4-thread e2e case decodes it | `1d32ea056eda34c6dad50a770ca4c262875056fe47e429f90a3f1d2736570bb6` |
| `GammaChart.exr` | [`TestImages/GammaChart.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/TestImages/GammaChart.exr) | 23,272 | PXR24 (lossy 24-bit float) compression | 800x800 half RGB PXR24; peak 100 nit | `bb2819850542afae5e2208ba39724c23fe1a07b80b96f4fd093b3c9a3ca8ac2f` |
| `GrayRampsHorizontal.exr` | [`TestImages/GrayRampsHorizontal.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/TestImages/GrayRampsHorizontal.exr) | 18,296 | Y-only (luminance) channel | 800x800 half Y PXR24; shown as grey; peak 1800 nit | `7858e712af500e737ebaedbf40730646529e6093371ddb939becc58700b8fc6a` |
| `WideColorGamut.exr` | [`TestImages/WideColorGamut.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/TestImages/WideColorGamut.exr) | 172,826 | chromaticities attribute equal to Rec.709; negative RGB (out-of-gamut colours) | 800x800 half RGB ZIP; classified Rec.709 (code 1); negatives clamp to 0 | `7140e9262ff518215e610fc5d4584ae470e50f8b0e7f21eb403f5b1684a33101` |
| `WideFloatRange.exr` | [`TestImages/WideFloatRange.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/TestImages/WideFloatRange.exr) | 70,935 | a single FLOAT channel named G spanning +/-1e38 | 500x500 float G PXR24; shown as grey ("float G as grey"); peak 10000 nit | `7b038f9f43e0a69b779f1ffb519a50b910d13717d7b0aab8adb2328682e6673f` |
| `stripes.exr` | [`TestImages/stripes.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/TestImages/stripes.exr) | 4,657 | RGBA with alpha | 100x50 half RGBA PIZ; straight alpha out | `a4c11b79d256fb6945087dc9b244e814264a128aeb2fefacc303c32d08e426a1` |
| `Garden.exr` | [`LuminanceChroma/Garden.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/LuminanceChroma/Garden.exr) | 399,046 | tiled Y-only file (one level, 128x128 tiles) with a preview attribute | 874x493 half Y PIZ tiled; "half Y, ..., tiled 128x128"; peak 877 nit | `c19060f8252ec7cce66979c04c883766d70343ca1916ccc538d02eea982cfc66` |
| `ColorCodedLevels.exr` | [`MultiResolution/ColorCodedLevels.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/MultiResolution/ColorCodedLevels.exr) | 53,298 | mip-mapped tiled RGBA, PXR24 | 512x512 half RGBA, 64x64 tiles, 10 mip levels: level 0 is shown | `ba836f7d37ea268ef9e96d7f0e28d035f4f197be12356133676282c578ada643` |
| `PeriodicPattern.exr` | [`MultiResolution/PeriodicPattern.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/MultiResolution/PeriodicPattern.exr) | 228,020 | mip-mapped tiled RGB with partial edge tiles | 517x517 half RGB ZIP, 64x64 tiles, 10 mip levels | `8f5a8d17703a4c61f0a25f436eafb11f0042e836f269cdb63c6d63804f1992f9` |
| `Rec709_YC.exr` | [`Chromaticities/Rec709_YC.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/Chromaticities/Rec709_YC.exr) | 378,570 | luminance/chroma (Y, RY, BY subsampled 2x2), Rec.709 | 610x406 half Y+chroma PIZ; reconstructed with the library's RgbaYca filters; the adapter test pins every pixel against Imf::RgbaInputFile | `dd7355a2c2a694bae041500af4f61ae96bdc02a6e9474fa53d4e4ccef83c8774` |
| `XYZ_YC.exr` | [`Chromaticities/XYZ_YC.exr`](https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/e38ffb0790f62f05a6f083a6fa4cac150b3b7452/Chromaticities/XYZ_YC.exr) | 364,055 | luminance/chroma with the CIE XYZ chromaticities (primaries at the XYZ axes, white E) - the same picture as Rec709_YC.exr in absolute tristimulus | 610x406 half Y+chroma PIZ; "linear CIE XYZ"; the XYZ values pass through XYZ -> BT.709 without adaptation (the white E declares none), so the e2e test pins its presented pixels to Rec709_YC.exr within a small tolerance; the adapter test holds its reconstruction within one code of Imf::RgbaInputFile | `7d0b9b7e6f19a698ff733539de15760a07f6771a1a4f5c1e545ac03d7a4ce293` |

## Synthetic files

Generated by `scripts/make-synthetic-fixtures.ps1` (`make_synthetic_fixtures.py` with
`OpenEXR==3.4.13` and `numpy==2.3.3`), dedicated to CC0-1.0. The pictures are formulas: a horizontal
ramp in R, a vertical one in G, a diagonal one in B, plus one highlight block, so the tone-mapping
peak sits above 100 nit; the values recorded in `ReferencePixels.hpp` are what the library decodes
back, so lossy schemes (PXR24, B44, DWA, HTJ2K) are measured rather than assumed.

| File | Bytes | What it exercises / expectation | SHA-256 |
|---|---:|---|---|
| `rgb_half_zip.exr` | 585 | 16x8 RGB half, ZIP compression, ramp with a 6.0 highlight (peak above 100 nit) | `6791037624dd3bd531abcf0b0a46cf2026c2cb41f8b5ef2b1184fab6224884b1` |
| `rgb_half_none.exr` | 1,238 | 16x8 RGB half, NONE compression, ramp with a 6.0 highlight (peak above 100 nit) | `82a1c8c9d30a58d8a9b482c8a0ef2d21520ccfd396b610424f8cf2ec0427287b` |
| `rgb_half_rle.exr` | 1,038 | 16x8 RGB half, RLE compression, ramp with a 6.0 highlight (peak above 100 nit) | `1704e1e3e5b18b0908d338cab92117f4651a183c6deda7eb753283f58ef84c2e` |
| `rgb_half_zips.exr` | 965 | 16x8 RGB half, ZIPS compression, ramp with a 6.0 highlight (peak above 100 nit) | `954dfeec0f5d6f534cdbfc35a2a4f38f7175831a6f01fed96b42d8dc5c3b411c` |
| `rgb_half_piz.exr` | 1,126 | 16x8 RGB half, PIZ compression, ramp with a 6.0 highlight (peak above 100 nit) | `ef2b8638a35c7824274cfef86857ba0061bffb9a0bb80e6468e562ba0b4d3f9e` |
| `rgb_half_pxr24.exr` | 563 | 16x8 RGB half, PXR24 compression, ramp with a 6.0 highlight (peak above 100 nit) | `2d996ef4258345e49bd4af2e9d44df8ebe1e0a3b2228a97bf905f4d4ab0e9589` |
| `rgb_half_b44.exr` | 694 | 16x8 RGB half, B44 compression, ramp with a 6.0 highlight (peak above 100 nit) | `677ab951642b7b25879bdf58a27b5af76acf86673a60e2204fe838148e6637e1` |
| `rgb_half_b44a.exr` | 694 | 16x8 RGB half, B44A compression, ramp with a 6.0 highlight (peak above 100 nit) | `2ed2914cda0c58706d7afeffaaaa82a27949663bfc52ae9cee5f3960d528c089` |
| `rgb_half_dwaa.exr` | 1,126 | 16x8 RGB half, DWAA compression, ramp with a 6.0 highlight (peak above 100 nit) | `09993db958ea73881dd353c41d32bb2315e8b1d94f0b02502c87924c75e03971` |
| `rgb_half_dwab.exr` | 1,126 | 16x8 RGB half, DWAB compression, ramp with a 6.0 highlight (peak above 100 nit) | `298fe4a37302d8bfa4de408d595d976d439bd060c0d6925eca049127b1cc6747` |
| `rgb_half_htj2k256.exr` | 1,126 | 16x8 RGB half, HTJ2K256 compression, ramp with a 6.0 highlight (peak above 100 nit) | `f56137dc0d586d33b1744c1581db1694d7f809354113fecd743284919a03041c` |
| `rgb_half_htj2k32.exr` | 1,126 | 16x8 RGB half, HTJ2K32 compression, ramp with a 6.0 highlight (peak above 100 nit) | `a19798126902490e9a75ec4d23950cefd410a5d85b472258f8bdbad4def87306` |
| `rgba_premultiplied.exr` | 637 | 16x8 RGBA half, ZIP; alpha bands 0 / 0.5 / 1 across x, colour premultiplied by alpha | `89649b2039bb3ee4518debccd5a17dec1fc049b04799860c160cc497b37b5183` |
| `y_float_zips.exr` | 898 | 16x8 Y only, FLOAT, ZIPS; horizontal ramp 0..2.5 | `1f79f4b0be656dfbacf6cc551dd3acc75130ea2aeb5b89fe8725d131fc0a8c75` |
| `layer_rgb.exr` | 1,176 | 16x8, no top-level colour: layers aaa (Z only), beauty (RGBA half + UINT id) and depth (RGB); beauty is the first complete one | `6f8c30e5a99f5eb0699e572c077e6a0310c9c98291f973c9b676e680ced45e25` |
| `uint_only.exr` | 604 | 16x8 with one UINT channel: nothing displayable, refused | `02a9a589bb6b6ce5d6b654aa3e126f0cb02202ad96353da086606319ed02bde8` |
| `rgb_float_mixed.exr` | 678 | 16x8 RGBA with mixed types (R and A FLOAT, G and B HALF), ZIP | `cb6e2c8ebe52845d53fa6a556ecb03fddbab031e483eb29bb1db71b161a79a53` |
| `data_inside_display.exr` | 549 | display 16x12 at 0,0; data 8x6 at 4,3 with alpha: transparent black around the data | `d89dfe9b56453e2ce96f499fdc745beb05ac02ee57e27be85423e83ae2613cd0` |
| `data_larger_than_display.exr` | 643 | display 8x6 at 2,2 inside data 16x12: the data is cropped | `97cb4e6e148010ba9c34b8ea424155ca35695f725f406bec8d091c0517b04a74` |
| `data_offset_partial.exr` | 493 | display 8x8 at 0,0; data 8x8 at -3,-2: partial overlap, opaque black elsewhere | `f9ac59a0150c1e06749cd4bbacb4494f4eff0518fb7b5d53889b2168c47f5cef` |
| `data_disjoint.exr` | 527 | display 8x8 at 0,0; data 8x8 at 20,20 with alpha: no overlap, the page is transparent black | `129378412bf920b84c9c2d100a2df6c3c1163bbfe294e7846de6d0489dff06e1` |
| `decreasing_y.exr` | 585 | 16x8 RGB half, ZIP, lineOrder DECREASING_Y (same pixels as rgb_half_zip.exr) | `acc49837409487ec955620f1b14a235887a48afff83a002b3da79d98e99ecfd0` |
| `white_luminance_250.exr` | 614 | 16x8 RGB half, whiteLuminance 250: 1.0 is 250 nit | `f8e1cf6038b5766fb104a3738c2f061bff7775a474b5b04726bb2b972ac2e352` |
| `chroma_ap0.exr` | 651 | 16x8 RGB half with chromaticities (0.7347, 0.2653, 0.0, 1.0, 0.0001, -0.077, 0.32168, 0.33767) | `4986004ab7b24b65d149479f2294d8dd3f8646647b49b48ef1f6dc72b4c1f78c` |
| `chroma_ap1.exr` | 651 | 16x8 RGB half with chromaticities (0.713, 0.293, 0.165, 0.83, 0.128, 0.044, 0.32168, 0.33767) | `a50b6842212c44d8c899bbeb182278a5414707d07112318ee9dbecf7325e4c44` |
| `chroma_rec2020.exr` | 651 | 16x8 RGB half with chromaticities (0.708, 0.292, 0.17, 0.797, 0.131, 0.046, 0.3127, 0.329) | `281a29195f552035a3bd7ae52118c63ff294f3e9ce4c1238d38500d7e8f5fcee` |
| `chroma_p3d65.exr` | 651 | 16x8 RGB half with chromaticities (0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.3127, 0.329) | `424ea2e6c4636de492983e8c9d3a7d2b3df3c81f39f3cbf037e2328b55552f7a` |
| `chroma_custom.exr` | 651 | 16x8 RGB half with chromaticities (0.7347, 0.2653, 0.1596, 0.8404, 0.0366, 0.0001, 0.3457, 0.3585) | `0c9665a09a3c8e5354246405dd0901e3bbde7d179cae3bfab9a629a8a5ffa2ad` |
| `chroma_xyz.exr` | 823 | 16x8 RGB half holding the CIE XYZ (D65) of the rgb_half_zip.exr ramp, chromaticities = CIE XYZ (primaries at the axes, white E): presented like rgb_half_zip.exr, no adaptation | `e7bfde86f3829ba16f06a66c1e0ce67ea531d0e02f8b0dd9209a89233eede7a3` |
| `rgb_whitey0.exr` | 651 | 16x8 RGB half (the rgb_half_zip.exr ramp) with chromaticities whose white has y = 0: unusable, shown as Rec.709 with a note | `efa3e123bdc2dea74eb1b86202ba9239621d27dcdcff245ed34ba19bb99cd36a` |
| `interop_lin_ap1.exr` | 618 | 16x8 RGB half, colorInteropID lin_ap1 and no chromaticities: AP1 by the fallback | `fbbb26720a609e0a49c939c4b88ba2a00684334df9a4c1df4eb3605e5d063cf4` |
| `interop_unknown.exr` | 623 | 16x8 RGB half, colorInteropID srgb_texture (not a linear id): Rec.709, the id is only reported | `e7d2fbe130fe8d3c57c9b5579fd88db22444d156d30b6cac0dc2186fc7ebcd3b` |
| `peak_above_10000.exr` | 584 | 16x8 RGB half with a 500.0 highlight (50000 nit): the peak clamps to 10000 nit | `a92d52fea4cb03f155418d0bef3e05cbf828f2a055dab0b0cdeb37d7f5d65fa9` |
| `nan_inf_negative.exr` | 664 | 16x8 RGB FLOAT, ZIP; row 0 carries NaN (R at x=0, G at x=4), +inf (G at x=1, B at x=5), -inf (B at x=2) and -0.5 (R at x=3) | `e9d2180014308f5340005f4589caa57ac9ef987b0ad0a44f96d379c3258e3bb3` |
| `huge_display.exr` | 382 | display 40000x40000 with a 2x2 data window: refused, the side exceeds the 32768 limit | `c95948b8b1854199dba4352cff19b9930744d097c2a88098eae2ba17cd61d410` |
| `display_over_maxpixels.exr` | 382 | display 20000x20000 (400 Mpx) with a 2x2 data window: refused, the area exceeds 16384x16384 | `59fc4eb9a93e881349d6b0bca60ea66800b7e5f4f71a8f1a96d28bb69188c71e` |
| `tile_too_large.exr` | 419 | 2x2 RGB in a single 4097x4097 tile: refused, the tile side exceeds the 4096 limit the plugin sets on the Core | `bd156c2359570ce8818fbd54511be5f42c7846650f9e7681878235ed29f880c8` |
| `truncated.exr` | 351 | the first 60 % of rgb_half_zip.exr: the header parses, the chunk data is missing | `6e729c1f8444aec5f412cd0cfa590bb5718426d070ab9ce1c6a92c4dafed5cb8` |
| `corrupt_chunk.exr` | 585 | rgb_half_zip.exr with 32 bytes inside its only chunk flipped: the header parses, the deflate stream fails its checksum | `f3ee0cc03370fb5b3e87df65046f5010129a7e9b6e94f238aa24a247d5b59222` |
| `magic_only.exr` | 64 | the OpenEXR magic number and version word followed by 56 bytes of nonsense: recognised, then refused | `2af7da21c3493390e411ba59d4af1ed77f33f6f5f7b4066cb557082678f52efe` |
| `garbage.bin` | 4,096 | 4096 pseudo-random bytes (seed 0x455852): not recognised | `dc89d4d6f1684efacffefd302a87d52bb2b615510b61f87ad68d31d724d53174` |
| `not_exr.txt` | 30 | a text file: not recognised | `e589a9e10996bfff6a7441768b104ce2815f3a1ce160e0492c4f91344abf5c73` |
| `multipart_views.exr` | 1,018 | two flat parts: part 0 "right" (view right, only R lit) and part 1 "left" (view left, full ramp): the left view is shown | `e5d6075c87830ae42b790e986f6f8a7c40f46ad99159126dce4808f6254692fe` |
| `multipart_deep_flat.exr` | 1,651 | part 0 "deep" (deepscanline RGB) and part 1 "flat" (8x8 RGB): the flat part is shown, deep parts skipped | `3abf3248e70076a58001b73acf3ffc5e3649fbd55d5acb7a86e4973c298faa9d` |
| `deep_only.exr` | 1,064 | a single deepscanline part: refused (deep-only) | `e52f5a401de03491894e44d153622a75368549d3899076bb1e960854beb4cf72` |
| `multipart_deeptile_flat.exr` | 1,327 | part 0 "deep" (deeptile RGB, 4x4 tiles) and part 1 "flat" (8x8 RGB): the flat part is shown, deep parts skipped | `6ff6553a2bc5b3e725a97c6be6e0c1d990328aa050442ee8a871987d7df2b8c6` |
| `multiview_string.exr` | 520 | single part whose multiView attribute is a plain string, not a string vector: ignored, the bare RGB is shown | `b1a4c8c167c43eade8d3d12d36125d740160d7d1e3a70770a15b9bcec043fc5e` |
| `multiview_left_first.exr` | 637 | single part, multiView [left, right]: the bare channels are the left view and are shown | `ec6470875537ec19522173b2d9743fc49c5d6a3ba3f0e4e073a855442cdb4b31` |
| `multiview_right_first.exr` | 637 | single part, multiView [right, left]: the bare channels are the right view, the left layer is shown | `7854db10de2464b6c4da35914120faed23cb7565aa7ef8a8ca2043b239b31548` |
| `tiled_rgba_3x3.exr` | 1,051 | 8x8 RGBA half in 3x3 tiles (partial tiles at the right and bottom), one level | `537b2241d6ceed6a59625493b0f0350d70f2879470b0e4ae950377f1bdfef6df` |
| `tiled_data_offset.exr` | 704 | 8x8 RGB half in 4x4 tiles, data at 5,3 inside a 16x16 display window | `80e4f691b3ced4c82d0dcd27b3c5706b0e1a519d3c75b866c7d59b2d623918dc` |
| `chroma_zips.exr` | 896 | 16x8 half Y/RY/BY with A = 1, ZIPS: one line per chunk, so the odd chunks carry no chroma row | `1dd0d48cf44cdd9a69f5e405f1c8554c80dd6f5fafa86674f83b141f3395e3f4` |
| `chroma_extra_channel.exr` | 551 | 16x8 half Y/RY/BY plus a float Z channel, ZIP: Z is skipped in the luminance/chroma path | `8348157f9461acc6b3b7c6657b28c94226f1ed2832e0d361517898b60d688d10` |
| `chroma_missing_by.exr` | 401 | 16x8 half Y and RY without BY, ZIP: no chroma pair, Y alone is shown as grey | `0ce2907f6ae58608dffe85cae5cd8465f7c64ac4b1086c17dee849f7f0c5afe4` |
| `chroma_corrupt_chunk.exr` | 551 | chroma_extra_channel.exr with 32 bytes inside its only chunk flipped: the luminance/chroma decode fails on the chunk | `aef17a995990aaa3ef9aa189825b7d8e109c6e9725d7246d6c199004265f58e0` |
| `yc_whitey0.exr` | 530 | 16x8 half Y/RY/BY (chroma_extra_channel.exr without Z) with chromaticities whose white has y = 0: unusable, reconstructed and shown as Rec.709 with a note | `6a3f3d14e64e184e93f2d6f4559786763417fd1a4f81436b5bb049ea62ae6c07` |
| `yc_collinear.exr` | 530 | 16x8 half Y/RY/BY with three equal primaries: unusable, reconstructed and shown as Rec.709 with a note | `1dbdddb94ed318dc9817f612b621390438e78696a6f0a939720325b7c5959de9` |
| `yc_huge_data.exr` | 896 | chroma_zips.exr with its header patched to a 20000x20000 data window behind a 1x1 display window: refused before any allocation (the luminance/chroma path reads the whole data window) | `a7e7178fc9f286310d2a82fbf0c98da6a49605c6b9ceb09654e3923d8a8f5647` |

