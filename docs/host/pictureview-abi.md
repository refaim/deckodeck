# PictureView 2021.4.19 x64: extended PVD decoder ABI

> **Interoperability note.** This document is derived from static analysis of the
> unmodified 2021.4.19 x64 `0PictureView.dll` and the decoder plug-ins shipped with
> it. It describes that binary, not a source-level promise by the host author and
> not necessarily any other PictureView build. The public baseline is PVD interface
> v1.0 in `third_party/pvd/PictureViewPlugin.h`.

All virtual addresses below assume the preferred image base `0x180000000`.
`llvm-readobj` identifies the host as COFF x86-64 with PE timestamp
`2021-04-19 13:59:56` (`0x607d8cdc`). No binary was executed or modified.

## Conclusions

- The host passes `pvdPageDecode` a zeroed **0x58-byte** stack object. The public
  header describes its first 0x20 bytes. The recovered x64 extension occupies
  offsets `0x20..0x57`.
- The host does not copy the object as a block after the call. It reads individual
  fields, passes the same address to `pvdPageFree`, and retains only selected
  results.
- The profile pointer/length pair at `+0x20/+0x28`, advertised by `Flags & 0x4`,
  is produced by BMP.pvd and JpegRec.pvd but is **never loaded by this host**.
  Consequently it is not dereferenced, parsed, compared, or passed to any direct
  or dynamically resolved API. The swapped-primary ICC experiment rendering
  unchanged is the expected result for this binary.
- `nBPP == 0` selects the planar/semiplanar YUV ABI. `nBPP != 0` selects the RGB,
  grayscale, or indexed upload path.
- There is no non-OpenGL GDI raster presentation path in this host. GDI is used
  to establish/swap an OpenGL pixel format; dynamically loaded GDI+ is a decoder
  fallback. In particular, 48- and 64-bpp images reach the OpenGL uploader as
  unsigned-16-bit RGB/BGRA data.

## Recovered `pvdInfoDecode` layout

The types below describe the x64 wire layout. Names beginning at `+0x20` are
descriptive names assigned by this note.

| Offset | Size | Recovered meaning | When the host reads it | Confidence |
|---:|---:|---|---|---|
| `0x00` | 8 | `BYTE *pImage` (RGB/indexed/gray pixels, or Y plane when `nBPP == 0`) | All successful decodes | Proven |
| `0x08` | 8 | `UINT32 *pPalette` | RGB/indexed branch (`nBPP != 0`); materially used for indexed formats | Proven/public |
| `0x10` | 4 | `UINT32 Flags` | Orientation nibble always; alpha mask on the nonzero-`nBPP` branch | Proven/public + extended bits |
| `0x14` | 4 | `UINT32 nBPP` | Always; zero selects YUV | Proven/public |
| `0x18` | 4 | `UINT32 nColorsUsed` | Loaded on the nonzero-`nBPP` branch and supplied to the uploader | Proven/public |
| `0x1c` | 4 | `INT32 lImagePitch` | All successful decodes; Y-plane pitch for YUV | Proven/public |
| `0x20` | 8 | `const BYTE *pICCProfile` | **Never read by the host** | Meaning proven from BMP V5 and JPEG producers; host behavior proven |
| `0x28` | 4 | `UINT32 cbICCProfile` | **Never read by the host** | Meaning proven from BMP V5 and JPEG producers; host behavior proven |
| `0x2c` | 4 | Reserved/alignment | Never read | Proven |
| `0x30` | 8 | `BYTE *pPlane1` | `nBPP == 0` | Proven |
| `0x38` | 8 | `BYTE *pPlane2` | `nBPP == 0` | Proven |
| `0x40` | 8 | `BYTE *pPlane3` / fourth component | Loaded when `nBPP == 0`; consumed only by a four-plane format | Proven |
| `0x48` | 4 | `INT32 lChromaPitch` | `nBPP == 0` | Proven |
| `0x4c` | 4 | `UINT32 YuvFormat` | `nBPP == 0` | Proven |
| `0x50` | 4 | `UINT32 lFrameTime`, milliseconds; decode-time override | Seeded before decode and retained after a successful render | Proven |
| `0x54` | 4 | Reserved/alignment | Never read | Proven |

