# PictureView 2026.09.30 x64: ICC, UTF-16 and RGB behavior

This note records static analysis of one unmodified host binary on 2026-10-10.
It is a binary-specific interoperability finding, not a source-level guarantee
for other releases or architectures. No host was executed, no GUI comparison was
performed, and no installed binary or setting was changed. The older host is
documented separately in [the 2021.4.19 note](pictureview-abi.md).

## Binary and SDK identity

- Host examined: `C:\Tools\FarManager\Plugins\PictureView\0PictureView.dll`.
- File version: `3.2026.9.30`; distribution readme: `Version 3, build 2026.09.30`.
- Size: 154624 bytes; architecture: COFF x86-64 / AMD64.
- SHA256: `F9EE0E3B78A8542E077BBE7A54AC664F45744101795A5FBF1CA3AA48DFF58D11`.
- PE timestamp: `2026-09-30 12:55:28` (`0x6abd06c0`).
- Preferred image base, used for every VA below: `0x180000000`.
- Author's SDK examined: `C:\Users\Roma\Desktop\PictureViewPlugin.h`, v1.1
  dated 2026.09.30, SHA256
  `3DE27BB331EF0E5019AABE712C867C487F1E433F492B37CABC9CD7391896EEE9`.
  The header still defines `PVD_CURRENT_INTERFACE_VERSION` as 1. It names
  `PVD_IDF_ICC` as 4 and appends `pICC` / `lICC` to `pvdInfoDecode`.

## Conclusion and complete data-flow proof

This host provides space for the ICC transport fields, but **never reads them**.
Consequently it cannot dereference, parse, compare, retain, or pass the supplied
ICC profile to a color-management implementation. Publishing the SDK's
`Flags |= PVD_IDF_ICC`, `pICC`, and `lICC` therefore has no color-management
effect in this exact binary. A decoder should nevertheless preserve the
published storage until `pvdPageFree`, as required for interoperable transport.

The PVD export lookup establishes the function-record offsets independently of
the older host:

```asm
180019e4f  lea  rdx, [rip + 0xa082]     ; "pvdPageDecode", VA 180023ed8
180019e59  call qword ptr [rip + 0x32f9] ; GetProcAddress
180019e5f  mov  qword ptr [rbx + 0x38], rax
180019e63  lea  rdx, [rip + 0xa07e]     ; "pvdPageFree", VA 180023ee8
180019e6d  call qword ptr [rip + 0x32e5] ; GetProcAddress
180019e73  mov  qword ptr [rbx + 0x40], rax
```

The sole PVD `pvdPageDecode` call is inside the fully inspected function
`0x18001a590..0x18001a936`. The host zeros 0x58 bytes at `rsp+0x90`; under the
x64 SDK layout, `pICC` is therefore at `rsp+0xb0` and `lICC` at `rsp+0xb8`:

```asm
18001a5e5  xor  edx, edx
18001a5e7  lea  r8d, [rdx + 0x58]
18001a5eb  lea  rcx, [rsp + 0x90]
18001a5f3  call 0x18001bdaa             ; memset thunk, IAT 18001d4b8
...
18001a616  lea  r8, [rsp + 0x90]        ; pDecodeInfo
18001a628  call qword ptr [rax + 0x38]  ; pvdPageDecode
18001a62b  test eax, eax
18001a62d  je   0x18001a8de             ; failed decode
```

All 151 instructions from the decode return at `0x18001a62b` through the free
call at `0x18001a8db` were inspected, including every RGB, indexed, gray and YUV
branch and their failure exits. The host loads individual decode fields at
offsets `+0x00`, `+0x08`, `+0x10`, `+0x14`, `+0x18`, `+0x1c`, `+0x30`, `+0x38`,
`+0x40`, `+0x48`, `+0x4c`, and `+0x54`. It never loads `+0x20` or `+0x28`,
including through a vector load or a block copy. The object address does not
escape to a helper; its only post-decode use as a whole is the decoder's own
`pvdPageFree` call. The code after that call through the return additionally
loads `+0x50` for frame time, but still never loads the profile fields.

The only uses of `Flags` discard the ICC bit: orientation shifts right by four;
the RGB and YUV alpha paths shift right by one and mask with one. No branch
tests `Flags & 4` and no helper receives the complete flags word:

```asm
18001a633  mov  eax, dword ptr [rsp + 0xa0] ; +0x10 Flags
18001a63a  shr  eax, 4
18001a63d  xor  dword ptr [rsi + 0x240], eax
...
18001a67d  mov  eax, dword ptr [rsp + 0xa0] ; YUV alpha
18001a684  shr  eax
18001a686  and  al, 1
...
18001a7c2  mov  ecx, dword ptr [rsp + 0xa0] ; RGB alpha
18001a7c9  shr  ecx
18001a7cb  and  cl, 1
...
18001a8cf  lea  rdx, [rsp + 0x90]
18001a8db  call qword ptr [rax + 0x40]      ; pvdPageFree
```

