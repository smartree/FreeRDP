/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * H.264 Bitmap Compression
 *
 * Copyright 2014 Mike McDonald <Mike.McDonald@software.dell.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <winpr/crt.h>
#include <winpr/print.h>
#include <winpr/bitstream.h>

#include <freerdp/codec/color.h>
#include <freerdp/codec/h264.h>

#define USE_GRAY_SCALE		0
#define USE_UPCONVERT		0
#define USE_TRACE		0

static BYTE clip(int x)
{
	if (x < 0) return 0;
	if (x > 255) return 255;
	return (BYTE)x;
}

static UINT32 YUV_to_RGB(BYTE Y, BYTE U, BYTE V)
{
	BYTE R, G, B;

#if USE_GRAY_SCALE
	/*
	 * Displays the Y plane as a gray-scale image.
	 */
	R = Y;
	G = Y;
	B = Y;
#else
	int C, D, E;

#if 0
	/*
	 * Documented colorspace conversion from YUV to RGB.
	 * See http://msdn.microsoft.com/en-us/library/ms893078.aspx
	 */

	C = Y - 16;
	D = U - 128;
	E = V - 128;

	R = clip(( 298 * C           + 409 * E + 128) >> 8);
	G = clip(( 298 * C - 100 * D - 208 * E + 128) >> 8);
	B = clip(( 298 * C + 516 * D           + 128) >> 8);
#endif

#if 0
	/*
	 * These coefficients produce better results.
	 * See http://www.microchip.com/forums/m599060.aspx
	 */

	C = Y;
	D = U - 128;
	E = V - 128;

	R = clip(( 256 * C           + 359 * E + 128) >> 8);
	G = clip(( 256 * C -  88 * D - 183 * E + 128) >> 8);
	B = clip(( 256 * C + 454 * D           + 128) >> 8);
#endif

#if 1
	/*
	 * These coefficients produce excellent results.
	 */

	C = Y;
	D = U - 128;
	E = V - 128;

	R = clip(( 256 * C           + 403 * E + 128) >> 8);
	G = clip(( 256 * C -  48 * D - 120 * E + 128) >> 8);
	B = clip(( 256 * C + 475 * D           + 128) >> 8);
#endif

#endif

	return RGB32(R, G, B);
}

#if USE_UPCONVERT
static BYTE* convert_420_to_444(BYTE* chroma420, int chroma420Width, int chroma420Height, int chroma420Stride)
{
	BYTE *chroma444, *src, *dst;
	int chroma444Width;
	int chroma444Height;
	int i, j;

	chroma444Width = chroma420Width * 2;
	chroma444Height = chroma420Height * 2;

	chroma444 = (BYTE*) malloc(chroma444Width * chroma444Height);

	if (!chroma444)
		return NULL;

	/* Upconvert in the horizontal direction. */

	for (j = 0; j < chroma420Height; j++)
	{
		src = chroma420 + j * chroma420Stride;
		dst = chroma444 + j * chroma444Width;
		dst[0] = src[0];
		for (i = 1; i < chroma420Width; i++)
		{
			dst[2*i-1] = (3 * src[i-1] + src[i] + 2) >> 2;
			dst[2*i] = (src[i-1] + 3 * src[i] + 2) >> 2;
		}
		dst[chroma444Width-1] = src[chroma420Width-1];
	}

	/* Upconvert in the vertical direction (in-place, bottom-up). */

	for (i = 0; i < chroma444Width; i++)   
	{
		src = chroma444 + i + (chroma420Height-2) * chroma444Width;
		dst = chroma444 + i + (2*(chroma420Height-2)+1) * chroma444Width;
		dst[2*chroma444Width] = src[chroma444Width];
		for (j = chroma420Height - 2; j >= 0; j--)
		{
			dst[chroma444Width] = (src[0] + 3 * src[chroma444Width] + 2) >> 2;
			dst[0] = (3 * src[0] + src[chroma444Width] + 2) >> 2;
			dst -= 2 * chroma444Width;
			src -= chroma444Width;
		}
	}

	return chroma444;
}
#endif

#if USE_TRACE
static void trace_callback(H264_CONTEXT* h264, int level, const char* message)
{
	printf("%d - %s\n", level, message);
}
#endif

static int g_H264FrameId = 0;
static BOOL g_H264DumpFrames = FALSE;