The extension should therefore be represented as a naturally aligned 0x58-byte
x64 structure. The pointer fields require 8-byte alignment. Producers should
leave fields they do not use as zero. The host already zero-initializes the whole
object, but relying on that outside this specific build would be unwise.

### Allocation, call, and scalar reads

The sole recovered PVD decode dispatch uses `rsp+0x70` as the object base:

```asm
1800197c1  xor  edx, edx
1800197c3  lea  r8d, [rdx + 0x58]
1800197c7  lea  rcx, [rsp + 0x70]
1800197cc  call 0x180019b4a             ; memset(base, 0, 0x58)
1800197d1  mov  eax, [rsp + 0x64]       ; pvdInfoPage::lFrameTime
1800197d5  mov  [rsp + 0xc0], eax       ; decode + 0x50
...
1800197ef  lea  r8, [rsp + 0x70]        ; pDecodeInfo
1800197fe  call qword ptr [rax + 0x30]  ; pvdPageDecode
```

After success it reads `Flags` and branches on `nBPP`:

```asm
180019809  mov  eax, [rsp + 0x80]       ; +0x10 Flags
180019810  shr  eax, 4
180019813  xor  [rsi + 0x240], eax      ; compose file transform
180019837  mov  eax, [rsp + 0x84]       ; +0x14 nBPP
18001983e  test eax, eax
180019840  jne  0x18001989b             ; RGB/gray/indexed setup
```

The YUV branch loads exactly the recovered YUV fields:

```asm
1800199a9  mov  r12, [rsp + 0xb0]       ; +0x40 plane 3
1800199b1  mov  ebx, [rsp + 0xbc]       ; +0x4c format
1800199b8  mov  edi, [rsp + 0xb8]       ; +0x48 chroma pitch
1800199bf  mov  r13d, [rsp + 0x8c]      ; +0x1c Y pitch
1800199c7  mov  rax, [rsp + 0xa8]       ; +0x38 plane 2
1800199d7  mov  rax, [rsp + 0xa0]       ; +0x30 plane 1
1800199e7  mov  rax, [rsp + 0x70]       ; +0x00 Y plane
```

The same object is passed to `pvdPageFree` at `0x180019a70`. After that call, on
a successful render, the host retains the seeded-or-overridden delay:

```asm
180019a67  lea  rdx, [rsp + 0x70]
180019a70  call qword ptr [rax + 0x38]  ; pvdPageFree
...
180019a97  mov  eax, [rsp + 0xc0]       ; +0x50
180019a9e  mov  [rsi + 0x23c], eax
```

FLIF.pvd confirms that `+0x50` is intentional output, not padding:

```asm
18008ffde  cmp  dword ptr [r15 + 0xe0], 2
18008ffe8  mov  ecx, dword ptr [r13 + 0x60]
18008ffec  mov  dword ptr [r14 + 0x50], ecx
```

## Decode flags

“Bit 4” is ambiguous, so this table uses masks. In particular, the ICC flag is
mask `0x4` (bit position 2), while the transform starts at bit position 4.

| Mask | Meaning in shipped producers | Host behavior | Confidence |
|---:|---|---|---|
| `0x00000001` | `PVD_IDF_READONLY` from the public header | No branch in this decode consumer; `pvdPageFree` is still called | Public meaning; host behavior proven |
| `0x00000002` | Alpha channel is meaningful | For `nBPP != 0`, passed to the RGB uploader as `(Flags >> 1) & 1`; ignored by the YUV branch | Proven |
| `0x00000004` | `+0x20/+0x28` contain an embedded ICC byte range | Never tested; the two fields are never loaded | Producer meaning and host non-use proven |
| `0x00000008` | Unknown/reserved | Ignored by this host | Host behavior proven; meaning unknown |
| `0x000000f0` | Image orientation/transform code in `Flags >> 4` | XOR-composed into the viewer transform word immediately after decode | Proven; codes `0..7` mapped below |
| `>= 0x00000100` | Unknown; no shipped producer use established | Also included in the unmasked `Flags >> 4`, so plug-ins must not set these speculatively | Host behavior proven; meaning unknown |

