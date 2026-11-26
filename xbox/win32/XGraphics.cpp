// XGraphics for the Windows build: buffers, format queries, and saving a
// surface as a .bmp or a texture as an .xpr. XGAssembleShader keeps the
// program's source text; D3DDevice_CreateVertexShader (XD3D8.cpp) looks at
// it to recognise the font's.
#include <xtl.h>

#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "UxWin.h"
#include "XFiles.h"
#include "XPixels.h"

extern "C" {

HRESULT WINAPI XGBufferCreate(DWORD numBytes, LPXGBUFFER *buffer)
{
	XGBuffer *b = (XGBuffer *)malloc(sizeof(XGBuffer));
	if (b == NULL)
		return E_OUTOFMEMORY;
	b->refCount = 1;
	b->size = numBytes;
	b->pData = calloc(1, numBytes ? numBytes : 1);
	if (b->pData == NULL)
	{
		free(b);
		return E_OUTOFMEMORY;
	}
	*buffer = b;
	return S_OK;
}

ULONG WINAPI XGBuffer_AddRef(XGBuffer *buffer)
{
	return ++buffer->refCount;
}

ULONG WINAPI XGBuffer_Release(XGBuffer *buffer)
{
	ULONG count = --buffer->refCount;
	if (count == 0)
	{
		free(buffer->pData);
		free(buffer);
	}
	return count;
}

LPVOID WINAPI XGBuffer_GetBufferPointer(XGBuffer *buffer)
{
	return buffer->pData;
}

DWORD WINAPI XGBuffer_GetBufferSize(XGBuffer *buffer)
{
	return buffer->size;
}

HRESULT WINAPI XGAssembleShader(LPCSTR, LPCVOID source, UINT length, DWORD, LPXGBUFFER *constants,
                                LPXGBUFFER *compiled, LPXGBUFFER *errors, LPXGBUFFER *listing,
                                SASM_ResolverCallback, LPVOID, LPDWORD type)
{
	if (constants != NULL)
		*constants = NULL;
	if (errors != NULL)
		*errors = NULL;
	if (listing != NULL)
		*listing = NULL;
	if (type != NULL)
		*type = 0;
	if (compiled != NULL)
	{
		if (FAILED(XGBufferCreate(length + 1, compiled)))
			return E_OUTOFMEMORY;
		if (source != NULL)
			memcpy((*compiled)->pData, source, length);
	}
	return S_OK;
}

BOOL WINAPI XGIsSwizzledFormat(D3DFORMAT format)
{
	UINT bits;
	bool swizzled, linear, known;
	int dxt;
	UxFormatTraits(format, &bits, &swizzled, &linear, &dxt, &known);
	return swizzled;
}

DWORD WINAPI XGBytesPerPixelFromFormat(D3DFORMAT format)
{
	UINT bits;
	bool swizzled, linear, known;
	int dxt;
	UxFormatTraits(format, &bits, &swizzled, &linear, &dxt, &known);
	return bits / 8;
}

HRESULT WINAPI XGWriteSurfaceToFile(IDirect3DSurface8 *surface, const char *path)
{
	if (surface == NULL || path == NULL || surface->Data == 0)
		return E_FAIL;
	UxPixelInfo info;
	UxDescribeContainer(surface, &info);
	if (!info.known)
		return E_FAIL;
	std::vector<DWORD> argb;
	UxDecodeLevel(info, 0, (const BYTE *)(ULONG_PTR)surface->Data, argb);
	std::string pc = UxMapPath(path);
	return UxWriteBmp(pc.c_str(), (const ux_u32 *)&argb[0], info.width, info.height) ? S_OK : E_FAIL;
}

// An .xpr with one texture: the XPR0 header, the resource's header, the
// end marker, padding to 2 KB, then the bits.
HRESULT WINAPI XGWriteSurfaceOrTextureToXPR(IDirect3DResource8 *resource, const char *path, BOOL)
{
	if (resource == NULL || path == NULL || resource->Data == 0)
		return E_FAIL;
	D3DPixelContainer *container = (D3DPixelContainer *)resource;
	UxPixelInfo info;
	UxDescribeContainer(container, &info);
	if (!info.known)
		return E_FAIL;
	DWORD type = resource->Common & D3DCOMMON_TYPE_MASK;
	DWORD dataSize = type == D3DCOMMON_TYPE_TEXTURE ? UxTotalSize(info) : UxLevelSize(info, 0);
	const DWORD headerSize = 2048;
	std::vector<BYTE> file(headerSize + dataSize, 0);
	DWORD *h = (DWORD *)&file[0];
	h[0] = 0x30525058;   // "XPR0"
	h[1] = headerSize + dataSize;
	h[2] = headerSize;
	h[3] = 1 | D3DCOMMON_TYPE_TEXTURE;   // Common: written as a texture
	h[4] = 0;                            // Data: offset into the data
	h[5] = 0;                            // Lock
	h[6] = container->Format;
	h[7] = container->Size;
	h[8] = 0xFFFFFFFF;
	memcpy(&file[headerSize], (const void *)(ULONG_PTR)resource->Data, dataSize);

	std::string pc = UxMapPath(path);
	HANDLE f = CreateFileW(std::wstring(pc.begin(), pc.end()).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
	                       FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return E_FAIL;
	DWORD written = 0;
	BOOL ok = WriteFile(f, &file[0], (DWORD)file.size(), &written, NULL);
	CloseHandle(f);
	return ok && written == file.size() ? S_OK : E_FAIL;
}

}   // extern "C"
