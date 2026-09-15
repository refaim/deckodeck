# Fixture sources and verified expectations

The libavif corpus files are pinned to commit
[`66663952a677bb8a13ea1530d5694775d7d143d4`](https://github.com/AOMediaCodec/libavif/commit/66663952a677bb8a13ea1530d5694775d7d143d4).
`LIBAVIF_DATA_README.md` is the upstream `tests/data/README.md` from that commit and preserves its
per-file notices. Unless overridden below, those files use libavif's BSD-2-Clause license.

`scripts/libavif-fixtures.sha256` attests only to the integrity of the bytes committed to this
repository (that `fetch-fixtures.ps1` downloaded and stored exactly what was pinned here), not to
upstream provenance; trust in the origin and license of each file rests on the table below and the
pinned libavif commit, not on the checksum matching.

Expected stream values were measured with FFmpeg/ffprobe 9.0.1 using `-count_frames -show_streams
-show_format`. For `avis` files the frame count and pixel format below come from the sequence track,
not the one-frame primary item. “Alpha: yes” means ffprobe exposed an auxiliary stream titled
`Alpha`. All dimensions are the decoded dimensions reported by ffprobe.

## Pinned libavif files

| File | Origin | License | Exercises | Verified expectation |
|---|---|---|---|---|
| `white_1x1.avif` | [`tests/data/white_1x1.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/white_1x1.avif) | BSD-2-Clause | Smallest still image | 1×1, 1 frame, `yuv444p`, alpha: no |
| `kodim03_yuv420_8bpc.avif` | [`tests/data/io/kodim03_yuv420_8bpc.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/io/kodim03_yuv420_8bpc.avif) | Eastman Kodak: unrestricted use (upstream notice) | Real 8-bit 4:2:0 photo | 768×512, 1 frame, `yuv420p`, alpha: no |
| `cosmos1650_yuv444_10bpc_p3pq.avif` | [`tests/data/io/cosmos1650_yuv444_10bpc_p3pq.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/io/cosmos1650_yuv444_10bpc_p3pq.avif) | CC BY 3.0 (upstream notice) | 10-bit 4:4:4 P3/PQ | 1024×428, 1 frame, `yuv444p10le`, alpha: no; no CLLI, so BT.2390 uses 1000 nits; converted BGRA64 at (0,0) = (48085,35561,25307,65535), (512,214) = (2682,20499,42409,65535), (1023,427) = (0,50080,49456,65535) |
| `alpha_noispe.avif` | [`tests/data/alpha_noispe.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/alpha_noispe.avif) | BSD-2-Clause | Alpha auxiliary item missing `ispe` | 80×80, 1 frame, `yuv444p` + `gray` alpha, alpha: yes |
| `abc_color_irot_alpha_irot.avif` | [`tests/data/abc_color_irot_alpha_irot.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/abc_color_irot_alpha_irot.avif) | BSD-2-Clause | Matching color/alpha `irot` association | 512×256, 1 frame, `yuv444p` + `gray` alpha, alpha: yes |
| `abc_color_irot_alpha_NOirot.avif` | [`tests/data/abc_color_irot_alpha_NOirot.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/abc_color_irot_alpha_NOirot.avif) | BSD-2-Clause | Missing alpha `irot` association | 512×256, 1 frame, `yuv444p` + `gray` alpha, alpha: yes |
| `clap_irot_imir_non_essential.avif` | [`tests/data/clap_irot_imir_non_essential.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/clap_irot_imir_non_essential.avif) | BSD-2-Clause | Non-essential clap/irot/imir transforms | 12×34, 1 frame, `yuv444p10le` + `gray10le` alpha, alpha: yes |
| `clop_irot_imor.avif` | [`tests/data/clop_irot_imor.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/clop_irot_imor.avif) | BSD-2-Clause | Unknown non-essential transform properties | 12×34, 1 frame, `yuv444p10le` + `gray10le` alpha, alpha: yes |
| `sofa_grid1x5_420.avif` | [`tests/data/sofa_grid1x5_420.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/sofa_grid1x5_420.avif) | BSD-2-Clause | 1×5 4:2:0 grid | 1024×770, 1 assembled frame, `yuv420p`, alpha: no |
| `color_grid_alpha_nogrid.avif` | [`tests/data/color_grid_alpha_nogrid.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/color_grid_alpha_nogrid.avif) | BSD-2-Clause | Color grid with non-grid alpha | 80×80, 1 assembled frame, `yuv444p` + `gray` alpha, alpha: yes |
| `colors-animated-8bpc.avif` | [`tests/data/colors-animated-8bpc.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/colors-animated-8bpc.avif) | BSD-2-Clause | 8-bit image sequence | 150×150, 5 frames, `yuv420p`, alpha: no |
| `colors-animated-8bpc-alpha-exif-xmp.avif` | [`tests/data/colors-animated-8bpc-alpha-exif-xmp.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/colors-animated-8bpc-alpha-exif-xmp.avif) | BSD-2-Clause | Animated alpha plus EXIF/XMP | 150×150, 5 frames, `yuv420p` + `gray` alpha, alpha: yes |
| `colors-animated-12bpc-keyframes-0-2-3.avif` | [`tests/data/colors-animated-12bpc-keyframes-0-2-3.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/colors-animated-12bpc-keyframes-0-2-3.avif) | BSD-2-Clause | 12-bit sequence and sparse keyframes | 64×64, 5 frames, `yuv422p12le` + `gray12le` alpha, alpha: yes; frame 0 alpha (0,0) is source sample 4094 and BGRA64 sample 65519, not the left-shifted 65504 |
| `colors_hdr_rec2020.avif` | [`tests/data/colors_hdr_rec2020.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/colors_hdr_rec2020.avif) | BSD-2-Clause | 10-bit Rec.2020 HDR/PQ | 200×200, 1 frame, `yuv444p10le`, alpha: no; CLLI maxCLL 470 nits; converted BGRA64 at (0,0) = (0,1014,65535,65535), (100,100) = (7854,53351,63663,65535), (199,199) = (65535,65535,65535,65535) |
| `colors_sdr_srgb.avif` | [`tests/data/colors_sdr_srgb.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/colors_sdr_srgb.avif) | BSD-2-Clause | 8-bit sRGB colour signalling | 200×200, 1 frame, `yuv444p`, alpha: no |
| `paris_icc_exif_xmp.avif` | [`tests/data/paris_icc_exif_xmp.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/paris_icc_exif_xmp.avif) | BSD-2-Clause | ICC, EXIF, and XMP metadata | 403×302, 1 frame, `yuv444p`, alpha: no |
| `draw_points_idat_progressive.avif` | [`tests/data/draw_points_idat_progressive.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/draw_points_idat_progressive.avif) | BSD-2-Clause | Progressive layers stored in `idat` | ffprobe rejects the header as “Not yet implemented”; dimensions/frame/pixel format/alpha unavailable from ffprobe |
| `extended_pixi.avif` | [`tests/data/extended_pixi.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/extended_pixi.avif) | BSD-2-Clause | Extended `pixi` and vertical chroma position | 4×4, 1 frame, `yuv420p`, alpha: no |
| `weld_sato_12B_8B_q0.avif` | [`tests/data/weld_sato_12B_8B_q0.avif`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/weld_sato_12B_8B_q0.avif) | Signature Edits irrevocable unrestricted-use license (full text in upstream notice) | 12-bit sample-transform extension | 1024×684, 1 frame, `yuv444p12le`, alpha: no |

## Derived EXIF-orientation files

These files inherit their source file's license. They were made byte-for-byte with the installed
ExifTool and the following commands, then checked with `exiftool -Orientation -n <file>`:

```powershell
exiftool -overwrite_original -Orientation=6 -n kodim03_exif_orientation_6.avif
exiftool -overwrite_original -Orientation=3 -n kodim03_exif_orientation_3.avif
exiftool -overwrite_original -Orientation=6 -n abc_color_irot_alpha_irot_plus_exif6.avif
```

| File | Derived from | License | Exercises | Verified expectation | SHA-256 |
|---|---|---|---|---|---|
| `kodim03_exif_orientation_6.avif` | `kodim03_yuv420_8bpc.avif` | Eastman Kodak: unrestricted use (upstream notice) | EXIF orientation 6 without AVIF transforms | 768×512 coded dimensions; EXIF orientation 6; PictureView code 7 | `473182907469BA4DA616F228F4F68EEB29CC150D12D3E98A90207ABBF712FC79` |
| `kodim03_exif_orientation_3.avif` | `kodim03_yuv420_8bpc.avif` | Eastman Kodak: unrestricted use (upstream notice) | EXIF orientation 3 without AVIF transforms | 768×512 coded dimensions; EXIF orientation 3; PictureView code 3 | `B338DBC677E552B70C138F79EB799C3A1256BE39FAECE9B99A103F91C06668AD` |
| `abc_color_irot_alpha_irot_plus_exif6.avif` | `abc_color_irot_alpha_irot.avif` | BSD-2-Clause | AVIF `irot` plus EXIF orientation 6 precedence | EXIF orientation 6 is ignored; shared `irot` remains authoritative; PictureView code 0 | `577D7121F5A05AE0414195F704F50A9D54E86B3EDBBCCC866803E1F3DE40CBED` |

## Synthetic files

These files are generated without external source images by `scripts/make-synthetic-fixtures.ps1`
and are dedicated to CC0-1.0. The animation is built from a concat-demuxer timeline and then remuxed
without its repeated end-marker frame; ffprobe verifies its three `stts` sample durations as 100,
200, and 300 ms.

| File | Origin | License | Exercises | Verified expectation |
|---|---|---|---|---|
| `quad_rgb_lossless.avif` | Project script, ffmpeg 9.0.1/libaom-av1 | CC0-1.0 | Lossless RGB with identity matrix/full range | 64×64, 1 frame, `gbrp`, alpha: no; TL `(255,0,0)`, TR `(0,255,0)`, BL `(0,0,255)`, BR `(255,255,255)` exactly |
| `quad_yuv420.avif` | Project script, ffmpeg 9.0.1/libaom-av1 | CC0-1.0 | Lossy limited-range 4:2:0 conversion | 64×64, 1 frame, `yuv420p`, alpha: no; the generator paints ideal red `(255,0,0)`, green `(0,255,0)`, blue `(0,0,255)` and white `(255,255,255)` quadrants. The values `(254,0,1)`, `(0,253,0)`, `(1,0,254)`, `(255,255,255)` (±2) are ffmpeg-swscale conversions of the coded YUV at the quadrant centres; libavif+libyuv converts the same coded planes to values up to ~11 away on the saturated quadrants (dav1d's output is bit-exact, only the YUV→RGB rounding differs). Tests therefore compare against the generator's ideal colours with tolerance 16, which still rejects a wrong matrix, a wrong range or swapped channels |
| `alpha_steps.avif` | Project script, ffmpeg 9.0.1/libaom-av1 | CC0-1.0 | Lossless `yuva444p` source split into color and alpha auxiliary items | 96×32, 1 frame, `yuv444p` + `gray` alpha, alpha: yes; vertical bands are exactly `(255,255,255,0)`, `(255,255,255,128)`, `(255,255,255,255)` |
| `anim_3frames.avif` | Project script, ffmpeg 9.0.1/libaom-av1 | CC0-1.0 | `avis` image sequence and variable timing | 64×64, 3 frames, `gbrp`, alpha: no; exact frames `(255,0,0)`, `(0,255,0)`, `(0,0,255)` lasting 100 ms, 200 ms, and 300 ms respectively |
| `gray_400.avif` | Project script, ffmpeg 9.0.1/libaom-av1 | CC0-1.0 | Monochrome 4:0:0 | 64×64, 1 frame, `gray`, alpha: no |
| `tenbit_444.avif` | Project script, ffmpeg 9.0.1/libaom-av1 | CC0-1.0 | Lossless 10-bit 4:4:4 | 64×64, 1 frame, `yuv444p10le`, alpha: no; BGRA64 normalizes the limited luma range [64,940], scales RGB over [0,65535], and fills alpha with 65535 |
| `not_avif.png` | Project script, ffmpeg 9.0.1 | CC0-1.0 | Valid non-AVIF signature | 64×64, 1 decoded frame, `rgb24`, alpha: no |
| `not_avif.bmp` | Project script, ffmpeg 9.0.1 | CC0-1.0 | Valid non-AVIF signature | 64×64, 1 decoded frame, `bgr24`, alpha: no |
| `garbage.bin` | Project script, deterministic PRNG seed `0x41564946` | CC0-1.0 | Hostile pseudo-random input | 4096 bytes; ffprobe finds no image stream, dimensions/frame/pixel format/alpha unavailable |
| `truncated.avif` | First 60% of generated `quad_yuv420.avif` | CC0-1.0 | Truncated-container rejection | 211 bytes in this generation; ffprobe reports invalid data, dimensions/frame/pixel format/alpha unavailable |

`LIBAVIF_DATA_README.md` comes from
[`tests/data/README.md`](https://raw.githubusercontent.com/AOMediaCodec/libavif/66663952a677bb8a13ea1530d5694775d7d143d4/tests/data/README.md)
at the same pinned commit and is retained as the authoritative notice file.