The RGB uploader at `0x180013cd0`, called at `0x18001a7fd`, receives only the
pixel pointer, pitch, bpp, alpha, palette and color count. Other post-decode
calls are the timer at `0x18001a654`, YUV preprocessing at `0x18001a6de`, texture
setup at `0x18001a7a8`, and YUV helpers at `0x18001a861`, `0x18001a8a0`,
`0x18001a8b3` and `0x18001a8c6`. None receives the profile range or the decode
object. Thus the conclusion does not rely on absence of a CMS import.

For reproduction, `.text` maps RVA `0x1000` to raw file offset `0x400`: the
decode call's file offset is `0x19a28`, the first flags read is `0x19a33`, and
the free call is `0x19cdb`. `.rdata` maps RVA `0x1d000` to raw offset `0x1b600`.

## Supporting loader and signature checks

The PE imports KERNEL32, USER32, GDI32, COMDLG32, msvcrt, VCOMP140, OPENGL32,
SHLWAPI and WINMM, with no CMS API and no delay-import directory. All references
to the `GetProcAddress` IAT slot at `0x18001d158` are **48 direct calls**. Their
literal names, checked against `.rdata`, comprise:

- Eight IDP functions at `0x180007cb6..0x180007d73`: `IDP_Init`,
  `IDP_OpenImage`, `IDP_OpenImageW`, `IDP_CloseImage`, `IDP_GetImageInfo`,
  `IDP_GetPageInfo`, `IDP_PageDecode`, `IDP_ShowPlugInDialog`.
- Seventeen GDI+ functions at `0x180008144..0x1800082a6`: `GdiplusStartup`,
  `GdiplusShutdown`, `GdipCreateBitmapFromFile`, `GdipCreateBitmapFromStream`,
  `GdipGetImageWidth`, `GdipGetImageHeight`, `GdipGetImagePixelFormat`,
  `GdipBitmapLockBits`, `GdipBitmapUnlockBits`, `GdipDisposeImage`,
  `GdipImageGetFrameCount`, `GdipImageSelectActiveFrame`, `GdipGetPropertyItemSize`,
  `GdipGetPropertyItem`, `GdipGetImageRawFormat`, `GdipGetImagePalette`,
  `GdipGetImagePaletteSize`.
- Thirteen GFL functions at `0x180008f36..0x180009032`: `gflLibraryInit`,
  `gflLibraryExit`, `gflEnableLZW`, `gflGetDefaultLoadParams`, `gflFreeBitmap`,
  `gflFreeFileInformation`, `gflGetFileInformationFromMemory`,
  `gflLoadBitmapFromMemory`, `gflSetPluginsPathnameW`, `gflGetFileInformationW`,
  `gflLoadBitmapW`, `gflBitmapGetEXIF2`, `gflFreeEXIF2`.
- `SetGestureConfig` at `0x180012934`.
- Nine PVD exports at `0x180019e09..0x180019eef`: `pvdInit`, `pvdExit`,
  `pvdPluginInfo`, `pvdPageInfo`, `pvdPageDecode`, `pvdPageFree`, `pvdFileClose`,
  `pvdFileOpenW`, and the `pvdFileOpen` fallback.

All seven `LoadLibraryExW` references are direct calls to IAT slot
`0x18001d070`: the IDP module path (`0x180007c6f`, `0x180007c96`),
`GdiPlus.dll` (`0x1800080ff`, `0x180008127`), `libgfl340.dll`
(`0x180008ef5`, `0x180008f19`), and enumerated PVD paths (`0x180019d5e`).
The 30 direct `wglGetProcAddress` calls to IAT slot `0x18001d2c0` resolve
context, framebuffer, texture, buffer and fragment-program functions, including
`glBindProgramARB`; none consumes ICC data.

ASCII strings contain no `ICC`, `acsp`, `LittleCMS`, `lcms`, `MSCMS`,
`ColorProfile`, `CreateColorTransform`, `TranslateBitmapBits`, `OpenColorProfile`
or `ColorSpace`. Full disassembly contains no immediate matching `acsp`, `rXYZ`,
`wtpt`, `rTRC` or `curv` in either byte order. These negative checks support the
data-flow proof; they do not independently rule out arbitrary static or dynamic
color-management implementations.

Unlike the older binary, this host imports `CreateDIBSection`, compatible-DC and
metafile APIs. This analysis does not establish their presentation role and does
not carry forward the older note's absence-of-GDI-rendering conclusion. Neither
x86 layout nor runtime appearance was examined.

## Wide entry selection and native RGB upload

The loader resolves `pvdFileOpenW` at `0x180019e8b..0x180019e9b` (name at
`0x180023f08`) and stores it at function-record offset `+0x28`. Only when it is
absent does `0x180019ee0..0x180019efc` resolve `pvdFileOpen` (name at
`0x180023f18`) into `+0x20`. Dispatch at `0x18001a2b1..0x18001a2e7` checks and
calls the wide function first, passing the UTF-16 path directly. The narrow
fallback converts that path using `CP_UTF8` (65001) at `0x18001a323` and calls
the `+0x20` slot at `0x18001a35e`.