JpegRec.pvd obtains an EXIF orientation, maps it through the byte table
`00 02 03 01 06 07 05 04`, and emits the result in the high nibble:

```asm
180009050  lea  rcx, [rip + 0x1de91]    ; table at 0x180026ee8
180009057  mov  r8b, [rcx + rdx]        ; EXIF orientation - 1
18000905b  mov  [r9 + 0xc8], r8b
...
18000fbfb  movzx eax, byte ptr [rsi + 0x238]
18000fc02  shl  eax, 4
18000fc05  mov  [rbx + 0x10], eax       ; pDecodeInfo->Flags
```

| EXIF orientation | Transform code (`Flags >> 4`) | Geometric meaning | Confidence |
|---:|---:|---|---|
| 1 | 0 | Identity | Proven mapping |
| 2 | 2 | Mirror horizontal | Proven mapping; conventional EXIF meaning |
| 3 | 3 | Rotate 180 degrees | Proven mapping; conventional EXIF meaning |
| 4 | 1 | Mirror vertical | Proven mapping; conventional EXIF meaning |
| 5 | 6 | Transpose | Proven mapping; conventional EXIF meaning |
| 6 | 7 | Rotate 90 degrees clockwise | Proven mapping; conventional EXIF meaning |
| 7 | 5 | Transverse | Proven mapping; conventional EXIF meaning |
| 8 | 4 | Rotate 270 degrees clockwise | Proven mapping; conventional EXIF meaning |

WIC.pvd independently emits alpha mask `0x2` and a transform nibble:

```asm
1800015da  bt   ecx, 8
1800015e0  mov  dword ptr [rdi + 0x10], 2
1800015e7  mov  eax, ecx
1800015e9  shr  eax, 0xc
1800015ec  and  eax, 0xf0
1800015f1  or   dword ptr [rdi + 0x10], eax
```

The public `pvdInfoImage::Flags` is separate. At `pvdFileOpen` the host provides
a zeroed 0x20-byte x64 `pvdInfoImage` and tests only mask `0x1`
(`PVD_IIF_ANIMATED`); no additional image-level flag was found.

## ICC profile pair: produced but ignored

BMP.pvd recognizes a BITMAPV5 header (`bV5Size == 0x7c`), requires
`bV5CSType == PROFILE_EMBEDDED` (`0x4d424544` in the compared little-endian
dword), bounds-checks `bV5ProfileData/bV5ProfileSize`, then writes:

```asm
18000167c  or   dword ptr [r9 + 0x10], 4
180001681  mov  eax, dword ptr [r10 + 0x7e]  ; profile data offset
18000168c  mov  qword ptr [r9 + 0x20], rcx   ; profile bytes
180001690  mov  eax, dword ptr [r10 + 0x82]  ; profile size
180001697  mov  dword ptr [r9 + 0x28], eax
```

JpegRec.pvd publishes the same triplet:

```asm
18000fc28  mov  rax, qword ptr [rsi + 0x248]
18000fc2f  mov  edx, dword ptr [rsi + 0x250]
18000fc3d  or   dword ptr [rbx + 0x10], 4
18000fc41  mov  qword ptr [rbx + 0x20], rax
18000fc45  mov  dword ptr [rbx + 0x28], edx
```

In the host's only `pvdPageDecode` consumer, the complete post-call path from
`0x180019801` through the `pvdPageFree` call at `0x180019a70` contains no load
from object offsets `+0x20` or `+0x28`. It also contains no test of `Flags & 4`.
This proves that the pointer cannot be dereferenced or compared on that path.

Additional negative checks support, but are not needed for, that data-flow proof:

- no immediate matching `acsp`, `rXYZ`, `wtpt`, `rTRC`, `curv`, `para`, or
  `desc` was found in either byte order in the full host disassembly;
- no `acsp`, LittleCMS, `mscms`, `ColorProfile`, or standalone `ICM` string was
  found in the host image (`para` occurs only inside OpenGL names such as
  `glProgramLocalParameter4fvARB`);