int h264_prepare_rgb_buffer(H264_CONTEXT* h264, int width, int height)
{
	UINT32 size;

	h264->width = width;
	h264->height = height;
	h264->scanline = h264->width * 4;
	size = h264->scanline * h264->height;

	if (size > h264->size)
	{
		h264->size = size;
		h264->data = (BYTE*) realloc(h264->data, h264->size);
	}

	if (!h264->data)
		return -1;

	return 1;
}

int freerdp_image_copy_yuv420p_to_xrgb(BYTE* pDstData, int nDstStep, int nXDst, int nYDst,
		int nWidth, int nHeight, BYTE* pSrcData[3], int nSrcStep[2], int nXSrc, int nYSrc)
{
	int x, y;
	BYTE* pDstPixel8;
	BYTE *pY, *pU, *pV, *pUv, *pVv;
	int temp1=0,temp2=0;

	pY = pSrcData[0];
	pUv = pU = pSrcData[1];
	pVv = pV = pSrcData[2];

	pDstPixel8 = &pDstData[(nYDst * nDstStep) + (nXDst * 4)];

	for (y = 0; y < nHeight; y++)
	{
		for (x = 0; x < nWidth; x++)
		{
/*			*((UINT32*) pDstPixel8) = RGB32(*pY, *pY, *pY);*/
			*((UINT32*) pDstPixel8) = YUV_to_RGB(*pY,*pU,*pV);
			pDstPixel8 += 4;
			pY++;
			
			if(temp1){
				temp1=0;
				pU++;
				pV++;
			}else{
				temp1=1;
			}
		}

		pDstPixel8 += (nDstStep - (nWidth * 4));
		pY += (nSrcStep[0] - nWidth);
		if(temp2){
			temp2=0;
			pU += (nSrcStep[1] - nWidth / 2);
			pV += (nSrcStep[1] - nWidth / 2);
			pUv = pU;
			pVv = pV;
		}else{
			temp2=1;
			pU = pUv;
			pV = pVv;
		}
	}

	return 1;
}

BYTE* h264_strip_nal_unit_au_delimiter(BYTE* pSrcData, UINT32* pSrcSize)
{
	BYTE* data = pSrcData;
	UINT32 size = *pSrcSize;
	BYTE forbidden_zero_bit = 0;
	BYTE nal_ref_idc = 0;
	BYTE nal_unit_type = 0;

	/* ITU-T H.264 B.1.1 Byte stream NAL unit syntax */

	while (size > 0)
	{
		if (*data)
			break;

		data++;
		size--;
	}

	if (*data != 1)
		return pSrcData;

	data++;
	size--;

	forbidden_zero_bit = (data[0] >> 7);
	nal_ref_idc = (data[0] >> 5);
	nal_unit_type = (data[0] & 0x1F);

	if (forbidden_zero_bit)
		return pSrcData; /* invalid */

	if (nal_unit_type == 9)
	{
		/* NAL Unit AU Delimiter */

		printf("NAL Unit AU Delimiter: idc: %d\n", nal_ref_idc);

		data += 2;
		size -= 2;

		*pSrcSize = size;
		return data;
	}

	return pSrcData;
}

