// Image files for D3DX on the Windows build (see UxWin.h): the Windows
// Imaging Component decodes PNG, JPEG, BMP, GIF and TIFF.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>

#include "UxWin.h"

static IWICImagingFactory *Factory()
{
	static IWICImagingFactory *s_factory;
	if (s_factory == NULL)
	{
		CoInitializeEx(NULL, COINIT_MULTITHREADED);
		CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
		                 (void **)&s_factory);
	}
	return s_factory;
}

int UxDecodeImage(const void *data, size_t size, ux_u32 *width, ux_u32 *height, ux_u32 **argb)
{
	*argb = NULL;
	IWICImagingFactory *factory = Factory();
	if (factory == NULL || data == NULL || size == 0)
		return 0;
	// (Each thread that decodes needs COM.)
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	int ok = 0;
	IWICStream *stream = NULL;
	IWICBitmapDecoder *decoder = NULL;
	IWICBitmapFrameDecode *frame = NULL;
	IWICFormatConverter *converter = NULL;
	if (SUCCEEDED(factory->CreateStream(&stream)) &&
	    SUCCEEDED(stream->InitializeFromMemory((BYTE *)data, (DWORD)size)) &&
	    SUCCEEDED(factory->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnDemand, &decoder)) &&
	    SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
	    SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.0,
	                                    WICBitmapPaletteTypeCustom)))
	{
		UINT w = 0, h = 0;
		converter->GetSize(&w, &h);
		if (w > 0 && h > 0 && w <= 16384 && h <= 16384)
		{
			ux_u32 *pixels = (ux_u32 *)malloc((size_t)w * h * 4);
			if (pixels != NULL && SUCCEEDED(converter->CopyPixels(NULL, w * 4, w * h * 4, (BYTE *)pixels)))
			{
				*width = w;
				*height = h;
				*argb = pixels;
				ok = 1;
			}
			else
			{
				free(pixels);
			}
		}
	}
	if (converter != NULL)
		converter->Release();
	if (frame != NULL)
		frame->Release();
	if (decoder != NULL)
		decoder->Release();
	if (stream != NULL)
		stream->Release();
	return ok;
}

int UxWriteBmp(const char *path, const ux_u32 *argb, ux_u32 width, ux_u32 height)
{
	FILE *f = fopen(path, "wb");
	if (f == NULL)
		return 0;
	BITMAPFILEHEADER file = {0};
	BITMAPINFOHEADER info = {0};
	info.biSize = sizeof(info);
	info.biWidth = (LONG)width;
	info.biHeight = (LONG)height;   // bottom-up
	info.biPlanes = 1;
	info.biBitCount = 32;
	info.biCompression = BI_RGB;
	info.biSizeImage = width * height * 4;
	file.bfType = 0x4D42;
	file.bfOffBits = sizeof(file) + sizeof(info);
	file.bfSize = file.bfOffBits + info.biSizeImage;
	fwrite(&file, sizeof(file), 1, f);
	fwrite(&info, sizeof(info), 1, f);
	for (ux_u32 y = height; y-- > 0;)
		fwrite(argb + (size_t)y * width, 4, width, f);
	fclose(f);
	return 1;
}