- the PE has no import from `MSCMS.dll` and no direct ICM/color-profile API;
- the exhaustive dynamic-resolution inventory below contains no color-management
  function.

The settings key `sRGBGammaAware` is not an ICC switch. It is one of the OpenGL
render configuration bits alongside shader, dithering, mipmap, and 10-bit output
options; there is no control-flow edge from it (or any setting) to `+0x20/+0x28`.

**Interoperability rule for this host:** it is harmless to reproduce the profile
triplet for compatibility with other/possible future hosts, but it has no visual
effect in 2021.4.19 x64. The profile storage should nevertheless remain valid
until `pvdPageFree`, because the producer-side convention does not establish a
shorter lifetime and other host builds may consume it.

## `nBPP` dispatch

The OpenGL upload function begins at `0x180004f90`. It computes `nBPP - 1`,
rejects values above 64, maps the remaining values through the byte table at RVA
`0x5c04`, and jumps through the target table at RVA `0x5bd4`:

```asm
180004fe2  lea  eax, [r9 - 1]           ; r9d = nBPP
180005001  cmp  eax, 0x3f
180005004  ja   0x18000544c
180005019  movzx eax, byte ptr [r8 + rax + 0x5c04]
180005022  mov  ecx, dword ptr [r8 + 4*rax + 0x5bd4]
18000502d  jmp  rcx
```

All unlisted nonzero values go to `0x18000544c`, which formats the diagnostic
`nBPP: 0x%X` and executes `int3` at `0x180005481`.

| `nBPP` | Interpretation/upload type | Handler | Confidence |
|---:|---|---:|---|
| 0 | Extended YUV planes; bypasses this jump table | `0x1800199a9` then `0x1800042b0/44f0/48c0` | Proven |
| 1 | Indexed/monochrome (palette, with black/white fast path) | `0x180005088` | Proven |
| 2 | Indexed, palette expanded | `0x180005214` | Proven |
| 4 | Indexed, palette expanded | `0x180005214` | Proven |
| 8 | Indexed when `pPalette != nullptr`; otherwise 8-bit gray | `0x1800051f8` | Proven |
| 15 | Packed RGB555/pad, uploaded as `GL_UNSIGNED_SHORT_1_5_5_5_REV` | `0x180005051` | Proven |
| 16 | 16-bit grayscale, `GL_UNSIGNED_SHORT` | `0x180005430` | Proven |
| 17 | Packed RGB565, uploaded as `GL_UNSIGNED_SHORT_5_6_5_REV` | `0x18000506e` | Proven |
| 24 | BGR, 8 bits per channel | `0x18000505d` | Proven |
| 30 | BGRA 2:10:10:10, `GL_UNSIGNED_INT_2_10_10_10_REV` | `0x18000502f` | Proven |
| 32 | BGRA, 8 bits per channel | `0x180005057` | Proven |
| 48 | RGB, unsigned 16 bits per channel | `0x180005419` | Proven |
| 64 | BGRA, unsigned 16 bits per channel | `0x180005413` (falls through `0x5419`) | Proven |

The 48/64 handlers are explicit:

```asm
180005413  mov  r12d, 0x80e1           ; GL_BGRA (64 bpp only)
180005419  shr  esi, 3                  ; bytes per pixel
18000541c  mov  r13d, 0x1403           ; GL_UNSIGNED_SHORT
```

WIC.pvd corroborates the accepted high-depth formats by choosing `nBPP` 48 or 64
at `0x18000171d` and `0x1800016cb`, respectively, before copying pixels.

## YUV ABI and format word

When `nBPP == 0`, `pImage` is plane 0 (normally Y). `pPlane1`, `pPlane2`, and
optionally `pPlane3` are additional components. `lImagePitch` is plane 0's byte
pitch; `lChromaPitch` is used for the remaining color planes. The format word is
a bitfield, not an enum: the host never switches on a complete literal value.