int h264_decompress(H264_CONTEXT* h264, BYTE* pSrcData, UINT32 SrcSize,
		BYTE** ppDstData, DWORD DstFormat, int nDstStep, int nXDst, int nYDst, int nWidth, int nHeight)
{
#ifdef WITH_OPENH264
	DECODING_STATE state;
	SBufferInfo sBufferInfo;
	SSysMEMBuffer* pSystemBuffer;
	UINT32 UncompressedSize;
	BYTE* pDstData;
	BYTE* pYUVData[3];
	BYTE* pY;
	BYTE* pU;
	BYTE* pV;
	int Y, U, V;
	int i, j;

	if (!h264 || !h264->pDecoder)
		return -1;

	pSrcData = h264_strip_nal_unit_au_delimiter(pSrcData, &SrcSize);

#if 0
	printf("h264_decompress: pSrcData=%p, SrcSize=%u, pDstData=%p, nDstStep=%d, nXDst=%d, nYDst=%d, nWidth=%d, nHeight=%d)\n",
		pSrcData, SrcSize, *ppDstData, nDstStep, nXDst, nYDst, nWidth, nHeight);
#endif

	/* Allocate a destination buffer (if needed). */

	UncompressedSize = nWidth * nHeight * 4;

	if (UncompressedSize == 0)
		return -1;

	pDstData = *ppDstData;

	if (!pDstData)
	{
		pDstData = (BYTE*) malloc(UncompressedSize);

		if (!pDstData)
			return -1;

		*ppDstData = pDstData;
	}

	if (g_H264DumpFrames)
	{
		FILE* fp;
		char buf[4096];

		snprintf(buf, sizeof(buf), "/tmp/wlog/bs_%d.h264", g_H264FrameId);
		fp = fopen(buf, "wb");
		fwrite(pSrcData, 1, SrcSize, fp);
		fflush(fp);
		fclose(fp);
	}

	/*
	 * Decompress the image.  The RDP host only seems to send I420 format.
	 */

	pYUVData[0] = NULL;
	pYUVData[1] = NULL;
	pYUVData[2] = NULL;

	ZeroMemory(&sBufferInfo, sizeof(sBufferInfo));

	state = (*h264->pDecoder)->DecodeFrame2(
		h264->pDecoder,
		pSrcData,
		SrcSize,
		pYUVData,
		&sBufferInfo);


		state = (*h264->pDecoder)->DecodeFrame2(
		h264->pDecoder,
		NULL,
		0,
		pYUVData,
		&sBufferInfo);	

	pSystemBuffer = &sBufferInfo.UsrData.sSystemBuffer;

#if 0
	printf("h264_decompress: state=%u, pYUVData=[%p,%p,%p], bufferStatus=%d, width=%d, height=%d, format=%d, stride=[%d,%d]\n",
		state, pYUVData[0], pYUVData[1], pYUVData[2], sBufferInfo.iBufferStatus,
		pSystemBuffer->iWidth, pSystemBuffer->iHeight, pSystemBuffer->iFormat,
		pSystemBuffer->iStride[0], pSystemBuffer->iStride[1]);
#endif

	if (state != 0)
		return -1;

	if (!pYUVData[0] || !pYUVData[1] || !pYUVData[2])
		return -1;

	if (sBufferInfo.iBufferStatus != 1)
		return -1;

	if (pSystemBuffer->iFormat != videoFormatI420)
		return -1;

	/* Convert I420 (same as IYUV) to XRGB. */

	pY = pYUVData[0];
	pU = pYUVData[1];
	pV = pYUVData[2];

	if (g_H264DumpFrames)
	{
		FILE* fp;
		BYTE* srcp;
		char buf[4096];

		snprintf(buf, sizeof(buf), "/tmp/wlog/H264_%d.ppm", g_H264FrameId);
		fp = fopen(buf, "wb");
		fwrite("P5\n", 1, 3, fp);
		snprintf(buf, sizeof(buf), "%d %d\n", pSystemBuffer->iWidth, pSystemBuffer->iHeight);
		fwrite(buf, 1, strlen(buf), fp);
		fwrite("255\n", 1, 4, fp);

		srcp = pY;

		for (j = 0; j < pSystemBuffer->iHeight; j++)
		{
			fwrite(srcp, 1, pSystemBuffer->iWidth, fp);
			srcp += pSystemBuffer->iStride[0];
		}

		fflush(fp);
		fclose(fp);
	}


	if (h264_prepare_rgb_buffer(h264, pSystemBuffer->iWidth, pSystemBuffer->iHeight) < 0)
		return -1;

	freerdp_image_copy_yuv420p_to_xrgb(h264->data, h264->scanline, 0, 0,
			h264->width, h264->height, pYUVData, pSystemBuffer->iStride, 0, 0);

	if (g_H264DumpFrames)
	{
		FILE* fp;
		BYTE* srcp;
		char buf[4096];

		snprintf(buf, sizeof(buf), "/tmp/wlog/H264_%d_rgb.ppm", g_H264FrameId);
		fp = fopen(buf, "wb");
		fwrite("P6\n", 1, 3, fp);
		snprintf(buf, sizeof(buf), "%d %d\n", pSystemBuffer->iWidth, pSystemBuffer->iHeight);
		fwrite(buf, 1, strlen(buf), fp);
		fwrite("255\n", 1, 4, fp);

		srcp = h264->data;

		for (j = 0; j < h264->height; j++)
		{
			for(i=0;i<h264->width;i++){
				fwrite(srcp, 1, 3, fp);
				srcp += 4;
			}
		}

		fflush(fp);
		fclose(fp);
	}

	g_H264FrameId++;

	return 1;

#if USE_UPCONVERT
	/* Convert 4:2:0 YUV to 4:4:4 YUV. */
	pU = convert_420_to_444(pU, pSystemBuffer->iWidth / 2, pSystemBuffer->iHeight / 2, pSystemBuffer->iStride[1]);
	pV = convert_420_to_444(pV, pSystemBuffer->iWidth / 2, pSystemBuffer->iHeight / 2, pSystemBuffer->iStride[1]);
#endif

	for (j = 0; j < nHeight; j++)
	{
		BYTE *pXRGB = pDstData + ((nYDst + j) * nDstStep) + (nXDst * 4);
		int y = nYDst + j;

		for (i = 0; i < nWidth; i++)
		{
			int x = nXDst + i;

			Y = pY[(y * pSystemBuffer->iStride[0]) + x];
#if USE_UPCONVERT
			U = pU[(y * pSystemBuffer->iWidth) + x];
			V = pV[(y * pSystemBuffer->iWidth) + x];
#else
			U = pU[(y/2) * pSystemBuffer->iStride[1] + (x/2)];
			V = pV[(y/2) * pSystemBuffer->iStride[1] + (x/2)];
#endif

			*(UINT32*)pXRGB = YUV_to_RGB(Y, U, V);
		
			pXRGB += 4;
		}
	}

#if USE_UPCONVERT
	free(pU);
	free(pV);
#endif
#endif

	return 1;
}

