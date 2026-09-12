#include <windows.h>
#include "../PictureViewPlugin.h"

typedef __int32 i32;
typedef __int64 i64;
typedef unsigned __int8 u8;
typedef unsigned __int16 u16;
typedef DWORD u32;

struct FileMap
{
	HANDLE hFile, hMapping;
	i64 lSize;
	const u8 *pMapping;

	const u8 *Open(const char *pName)
	{
		wchar_t NameBuf[MAX_PATH];
		hMapping = INVALID_HANDLE_VALUE;
		pMapping = NULL;
		if (MultiByteToWideChar(CP_UTF8, 0, pName, -1, NameBuf, MAX_PATH))
			if ((hFile = CreateFileW(NameBuf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) != INVALID_HANDLE_VALUE)
			{
				*(u32*)&lSize = GetFileSize(hFile, (u32*)&lSize + 1);
				if ((u32)lSize == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
					lSize = 0;
				if (hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL))
					pMapping = (u8*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
				else
					CloseHandle(hFile), hFile = INVALID_HANDLE_VALUE;
			}
		return pMapping;
	}
	void Close(void)
	{
		if (hFile != INVALID_HANDLE_VALUE)
		{
			if (pMapping)
				UnmapViewOfFile(pMapping);
			if (hMapping != INVALID_HANDLE_VALUE)
				CloseHandle(hMapping);
			CloseHandle(hFile);
		}
	}
};

UINT32 __stdcall pvdInit(void)
{
	return PVD_CURRENT_INTERFACE_VERSION;
}

void __stdcall pvdExit(void)
{
}

void __stdcall pvdPluginInfo(pvdInfoPlugin *pPluginInfo)
{
	pPluginInfo->Priority = 7;
	pPluginInfo->pName = "BMP Decoder";
	pPluginInfo->pVersion = "1.0";
	pPluginInfo->pComments = "PictureView demo plugin";
}

BOOL __stdcall pvdFileOpen(const char *pFileName, INT64 lFileSize, const BYTE *pBuf, UINT32 lBuf, pvdInfoImage *pImageInfo, void **ppContext)
{
	if (lBuf >= 0x38 && *(u16*)pBuf == 'MB' && *(u32*)(pBuf + 0x0A) >= 0x36 && *(u32*)(pBuf + 0x0A) <= 0x436 && *(u32*)(pBuf + 0x0E) == 0x28 && !pBuf[0x1D] && !*(u32*)(pBuf + 0x1E))
	{
		FileMap *pFile = (FileMap*) HeapAlloc(GetProcessHeap(), 0, sizeof(FileMap));
		if (!lFileSize)
		{
			pFile->hFile = INVALID_HANDLE_VALUE;
			pFile->pMapping = pBuf;
			pFile->lSize = lBuf;
		}
		else if (!pFile->Open(pFileName) || pFile->lSize < 0x38)
		{
			pFile->Close();
			HeapFree(GetProcessHeap(), 0, pFile);
			return FALSE;
		}
		pImageInfo->nPages = 1;
		pImageInfo->Flags = 0;
		pImageInfo->pFormatName = "BMP";
		pImageInfo->pCompression = NULL;
		pImageInfo->pComments = NULL;
		*ppContext = pFile;
		return TRUE;
	}
	return FALSE;
}

BOOL __stdcall pvdPageInfo(void *pContext, UINT32 iPage, pvdInfoPage *pPageInfo)
{
	if (!iPage)
	{
		const u8 *const p = ((FileMap*)pContext)->pMapping;
		pPageInfo->lWidth = *(u32*)(p + 0x12);
		pPageInfo->lHeight = abs(*(i32*)(p + 0x16));
		pPageInfo->nBPP = *(u16*)(p + 0x1C);
		return TRUE;
	}
	return FALSE;
}

BOOL __stdcall pvdPageDecode(void *pContext, UINT32 iPage, pvdInfoDecode *pDecodeInfo, pvdDecodeCallback DecodeCallback, void *pDecodeCallbackContext)
{
	if (!iPage)
	{
		u8 *const p = (u8*)((FileMap*)pContext)->pMapping;
		pDecodeInfo->pImage = p + *(u32*)(p + 0x0A);
		pDecodeInfo->pPalette = (UINT32*)(p + 0x36);
		pDecodeInfo->Flags = PVD_IDF_READONLY;
		pDecodeInfo->nBPP = *(u16*)(p + 0x1C);
		pDecodeInfo->nColorsUsed = *(u32*)(p + 0x2E);
		pDecodeInfo->lImagePitch = *(i32*)(p + 0x16) < 0 ? (*(u32*)(p + 0x12) * *(u16*)(p + 0x1C) + 0x1F & ~0x1F) / 8 : -(i32)((*(u32*)(p + 0x12) * *(u16*)(p + 0x1C) + 0x1F & ~0x1F) / 8);
		return TRUE;
	}
	return FALSE;
}

void __stdcall pvdPageFree(void *pContext, pvdInfoDecode *pDecodeInfo)
{
}

void __stdcall pvdFileClose(void *pContext)
{
	((FileMap*)pContext)->Close();
	HeapFree(GetProcessHeap(), 0, pContext);
}
