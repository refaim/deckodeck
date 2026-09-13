#pragma once

#include <cstddef>

// The PictureView SDK header uses Win32 fixed-width typedefs (BOOL, BYTE, UINT32, INT32, INT64)
// and __stdcall, so it needs <Windows.h> before it and a C linkage block around it. This header
// does that dance exactly once for the marshalling layer, the exports and the tests.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

extern "C"
{
#include "../../third_party/pvd/PictureViewPlugin.h"
}

// Undocumented by the public SDK: BMP.pvd from the PictureView 2021.4.19 distribution sets
// decoded-image flag bit 2 when its 32-bit BMP alpha mask makes the alpha channel meaningful.
#define PVD_IDF_ALPHA 2

// Undocumented by the public SDK: BMP.pvd x64 from the PictureView 2021.4.19 distribution sets
// decoded-image flag bit 4 for a BITMAPV5 embedded ICC profile, then writes the profile address at
// offset 0x20 and its 32-bit byte count at offset 0x28 of the host-provided decode structure.
#define PVD_IDF_ICC_PROFILE 4

struct pvdInfoDecodeEx
{
    BYTE *pImage;
    UINT32 *pPalette;
    UINT32 Flags;
    UINT32 nBPP;
    UINT32 nColorsUsed;
    INT32 lImagePitch;
    const BYTE *pIccProfile;
    UINT32 cbIccProfile;
};

#if defined(_WIN64)
static_assert(offsetof(pvdInfoDecodeEx, pImage) == offsetof(pvdInfoDecode, pImage));
static_assert(offsetof(pvdInfoDecodeEx, lImagePitch) == offsetof(pvdInfoDecode, lImagePitch));
static_assert(sizeof(pvdInfoDecode) == 0x20);
static_assert(offsetof(pvdInfoDecodeEx, pIccProfile) == 0x20);
static_assert(offsetof(pvdInfoDecodeEx, cbIccProfile) == 0x28);
#else
// The x86 extension layout has not been verified against PictureView and is never written.
#endif