| Bits/mask | Host interpretation | Confidence |
|---:|---|---|
| `0x0003` | Horizontal chroma subsampling exponent: chroma width is `ceil(width / 2^value)` | Proven |
| `0x000c` | Vertical chroma subsampling exponent in bits 2-3: chroma height is `ceil(height / 2^((value >> 2)))` | Proven |
| `0x0010` | Limited/video-range level conversion | Inferred from TIFF's `ReferenceBlackWhite` test for scaled 235 and host shader selection |
| `0x0020` | Special two-plane, unsigned-16-bit 4:2:0 path (P016 layout) | Proven in host and BMP/TIFF producers |
| `0x0040` | Explicit BT.601-class YCbCr matrix selector | Inferred with high confidence from WebP/BPG usage and the host matrix selector |
| `0x0080` | Explicit BT.709-class YCbCr matrix selector | Proven/inferred from TIFF comparing `Kr` with approximately 0.2125 before setting this bit |
| neither `0x0040` nor `0x0080` | Matrix selected heuristically: the host tests width `> 1024` or height `> 576` | Proven behavior; exact matrix names inferred |
| `0x0100` | Horizontal half-texel chroma sampling offset | Proven shader-coordinate effect; “centered chroma” name inferred |
| `0x0200` | Vertical half-texel chroma sampling offset | Proven shader-coordinate effect; “centered chroma” name inferred |
| `0x0400` | A fourth plane is present (unless `0x20` selected the special two-plane path) | Proven |
| `0x0800` with `0x0400` | Selects alpha-like treatment of the fourth plane; BPG uses `0x0c00` for images with alpha | Producer use proven; general semantic boundary inferred |
| all other bits | No shipped meaning established | Unknown; leave zero |

Evidence for the plane count and subsampling masks:

```asm
1800042c4  and  edx, 0x20
180004328  mov  r13d, 2                 ; special two-plane case
180004330  bt   r8d, 0xa
18000433c  add  r13d, 3                 ; otherwise 3 or 4 planes
...
1800043a4  and  ecx, 3                  ; horizontal exponent
1800043b3  shr  edx, 2
1800043b6  and  edx, 3                  ; vertical exponent
```

The matrix/range and chroma-position portion of the fragment-program key is built
at `0x180004a2f..0x180004a91`; bits 8 and 9 cause coordinate offsets at
`0x180004b48` and `0x180004b60`.

### Values emitted by bundled decoders

| Producer/layout | `YuvFormat` | Plane/pitch observations | Confidence |
|---|---:|---|---|
| BMP YV24, 8-bit 4:4:4 | `0x000` | Three full-size planes; `+0x30` and `+0x38`; chroma pitch = width | Proven |
| BMP I420, 8-bit 4:2:0 | `0x115` | Three planes; H/V exponents = 1; horizontal chroma offset; limited range | Proven value/layout; semantic bit names partly inferred |
| BMP P016, 16-bit 4:2:0 | `0x135` | `+0x30 == +0x38` points at interleaved UV; both pitches = `2 * width` | Proven |
| WebP lossy YUV, 8-bit 4:2:0 | `0x055` | Three planes, H/V exponents = 1 | Proven |
| JpegRec three-plane JPEG | `B | H | V`, `B in {0x040,0x340}`, `H in {0,1,2}`, `V in {0,4,8}` | Sampling factors determine H/V; `0x300` requests both chroma offsets | Proven formula; producer condition selecting `B` not fully named |
| JpegRec four-plane JPEG | Previous value OR `0x400` or `0xc00` | Fourth pointer at `+0x40`; `0x400` selects the host's special non-alpha four-component shader, `0xc00` the alternate treatment | Proven formula; exact JPEG color-space names inferred/unknown |
| BPG YUV + alpha | `0xc40`, `0xc41`, `0xc45`, `0xc80`, `0xc81`, or `0xc85` | Four planes; 4:4:4, 4:2:2, or 4:2:0; matrix selector varies | Proven |
| TIFF YCbCr, 8-bit | `H | V | R | M` | Three planes; `H in {0,1,2}`, `V in {0,4,8}`, `R in {0,0x10}`, `M in {0x40,0x80}` | Proven formula; `R/M` names inferred as above |
| TIFF YCbCr, 16-bit | Previous TIFF formula OR `0x20` | Special two-plane path; both byte pitches are doubled | Proven |