The RGB upload routine at `0x180005140` dispatches on `nBPP - 1`, with an upper
bound of `0x40` checked at `0x1800051af`. Its byte-index table has RVA `0x5e14`
and its target table RVA `0x5dd4`. The relevant cases are:

| PVD `nBPP` | Stored bytes/pixel | Handler VA | OpenGL format | OpenGL sample type |
| --- | --- | --- | --- | --- |
| 25 | 3 | `0x180005232` | `GL_RGB` (`0x1907`) | `GL_UNSIGNED_BYTE` (`0x1401`) |
| 33 | 4 | `0x18000521e` | `GL_RGBA` (`0x1908`) | `GL_UNSIGNED_BYTE` (`0x1401`) |
| 49 | 6 | `0x1800055ec` | `GL_RGB` (`0x1907`) | `GL_UNSIGNED_SHORT` (`0x1403`) |
| 65 | 8 | `0x1800055ca` | `GL_RGBA` (`0x1908`) | `GL_UNSIGNED_SHORT` (`0x1403`) |

The decode-to-upload path at `0x18001a7c2..0x18001a7fd` passes the returned
`nBPP`, pixel pointer and pitch, with alpha derived only from `(Flags >> 1) & 1`.
The ordinary upload path at `0x180015769..0x180015784` forwards the format code
without converting it to a bit count or changing channel order. Thus 25/33/49/65
are format selectors; allocation and row traversal must still use 3/4/6/8 bytes.
The SDK's 16-bit samples are little-endian, matching this Windows target.

The examined 2021.4.19 host resolves only the narrow entry and has no native RGB
cases in its exhaustive format dispatch (see the older note). AVIF and RPGMVP
therefore use an explicit per-session policy: wide entry requests native RGB,
narrow entry retains BGR, and a decoder must opt in. This establishes
compatibility for these two examined host versions. It is not formal capability
negotiation: the numeric interface version remains 1, and the SDK exposes no
format-capability query. Intermediate or third-party hosts were not examined;
an unverified host that calls the wide entry is assumed to support SDK v1.1 RGB.

AVIF can request native RGB/RGBA directly from libavif 1.4.2. For 8-bit output
its RGB/BGR libyuv branches use corresponding channel kernels and UV/YVU
matrices (`reformat_libyuv.c`, lines 388–419 and 563–605). The existing 16-bit
path continues through libavif's built-in conversion (libyuv rejects non-8-bit
RGB output at line 939). It also produces RGB16 directly for RGB48, using six
bytes per pixel. RPGMVP's libspng 0.7.4 produces RGB8, RGBA8 and host-endian RGBA16;
native output omits the adapter's full-image R/B swap. For opaque 16-bit truecolor
PNG, `SPNG_FMT_PNG` produces RGB16 directly. For opaque grayscale16 it produces
one 16-bit channel, expanded backwards to RGB16 inside the same six-byte-per-pixel
allocation. Explicit RGB48 requests for shallower opaque inputs use RGB8 and the
same backwards expansion with sample multiplication by 257. The adapter still
rejects alpha/tRNS sources for RGB48 and uses RGBA16 for those sources.
No additional image buffer is required. Opaque deep and colour-presented native
output uses RGB48, saving 25% of decoded pixel storage relative to RGBA64 while
preserving all colour samples; RGBA64 retains straight alpha when present.

## Reproduction

Run from the repository root in PowerShell. LLVM was resolved through the
repository helper to VS 2022 Build Tools LLVM 19; the host installation was read
only and the full disassembly was written to `%TEMP%`:

```powershell
. .\scripts\llvm-dir.ps1
$llvmPath = Get-LlvmDir
$binaryPath = 'C:\Tools\FarManager\Plugins\PictureView\0PictureView.dll'
(Get-Item -LiteralPath $binaryPath).VersionInfo
Get-FileHash -Algorithm SHA256 -LiteralPath $binaryPath
& (Join-Path $llvmPath 'llvm-readobj.exe') --file-headers --sections --coff-imports $binaryPath
$asmPath = Join-Path $env:TEMP 'pictureview-2026-9-30-x64.asm'
& (Join-Path $llvmPath 'llvm-objdump.exe') -d --x86-asm-syntax=intel --no-show-raw-insn $binaryPath |
  Set-Content -LiteralPath $asmPath
& (Join-Path $llvmPath 'llvm-objdump.exe') -d --x86-asm-syntax=intel --no-show-raw-insn --start-address=0x18001a320 --stop-address=0x18001a940 $binaryPath
& (Join-Path $llvmPath 'llvm-strings.exe') -t x --bytes=4 $binaryPath
rg -n 'call.*\[.*\+ 0x38\]' $asmPath
rg -n '0x18001d158|0x18001d070|0x18001d2c0' $asmPath
```

No project build, test, coverage or lint run was needed for the static host
investigation. Implementation verification is recorded separately in the build
report. No custom ICC parser or transform was added.
