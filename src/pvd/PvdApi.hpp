#pragma once

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

// Undocumented by the public SDK: PictureView consumes Flags >> 4 as its orientation code.
// Provenance and the EXIF-to-code table: docs/host/pictureview-abi.md, "Decode flags".
#define PVD_IDF_ORIENTATION_SHIFT 4