int h264_compress(H264_CONTEXT* h264, BYTE* pSrcData, UINT32 SrcSize, BYTE** ppDstData, UINT32* pDstSize)
{
	return 1;
}

void h264_context_reset(H264_CONTEXT* h264)
{

}

H264_CONTEXT* h264_context_new(BOOL Compressor)
{
	H264_CONTEXT* h264;

	h264 = (H264_CONTEXT*) calloc(1, sizeof(H264_CONTEXT));

	if (h264)
	{
		h264->Compressor = Compressor;

		if (h264_prepare_rgb_buffer(h264, 256, 256) < 0)
			return NULL;

#ifdef WITH_OPENH264
		{
			static EVideoFormatType videoFormat = videoFormatI420;

#if USE_TRACE
			static int traceLevel = WELS_LOG_DEBUG;
			static WelsTraceCallback traceCallback = (WelsTraceCallback) trace_callback;
#endif

			SDecodingParam sDecParam;
			long status;

			WelsCreateDecoder(&h264->pDecoder);

			if (!h264->pDecoder)
			{
				printf("Failed to create OpenH264 decoder\n");
				goto EXCEPTION;
			}

			ZeroMemory(&sDecParam, sizeof(sDecParam));
			sDecParam.iOutputColorFormat  = videoFormatI420;
			sDecParam.uiEcActiveFlag  = 1;
			sDecParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;

			status = (*h264->pDecoder)->Initialize(h264->pDecoder, &sDecParam);

			if (status != 0)
			{
				printf("Failed to initialize OpenH264 decoder (status=%ld)\n", status);
				goto EXCEPTION;
			}

			status = (*h264->pDecoder)->SetOption(h264->pDecoder, DECODER_OPTION_DATAFORMAT, &videoFormat);

			if (status != 0)
			{
				printf("Failed to set data format option on OpenH264 decoder (status=%ld)\n", status);
			}


#if USE_TRACE
			status = (*h264->pDecoder)->SetOption(h264->pDecoder, DECODER_OPTION_TRACE_LEVEL, &traceLevel);
			if (status != 0)
			{
				printf("Failed to set trace level option on OpenH264 decoder (status=%ld)\n", status);
			}

			status = (*h264->pDecoder)->SetOption(h264->pDecoder, DECODER_OPTION_TRACE_CALLBACK, &traceCallback);
			if (status != 0)
			{
				printf("Failed to set trace callback option on OpenH264 decoder (status=%ld)\n", status);
			}

			status = (*h264->pDecoder)->SetOption(h264->pDecoder, DECODER_OPTION_TRACE_CALLBACK_CONTEXT, &h264);
			if (status != 0)
			{
				printf("Failed to set trace callback context option on OpenH264 decoder (status=%ld)\n", status);
			}
#endif
		}
#endif
			
		h264_context_reset(h264);
	}

	return h264;

EXCEPTION:
#ifdef WITH_OPENH264
	if (h264->pDecoder)
	{
		WelsDestroyDecoder(h264->pDecoder);
	}
#endif

	free(h264);

	return NULL;
}

void h264_context_free(H264_CONTEXT* h264)
{
	if (h264)
	{
		free(h264->data);

#ifdef WITH_OPENH264
		if (h264->pDecoder)
		{
			(*h264->pDecoder)->Uninitialize(h264->pDecoder);
			WelsDestroyDecoder(h264->pDecoder);
		}
#endif

		free(h264);
	}
}
