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

extern "C" {
#include "../../third_party/pvd/PictureViewPlugin.h"
}