Representative producer writes are visible in BMP.pvd:

```asm
1800016ab  mov  dword ptr [r9 + 0x4c], 0x135  ; P016
1800016b3  mov  dword ptr [r9 + 0x1c], eax
1800016b7  mov  dword ptr [r9 + 0x48], eax
1800016c7  mov  qword ptr [r9 + 0x30], rax
1800016cb  mov  qword ptr [r9 + 0x38], rax
...
180001737  mov  dword ptr [r9 + 0x4c], 0x115  ; I420
180001747  mov  dword ptr [r9 + 0x48], eax
180001751  mov  qword ptr [r9 + 0x30], rax
18000176d  mov  qword ptr [r9 + 0x38], rcx
```

WebP.pvd uses `0x55` at `0x18001448b`. BPG.pvd loads the subsampling bases
`0x405/0x401/0x400` from its table at RVA `0x1aab8`, then ORs `0x840` or `0x880`
at `0x180018c24..0x180018c45`. TIFF.pvd constructs the masks at
`0x18004d4de..0x18004d5a2`; its helpers set `0x20` for 16-bit storage at
`0x18004c890` and `0x18004ce3b`.

## Settings consulted by rendering

The advanced-render settings are obtained and saved through Far's supplied
settings callbacks. The host does not import registry APIs, and no INI access was
found, so these are Far settings key names rather than registry value paths or an
INI section established by this binary.

The relevant UTF-16 strings and RVAs are:

| RVA | Key | Relevance | Confidence |
|---:|---|---|---|
| `0x20980` | `UseMipmaps` | Passed into texture creation; read at decode call site `0x18001992d` from configuration bit 24 | Proven |
| `0x20998` | `UseShaders` | Selects programmable rendering facilities | Proven key/use class |
| `0x209b0` | `ShaderOptimization` | Fragment-program variant/optimization | Proven key; detailed effect not traced |
| `0x209d8` | `Dither` | Render postprocessing option | Proven key/use class |
| `0x209e8` | `UpscaleSmooth` | Scaling filter option | Proven key/use class |
| `0x20a08` | `sRGBGammaAware` | OpenGL sRGB/gamma-aware rendering, not ICC profile loading | Proven key; use inferred from GL capability strings/control flow |
| `0x20a28` | `10bitRendering` | 10-bit output mode | Proven key/use class |
| `0x20a48` | `10bitRenderingFix` | 10-bit output workaround | Proven key; detailed effect not traced |
| `0x20a70` | `ChromaLevels` | Numeric chroma-level parameter stored at `0x1800233cc` | Proven |
| `0x20a90` | `PostProcessing` | Bitmask stored at `0x1800233fc`; supplied on the YUV path at `0x18001988b` and used in RGB postprocessing | Proven |

`AdvSettings` exists at RVA `0x20570` as the advanced-settings UI label;
`PictureView` at RVA `0x204a8` is the plug-in name. Neither supplies an ICC
condition. Other non-render keys exist, but none is referenced by the decode
object's profile offsets.

## Dynamic loading and symbol resolution

This inventory is exhaustive for direct `LoadLibraryExW` and `GetProcAddress`
calls in `0PictureView.dll`. There are seven `LoadLibraryExW` call sites and 45
`GetProcAddress` call sites. The PE has no delay-import directory.

### `LoadLibraryExW`

| Call site | Module/path | Confidence |
|---:|---|---|
| `0x180007bff` | ACDSee IDP module path constructed beside PictureView | Proven; concrete module name supplied by caller |
| `0x180007c26` | Same ACDSee IDP module, normal search fallback | Proven |
| `0x18000808f` | `GdiPlus.dll` beside PictureView | Proven |
| `0x1800080b7` | `GdiPlus.dll`, normal search fallback | Proven |
| `0x180008eb5` | `libgfl340.dll` beside PictureView | Proven |
| `0x180008ed9` | `libgfl340.dll`, normal search fallback | Proven |
| `0x180018ffe` | Each path returned by the `*.pvd` enumeration | Proven |

