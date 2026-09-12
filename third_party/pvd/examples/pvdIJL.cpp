#include <windows.h>
#include "../PictureViewPlugin.h"
#include "ijl.h"

#pragma comment(lib, "ijl15.lib")

HANDLE hMemoryHeap;

UINT32 __stdcall pvdInit(void)
{
	hMemoryHeap = GetProcessHeap();
	return PVD_CURRENT_INTERFACE_VERSION;
}

void __stdcall pvdExit(void)
{
}

void __stdcall pvdPluginInfo(pvdInfoPlugin *pPluginInfo)
{
	pPluginInfo->Priority = 7;
	pPluginInfo->pName = "IJL";
	pPluginInfo->pVersion = "1.0";
	pPluginInfo->pComments = "Interface for Intel JPEG Library v1.5";
}

BOOL __stdcall pvdFileOpen(const char *pFileName, INT64 lFileSize, const BYTE *pBuf, UINT32 lBuf, pvdInfoImage *pImageInfo, void **ppContext)
{
	if (lBuf >= 0x20 && (*(UINT32*)pBuf & 0x00FFFFFF) == 0xFFD8FF && lFileSize)
	{
		JPEG_CORE_PROPERTIES *pjcprops = (JPEG_CORE_PROPERTIES*)HeapAlloc(hMemoryHeap, 0, sizeof(JPEG_CORE_PROPERTIES) + 0x100);
		if (ijlInit(pjcprops) == IJL_OK)
		{
			wchar_t NameBuf[MAX_PATH], ShortNameBuf[MAX_PATH];
			char ShortName[MAX_PATH];
			if (MultiByteToWideChar(CP_UTF8, 0, pFileName, -1, NameBuf, MAX_PATH))
			{
				const DWORD i = GetShortPathNameW(NameBuf, ShortNameBuf, MAX_PATH);
				if (WideCharToMultiByte(CP_OEMCP, 0, i && i < MAX_PATH ? ShortNameBuf : NameBuf, -1, ShortName, MAX_PATH, NULL, NULL))
				{
					pjcprops->JPGFile = ShortName;
					pjcprops->jprops.jpeg_comment = (char*)(pjcprops + 1);
					pjcprops->jprops.jpeg_comment_size = 0x100;
					//pjcprops->jprops.dcttype = IJL_AAN;
					if (ijlRead(pjcprops, IJL_JFILE_READPARAMS) == IJL_OK)
					{
						pImageInfo->nPages = 1;
						pImageInfo->Flags = 0;
						pImageInfo->pFormatName = "JPEG";
						pImageInfo->pCompression = "JPEG";
						if (pjcprops->jprops.jpeg_comment_size != 0x100)
						{
							((char*)(pjcprops + 1))[pjcprops->jprops.jpeg_comment_size] = 0;
							pImageInfo->pComments = (char*)(pjcprops + 1);
						}
						else
							pImageInfo->pComments = NULL;
						*ppContext = pjcprops;
						return TRUE;
					}
				}
			}
			ijlFree(pjcprops);
		}
		HeapFree(hMemoryHeap, 0, pjcprops);
	}
	return FALSE;
}

BOOL __stdcall pvdPageInfo(void *pContext, UINT32 iPage, pvdInfoPage *pPageInfo)
{
	if (!iPage)
	{
		pPageInfo->lWidth  = ((JPEG_CORE_PROPERTIES*)pContext)->JPGWidth;
		pPageInfo->lHeight = ((JPEG_CORE_PROPERTIES*)pContext)->JPGHeight;
		pPageInfo->nBPP    = ((JPEG_CORE_PROPERTIES*)pContext)->JPGChannels * 8;
		return TRUE;
	}
	return FALSE;
}

BOOL __stdcall pvdPageDecode(void *pContext, UINT32 iPage, pvdInfoDecode *pDecodeInfo, pvdDecodeCallback DecodeCallback, void *pDecodeCallbackContext)
{
	if (!iPage)
		if (pDecodeInfo->pImage = (BYTE*)HeapAlloc(hMemoryHeap, 0, ((JPEG_CORE_PROPERTIES*)pContext)->JPGWidth * ((JPEG_CORE_PROPERTIES*)pContext)->JPGHeight * ((JPEG_CORE_PROPERTIES*)pContext)->JPGChannels))
		{
			((JPEG_CORE_PROPERTIES*)pContext)->DIBBytes  = pDecodeInfo->pImage;
			((JPEG_CORE_PROPERTIES*)pContext)->DIBWidth  = ((JPEG_CORE_PROPERTIES*)pContext)->JPGWidth;
			((JPEG_CORE_PROPERTIES*)pContext)->DIBHeight = ((JPEG_CORE_PROPERTIES*)pContext)->JPGHeight;
			((JPEG_CORE_PROPERTIES*)pContext)->DIBColor  = ((JPEG_CORE_PROPERTIES*)pContext)->JPGColor == IJL_G ? IJL_G : IJL_BGR;
			pDecodeInfo->nBPP = ((JPEG_CORE_PROPERTIES*)pContext)->JPGChannels * 8;
			pDecodeInfo->lImagePitch = ((JPEG_CORE_PROPERTIES*)pContext)->JPGWidth * ((JPEG_CORE_PROPERTIES*)pContext)->JPGChannels;
			pDecodeInfo->pPalette = NULL;
			pDecodeInfo->nColorsUsed = 0;
			if (ijlRead((JPEG_CORE_PROPERTIES*)pContext, IJL_JFILE_READWHOLEIMAGE) == IJL_OK)
				return TRUE;
			HeapFree(hMemoryHeap, 0, pDecodeInfo->pImage);
		}
	return FALSE;
}

void __stdcall pvdPageFree(void *pContext, pvdInfoDecode *pDecodeInfo)
{
	HeapFree(hMemoryHeap, 0, pDecodeInfo->pImage);
}

void __stdcall pvdFileClose(void *pContext)
{
	ijlFree((JPEG_CORE_PROPERTIES*)pContext);
	HeapFree(hMemoryHeap, 0, pContext);
}
