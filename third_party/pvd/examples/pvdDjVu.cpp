// This program is free software under the terms of the GNU General Public License version 3.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "PictureViewPlugin.h"
#include "ddjvuapi.h"

struct SContext
{
	ddjvu_context_t *ctx;
	ddjvu_document_t *doc;
	ddjvu_format_t *fmt;
	unsigned char *pImage;
	UINT32 iCurrentPage;
	UINT32 lWidth, lHeight;
};

bool handle_ddjvu_messages(ddjvu_context_t *ctx)
{
	for (const ddjvu_message_t *msg = ddjvu_message_wait(ctx);;)
	{
		ddjvu_message_tag_t tag = msg->m_any.tag;
		ddjvu_message_pop(ctx);
		if (tag == DDJVU_ERROR)
			return false;
		if (!(msg = ddjvu_message_peek(ctx)))
			return true;
	}
}

bool SelectPage(SContext *pContext, UINT32 iPage)
{
	if (pContext->iCurrentPage == iPage)
		return true;

	ddjvu_pageinfo_t info;
	for (;;)
	{
		ddjvu_status_t r = ddjvu_document_get_pageinfo(pContext->doc, iPage, &info);
		if (r == DDJVU_JOB_OK)
			break;
		if (r >= DDJVU_JOB_FAILED || !handle_ddjvu_messages(pContext->ctx))
		{
			pContext->iCurrentPage = -1;
			return false;
		}
	}

	pContext->iCurrentPage = iPage;
	pContext->lWidth = info.width;
	pContext->lHeight = info.height;
	return true;
}

UINT32 __stdcall pvdInit(void)
{
	return PVD_CURRENT_INTERFACE_VERSION;
}

void __stdcall pvdExit(void)
{
}

void __stdcall pvdPluginInfo(pvdInfoPlugin *pPluginInfo)
{
	pPluginInfo->Priority = 10;
	pPluginInfo->pName = "DjVu";
	pPluginInfo->pVersion = "1.0";
	pPluginInfo->pComments = "DjVuLibre based DjVu decoder";
}

BOOL __stdcall pvdFileOpen(const char *pFileName, INT64 lFileSize, const BYTE *pBuf, UINT32 lBuf, pvdInfoImage *pImageInfo, SContext **ppContext)
{
	ddjvu_context_t *ctx;
	if (lBuf < 0x10 || ((UINT32*)pBuf)[0] != 'T&TA' || ((UINT32*)pBuf)[1] != 'MROF' || !(ctx = ddjvu_context_create(NULL)))
		return FALSE;

	if (ddjvu_document_t *doc = ddjvu_document_create_by_filename_utf8(ctx, pFileName, TRUE))
	{
		while (!ddjvu_document_decoding_done(doc))
			if (!handle_ddjvu_messages(ctx))
				break;
			else
				if (ddjvu_format_t *fmt = ddjvu_format_create(DDJVU_FORMAT_BGR24, 0, NULL))
				{
					ddjvu_format_set_row_order(fmt, TRUE);
					if (*ppContext = (SContext*)HeapAlloc(GetProcessHeap(), 0, sizeof(**ppContext)))
					{
						(*ppContext)->ctx = ctx;
						(*ppContext)->doc = doc;
						(*ppContext)->fmt = fmt;
						(*ppContext)->iCurrentPage = -1;
						(*ppContext)->pImage = NULL;
						pImageInfo->nPages = ddjvu_document_get_pagenum(doc);
						return TRUE;
					}
					ddjvu_format_release(fmt);
				}
		ddjvu_document_release(doc);
	}
	ddjvu_context_release(ctx);
	return FALSE;
}

void __stdcall pvdFileClose(SContext *pContext)
{
	ddjvu_format_release(pContext->fmt);
	ddjvu_document_release(pContext->doc);
	ddjvu_context_release(pContext->ctx);
	HeapFree(GetProcessHeap(), 0, pContext);
}

BOOL __stdcall pvdPageInfo(SContext *pContext, UINT32 iPage, pvdInfoPage *pPageInfo)
{
	if (SelectPage(pContext, iPage))
	{
		pPageInfo->lWidth = pContext->lWidth;
		pPageInfo->lHeight = pContext->lHeight;
		pPageInfo->nBPP = 0x18;
		return TRUE;
	}
	return FALSE;
}

BOOL __stdcall pvdPageDecode(SContext *pContext, UINT32 iPage, pvdInfoDecode *pDecodeInfo, pvdDecodeCallback DecodeCallback, void *pDecodeCallbackContext)
{
	ddjvu_page_t *page = ddjvu_page_create_by_pageno(pContext->doc, iPage);
	if (!page)
		return FALSE;

	for (;;)
	{
		ddjvu_status_t r = ddjvu_page_decoding_status(page);
		if (r == DDJVU_JOB_OK)
			break;
		if (r >= DDJVU_JOB_FAILED || !handle_ddjvu_messages(pContext->ctx))
		{
			ddjvu_page_release(page);
			return false;
		}
	}
	SelectPage(pContext, iPage);

	pDecodeInfo->nBPP = 0x18;
	pDecodeInfo->lImagePitch = pContext->lWidth*3;
	size_t ImageSize = (size_t)pDecodeInfo->lImagePitch*pContext->lHeight;
	if (pDecodeInfo->pImage = pContext->pImage = (unsigned char*)VirtualAlloc(NULL, ImageSize, MEM_COMMIT|MEM_RESERVE|MEM_TOP_DOWN, PAGE_READWRITE))
	{
		ddjvu_rect_t rect = {0, 0, pContext->lWidth, pContext->lHeight};
		if (!ddjvu_page_render(page, DDJVU_RENDER_COLOR, &rect, &rect, pContext->fmt, pDecodeInfo->lImagePitch, (char*)pContext->pImage))
			memset(pContext->pImage, 0xFF, ImageSize);
		ddjvu_page_release(page);
		return TRUE;
	}
	ddjvu_page_release(page);
	return FALSE;
}

void __stdcall pvdPageFree(SContext *pContext, pvdInfoDecode *pDecodeInfo)
{
	VirtualFree(pContext->pImage, 0, MEM_RELEASE);
	pContext->pImage = NULL;
}