The ACDSee integration contains `ID_ICO.apl` as one visible candidate, but the
loader routine itself accepts a caller-supplied IDP module name.

### Win32 `GetProcAddress`

| Call site(s) | Resolved names | Confidence |
|---|---|---|
| `0x180007c46`, `7c61`, `7c7c`, `7c97`, `7cb2`, `7ccd` | `IDP_Init`, `IDP_OpenImage`, `IDP_CloseImage`, `IDP_GetImageInfo`, `IDP_GetPageInfo`, `IDP_PageDecode` | Proven |
| `0x1800080d4`, `80e9`, `80fe`, `8113`, `8128`, `813d`, `8152`, `8167`, `817c`, `8191`, `81a6`, `81be`, `81d6`, `81ee`, `8206`, `821e`, `8236` | `GdiplusStartup`, `GdiplusShutdown`, `GdipCreateBitmapFromFile`, `GdipCreateBitmapFromStream`, `GdipGetImageWidth`, `GdipGetImageHeight`, `GdipGetImagePixelFormat`, `GdipBitmapLockBits`, `GdipBitmapUnlockBits`, `GdipDisposeImage`, `GdipImageGetFrameCount`, `GdipImageSelectActiveFrame`, `GdipGetPropertyItemSize`, `GdipGetPropertyItem`, `GdipGetImageRawFormat`, `GdipGetImagePalette`, `GdipGetImagePaletteSize` | Proven |
| `0x180008ef6`, `8f0b`, `8f20`, `8f35`, `8f4a`, `8f5f`, `8f74`, `8f89`, `8f9e`, `8fb3`, `8fc8`, `8fdd`, `8ff2` | `gflLibraryInit`, `gflLibraryExit`, `gflEnableLZW`, `gflGetDefaultLoadParams`, `gflFreeBitmap`, `gflFreeFileInformation`, `gflGetFileInformationFromMemory`, `gflLoadBitmapFromMemory`, `gflSetPluginsPathnameW`, `gflGetFileInformationW`, `gflLoadBitmapW`, `gflBitmapGetEXIF2`, `gflFreeEXIF2` | Proven |
| `0x180012795` | `SetGestureConfig`, from `GetModuleHandleW("user32.dll")` | Proven |
| `0x1800190a9`, `190bd`, `190d1`, `190e5`, `190f9`, `1910d`, `19121`, `19135` | `pvdInit`, `pvdExit`, `pvdPluginInfo`, `pvdFileOpen`, `pvdPageInfo`, `pvdPageDecode`, `pvdPageFree`, `pvdFileClose` | Proven |

The PVD loader rejects a module unless all eight addresses are non-null. Its
function-record offsets place `pvdPageDecode` at `+0x30` and `pvdPageFree` at
`+0x38`, matching the dispatch shown above.

### `wglGetProcAddress`

For completeness, OpenGL extension resolution uses a separate imported API,
`wglGetProcAddress`, at 30 call sites. These cannot conceal ICC handling either:

| Call site(s) | Resolved names | Confidence |
|---|---|---|
| `0x180001eec`, `1f39`, `1f70`, `1fd0` | `wglGetExtensionsStringARB`, `wglChoosePixelFormatARB`, `wglCreateContextAttribsARB`, `wglGetPixelFormatAttribivARB` | Proven |
| `0x180002732`, `274b`, `2764`, `277d`, `27c5` | `glGenFramebuffersEXT`, `glDeleteFramebuffersEXT`, `glBindFramebufferEXT`, `glFramebufferTexture2DEXT`, `glActiveTextureARB` | Proven |
| `0x180002817`, `282b`, `283f`, `2853`, `2867` | `glCreateTextures`, `glBindTextureUnit`, `glTextureParameteri`, `glTextureSubImage2D`, `glTextureStorage2D` | Proven |
| `0x1800028cc`, `28e0`, `28f4`, `292e` | `glBindMultiTextureEXT`, `glTextureParameteriEXT`, `glTextureSubImage2DEXT`, `glTextureStorage2DEXT` | Proven |
| `0x18000298e`, `29ab`, `29c8`, `29e1`, `2a00`, `2a19` | `glGenBuffersARB`, `glDeleteBuffersARB`, `glBindBufferARB`, `glBufferDataARB`, `glMapBufferARB`, `glUnmapBufferARB` | Proven |
| `0x180002a58` | `wglSwapIntervalEXT` | Proven |
| `0x180002bcd`, `2be1`, `2bf5`, `2c09`, `2c1d` | `glBindProgramARB`, `glDeleteProgramsARB`, `glGenProgramsARB`, `glProgramStringARB`, `glProgramLocalParameter4fvARB` | Proven |

## GDI/presentation answer

There is no GDI pixel-upload or drawing fallback in this x64 host:

- its only GDI32 imports are `DeleteEnhMetaFile`, `ChoosePixelFormat`,
  `SetPixelFormat`, and `SwapBuffers`;
- it imports none of `BitBlt`, `StretchBlt`, `SetDIBitsToDevice`, `StretchDIBits`,
  `CreateDIBSection`, or equivalent raster presentation functions;
- the exhaustive `GetProcAddress` list contains no such function;
- the dynamically loaded GDI+ set contains decode/metadata/lock-bits functions
  but no `GdipDrawImage` or graphics-context function; and
- both the RGB entry at `0x180013650` and YUV entries at
  `0x1800042b0/0x1800044f0/0x1800048c0` feed textures and OpenGL fragment
  programs.

Thus there is no separate “GDI behavior” for 48/64 bpp to document. Those values
are accepted only by the recovered OpenGL texture-upload path. If OpenGL setup
fails, no alternate GDI raster renderer was found.

## Confidence limits and unresolved details

| Item | Status | Confidence |
|---|---|---|
| x64 object extent and every host-read extended offset | Settled: 0x58 bytes; reads at `+0x30/+0x38/+0x40/+0x48/+0x4c/+0x50`; never `+0x20/+0x28` | Proven |
| ICC handling in 2021.4.19 x64 | Settled: producer-only, ignored by host | Proven |
| Exact ABI on x86 or another release | Not examined; do not extrapolate pointer layout or behavior | Unknown |
| Symbolic names for all YUV control bits | Storage, plane-count, subsampling, and sampling-offset effects are settled; some matrix/range/fourth-plane names are inferred from producer behavior | Mixed, marked in tables |
| Meaning of transform codes 8-15 | WIC reserves/emits a nibble, but shipped EXIF mapping only establishes 0-7 | Unknown |
| Reserved bytes `+0x2c` and `+0x54` | Zeroed and unread in this host; no shipped producer meaning found | Host behavior proven; future meaning unknown |

## Static-analysis record

The evidence was gathered with the installed LLVM 19 tools and the supplied full
Intel-syntax disassembly. Principal command forms were:

```text
rtk "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-readobj.exe" --file-headers --coff-imports <binary>
rtk "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-readobj.exe" --sections <binary>
rtk "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-readobj.exe" --coff-exports <decoder.pvd>
rtk "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-objdump.exe" -d --x86-asm-syntax=intel --no-show-raw-insn --start-address=<va> --stop-address=<va> <binary>
rtk "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-strings.exe" --bytes=4 <binary>
rtk rg -n -C <count> <pattern> <supplied-host.asm>
```

Targeted decoder ranges were BMP `0x180001560..0x180001860`, WebP
`0x1800143b0..0x1800144bd`, JpegRec `0x18000fb00..0x18000fd83`, BPG
`0x180018a60..0x1800192d7`, TIFF `0x18004c4e0..0x18004d73b`, FLIF
`0x18008fdd0..0x180090000`, and WIC `0x1800015b0..0x180001801`. Small table and
floating-point constants were read directly from mapped PE section bytes; no
file was changed.

No build, tests, coverage run, lint run, or import/export validation of pvdkit was
performed, because this task expressly prohibited building and changing existing
files. The static host import result is recorded above. This note is the only file
added by this analysis.
