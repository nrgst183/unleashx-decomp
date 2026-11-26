// D3DX 8 for the Windows build: the matrix helpers the dash uses, and
// textures and surfaces from image files (PNG/JPEG/BMP/GIF through WIC,
// WinImage.cpp; DDS and TGA here) and from other surfaces.
#include <xtl.h>

#include <algorithm>
#include <stdlib.h>

#include <math.h>
#include <string.h>
#include <vector>

#include "UxWin.h"
#include "XFiles.h"
#include "XPixels.h"

void UxMarkDirty(D3DResource *resource);

// ---- math ----

extern "C" {

D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *out, CONST D3DXMATRIX *a, CONST D3DXMATRIX *b)
{
	D3DXMATRIX r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] +
			            a->m[i][3] * b->m[3][j];
	*out = r;
	return out;
}

D3DXMATRIX *WINAPI D3DXMatrixTranspose(D3DXMATRIX *out, CONST D3DXMATRIX *m)
{
	D3DXMATRIX r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r.m[i][j] = m->m[j][i];
	*out = r;
	return out;
}

D3DXMATRIX *WINAPI D3DXMatrixScaling(D3DXMATRIX *out, FLOAT sx, FLOAT sy, FLOAT sz)
{
	memset(out, 0, sizeof(*out));
	out->_11 = sx;
	out->_22 = sy;
	out->_33 = sz;
	out->_44 = 1.0f;
	return out;
}

D3DXMATRIX *WINAPI D3DXMatrixTranslation(D3DXMATRIX *out, FLOAT x, FLOAT y, FLOAT z)
{
	memset(out, 0, sizeof(*out));
	out->_11 = out->_22 = out->_33 = out->_44 = 1.0f;
	out->_41 = x;
	out->_42 = y;
	out->_43 = z;
	return out;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationYawPitchRoll(D3DXMATRIX *out, FLOAT yaw, FLOAT pitch, FLOAT roll)
{
	// Roll (z), then pitch (x), then yaw (y), as D3DX.
	float sy = sinf(yaw), cy = cosf(yaw), sp = sinf(pitch), cp = cosf(pitch), sr = sinf(roll), cr = cosf(roll);
	out->_11 = cr * cy + sr * sp * sy;
	out->_12 = sr * cp;
	out->_13 = sr * sp * cy - cr * sy;
	out->_14 = 0.0f;
	out->_21 = cr * sp * sy - sr * cy;
	out->_22 = cr * cp;
	out->_23 = sr * sy + cr * sp * cy;
	out->_24 = 0.0f;
	out->_31 = cp * sy;
	out->_32 = -sp;
	out->_33 = cp * cy;
	out->_34 = 0.0f;
	out->_41 = out->_42 = out->_43 = 0.0f;
	out->_44 = 1.0f;
	return out;
}

D3DXMATRIX *WINAPI D3DXMatrixLookAtLH(D3DXMATRIX *out, CONST D3DXVECTOR3 *eye, CONST D3DXVECTOR3 *at,
                                      CONST D3DXVECTOR3 *up)
{
	D3DXVECTOR3 z(at->x - eye->x, at->y - eye->y, at->z - eye->z);
	float len = sqrtf(z.x * z.x + z.y * z.y + z.z * z.z);
	if (len > 0.0f)
		z /= len;
	D3DXVECTOR3 x(up->y * z.z - up->z * z.y, up->z * z.x - up->x * z.z, up->x * z.y - up->y * z.x);
	len = sqrtf(x.x * x.x + x.y * x.y + x.z * x.z);
	if (len > 0.0f)
		x /= len;
	D3DXVECTOR3 y(z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x);
	out->_11 = x.x; out->_12 = y.x; out->_13 = z.x; out->_14 = 0.0f;
	out->_21 = x.y; out->_22 = y.y; out->_23 = z.y; out->_24 = 0.0f;
	out->_31 = x.z; out->_32 = y.z; out->_33 = z.z; out->_34 = 0.0f;
	out->_41 = -(x.x * eye->x + x.y * eye->y + x.z * eye->z);
	out->_42 = -(y.x * eye->x + y.y * eye->y + y.z * eye->z);
	out->_43 = -(z.x * eye->x + z.y * eye->y + z.z * eye->z);
	out->_44 = 1.0f;
	return out;
}

D3DXMATRIX *WINAPI D3DXMatrixPerspectiveFovLH(D3DXMATRIX *out, FLOAT fovy, FLOAT aspect, FLOAT zn, FLOAT zf)
{
	float yScale = 1.0f / tanf(fovy / 2.0f);
	float xScale = yScale / aspect;
	memset(out, 0, sizeof(*out));
	out->_11 = xScale;
	out->_22 = yScale;
	out->_33 = zf / (zf - zn);
	out->_34 = 1.0f;
	out->_43 = -zn * zf / (zf - zn);
	return out;
}

D3DXVECTOR4 *WINAPI D3DXVec3Transform(D3DXVECTOR4 *out, CONST D3DXVECTOR3 *v, CONST D3DXMATRIX *m)
{
	D3DXVECTOR4 r;
	r.x = v->x * m->_11 + v->y * m->_21 + v->z * m->_31 + m->_41;
	r.y = v->x * m->_12 + v->y * m->_22 + v->z * m->_32 + m->_42;
	r.z = v->x * m->_13 + v->y * m->_23 + v->z * m->_33 + m->_43;
	r.w = v->x * m->_14 + v->y * m->_24 + v->z * m->_34 + m->_44;
	*out = r;
	return out;
}

UINT WINAPI D3DXGetFVFVertexSize(DWORD fvf)
{
	UINT size = 0;
	switch (fvf & D3DFVF_POSITION_MASK)
	{
	case D3DFVF_XYZ: size = 12; break;
	case D3DFVF_XYZRHW: size = 16; break;
	case D3DFVF_XYZB1: size = 16; break;
	case D3DFVF_XYZB2: size = 20; break;
	case D3DFVF_XYZB3: size = 24; break;
	case D3DFVF_XYZB4: size = 28; break;
	}
	if (fvf & D3DFVF_NORMAL)
		size += 12;
	if (fvf & D3DFVF_DIFFUSE)
		size += 4;
	if (fvf & D3DFVF_SPECULAR)
		size += 4;
	UINT count = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (UINT i = 0; i < count; i++)
	{
		UINT format = (fvf >> (16 + i * 2)) & 3;
		size += format == 0 ? 8 : format == 1 ? 12 : format == 2 ? 16 : 4;
	}
	return size;
}

HRESULT WINAPI D3DXComputeBoundingSphere(PVOID points, DWORD count, DWORD fvf, D3DXVECTOR3 *center, FLOAT *radius)
{
	UINT stride = D3DXGetFVFVertexSize(fvf);
	if (points == NULL || count == 0 || stride == 0)
		return D3DERR_INVALIDCALL;
	D3DXVECTOR3 sum(0, 0, 0);
	for (DWORD i = 0; i < count; i++)
	{
		const float *p = (const float *)((const BYTE *)points + i * stride);
		sum.x += p[0];
		sum.y += p[1];
		sum.z += p[2];
	}
	sum /= (float)count;
	float r = 0.0f;
	for (DWORD i = 0; i < count; i++)
	{
		const float *p = (const float *)((const BYTE *)points + i * stride);
		float dx = p[0] - sum.x, dy = p[1] - sum.y, dz = p[2] - sum.z;
		float d = sqrtf(dx * dx + dy * dy + dz * dz);
		if (d > r)
			r = d;
	}
	*center = sum;
	*radius = r;
	return S_OK;
}

}   // extern "C"

void WINAPI D3DXSetDXT3DXT5(BOOL)
{
}

// ---- images ----

// An image as A8R8G8B8 (or, for a DDS, possibly DXT blocks to keep).
struct Image
{
	UINT width, height;
	std::vector<DWORD> argb;
	DWORD format;   // the Xbox format it'd best be: A8R8G8B8, or DXT1/3/5
	std::vector<BYTE> dxt;
};

static bool LoadDds(const BYTE *data, size_t size, Image &image)
{
	if (size < 128 || memcmp(data, "DDS ", 4) != 0)
		return false;
	const DWORD *h = (const DWORD *)(data + 4);
	UINT height = h[2], width = h[3];
	DWORD pfFlags = h[19], fourcc = h[20], bits = h[21];
	DWORD rMask = h[22], gMask = h[23], bMask = h[24], aMask = h[25];
	if (width == 0 || height == 0 || width > 8192 || height > 8192)
		return false;
	image.width = width;
	image.height = height;
	const BYTE *pixels = data + 128;
	size_t available = size - 128;
	if (pfFlags & 4)   // DDPF_FOURCC
	{
		int dxt = fourcc == MAKEFOURCC('D', 'X', 'T', '1') ? 1 : (fourcc == MAKEFOURCC('D', 'X', 'T', '2') ||
		                                                         fourcc == MAKEFOURCC('D', 'X', 'T', '3'))
		                                                            ? 3
		                                                        : (fourcc == MAKEFOURCC('D', 'X', 'T', '4') ||
		                                                           fourcc == MAKEFOURCC('D', 'X', 'T', '5'))
		                                                            ? 5
		                                                            : 0;
		if (dxt == 0)
			return false;
		UxPixelInfo info;
		DWORD format, sizeField;
		DWORD code = dxt == 1 ? 0x0C : dxt == 3 ? 0x0E : 0x0F;
		UxEncodeFormat(code, width, height, 1, &format, &sizeField);
		UxDescribePixels(format, 0, &info);
		info.width = width;
		info.height = height;
		info.levels = 1;
		if (available < UxLevelSize(info, 0))
			return false;
		UxDecodeLevel(info, 0, pixels, image.argb);
		image.format = code;
		image.dxt.assign(pixels, pixels + UxLevelSize(info, 0));
		return true;
	}
	UINT bytes = bits / 8;
	if (bytes < 2 || bytes > 4 || available < (size_t)width * height * bytes)
		return false;
	image.argb.resize((size_t)width * height);
	for (size_t i = 0; i < (size_t)width * height; i++)
	{
		DWORD v = 0;
		memcpy(&v, pixels + i * bytes, bytes);
		DWORD out = 0;
		DWORD masks[4] = {bMask, gMask, rMask, aMask};
		for (int c = 0; c < 4; c++)
		{
			DWORD m = masks[c];
			DWORD value = 255;
			if (m != 0)
			{
				int shift = 0;
				while (!((m >> shift) & 1))
					shift++;
				DWORD max = m >> shift;
				value = ((v & m) >> shift) * 255 / max;
			}
			else if (c != 3)
			{
				value = 0;
			}
			out |= value << (8 * c);
		}
		image.argb[i] = out;
	}
	image.format = 0x06;
	return true;
}

static bool LoadTga(const BYTE *data, size_t size, Image &image)
{
	if (size < 18)
		return false;
	int idLength = data[0], colorMapType = data[1], type = data[2];
	int width = data[12] | (data[13] << 8), height = data[14] | (data[15] << 8), bpp = data[16], descriptor = data[17];
	if (colorMapType != 0 || (type != 2 && type != 10 && type != 3 && type != 11) || width <= 0 || height <= 0 ||
	    (bpp != 24 && bpp != 32 && bpp != 8))
		return false;
	int bytes = bpp / 8;
	const BYTE *p = data + 18 + idLength, *end = data + size;
	image.width = width;
	image.height = height;
	image.argb.assign((size_t)width * height, 0);
	bool rle = type == 10 || type == 11;
	size_t i = 0, count = (size_t)width * height;
	DWORD pixel = 0;
	while (i < count)
	{
		int run = 1;
		bool repeat = false;
		if (rle)
		{
			if (p >= end)
				break;
			BYTE header = *p++;
			run = (header & 0x7F) + 1;
			repeat = (header & 0x80) != 0;
		}
		for (int k = 0; k < run && i < count; k++, i++)
		{
			if (!repeat || k == 0)
			{
				if (p + bytes > end)
					return false;
				if (bytes == 1)
					pixel = 0xFF000000 | (p[0] << 16) | (p[0] << 8) | p[0];
				else
					pixel = p[0] | (p[1] << 8) | (p[2] << 16) | (bytes == 4 ? ((DWORD)p[3] << 24) : 0xFF000000);
				p += bytes;
			}
			image.argb[i] = pixel;
		}
	}
	if (!(descriptor & 0x20))
	{
		// Bottom-up: flip.
		for (int y = 0; y < height / 2; y++)
			for (int x = 0; x < width; x++)
				std::swap(image.argb[(size_t)y * width + x], image.argb[(size_t)(height - 1 - y) * width + x]);
	}
	image.format = 0x06;
	return true;
}

static bool LoadImage(const void *data, size_t size, Image &image)
{
	image.dxt.clear();
	if (LoadDds((const BYTE *)data, size, image))
		return true;
	ux_u32 w = 0, h = 0, *argb = NULL;
	if (UxDecodeImage(data, size, &w, &h, &argb))
	{
		image.width = w;
		image.height = h;
		image.argb.assign(argb, argb + (size_t)w * h);
		free(argb);
		image.format = 0x06;
		return true;
	}
	return LoadTga((const BYTE *)data, size, image);
}

// Resamples (bilinear when shrinking or growing).
static void Resample(const std::vector<DWORD> &src, UINT sw, UINT sh, std::vector<DWORD> &dst, UINT dw, UINT dh)
{
	dst.resize((size_t)dw * dh);
	if (sw == dw && sh == dh)
	{
		dst = src;
		return;
	}
	for (UINT y = 0; y < dh; y++)
	{
		float fy = ((float)y + 0.5f) * sh / dh - 0.5f;
		int y0 = (int)floorf(fy);
		float ty = fy - y0;
		int y1 = y0 + 1;
		y0 = y0 < 0 ? 0 : y0 >= (int)sh ? sh - 1 : y0;
		y1 = y1 < 0 ? 0 : y1 >= (int)sh ? sh - 1 : y1;
		for (UINT x = 0; x < dw; x++)
		{
			float fx = ((float)x + 0.5f) * sw / dw - 0.5f;
			int x0 = (int)floorf(fx);
			float tx = fx - x0;
			int x1 = x0 + 1;
			x0 = x0 < 0 ? 0 : x0 >= (int)sw ? sw - 1 : x0;
			x1 = x1 < 0 ? 0 : x1 >= (int)sw ? sw - 1 : x1;
			DWORD c00 = src[(size_t)y0 * sw + x0], c10 = src[(size_t)y0 * sw + x1], c01 = src[(size_t)y1 * sw + x0],
			      c11 = src[(size_t)y1 * sw + x1];
			DWORD out = 0;
			for (int s = 0; s < 32; s += 8)
			{
				float a = ((c00 >> s) & 255) * (1 - tx) + ((c10 >> s) & 255) * tx;
				float b = ((c01 >> s) & 255) * (1 - tx) + ((c11 >> s) & 255) * tx;
				out |= (DWORD)(a * (1 - ty) + b * ty + 0.5f) << s;
			}
			dst[(size_t)y * dw + x] = out;
		}
	}
}

static UINT NextPow2(UINT v)
{
	UINT p = 1;
	while (p < v)
		p <<= 1;
	return p;
}

static void ApplyColorKey(std::vector<DWORD> &argb, D3DCOLOR key)
{
	if (key == 0)
		return;
	for (size_t i = 0; i < argb.size(); i++)
		if (argb[i] == key)
			argb[i] = 0;
}

// Writes an image into a texture's levels (mipmaps by halving).
static void FillTexture(D3DTexture *texture, const Image &image, D3DCOLOR key)
{
	UxPixelInfo info;
	UxDescribeContainer(texture, &info);
	BYTE *bits = (BYTE *)(ULONG_PTR)texture->Data;
	if (bits == NULL)
		return;
	if (!image.dxt.empty() && info.format == image.format && info.width == image.width && info.height == image.height &&
	    info.levels == 1)
	{
		memcpy(bits, &image.dxt[0], image.dxt.size() < UxLevelSize(info, 0) ? image.dxt.size() : UxLevelSize(info, 0));
		UxMarkDirty(texture);
		return;
	}
	std::vector<DWORD> level;
	Resample(image.argb, image.width, image.height, level, info.width, info.height);
	ApplyColorKey(level, key);
	for (UINT l = 0; l < info.levels; l++)
	{
		if (l > 0)
		{
			std::vector<DWORD> smaller;
			Resample(level, UxLevelWidth(info, l - 1), UxLevelHeight(info, l - 1), smaller, UxLevelWidth(info, l),
			         UxLevelHeight(info, l));
			level.swap(smaller);
		}
		UxEncodeLevel(info, l, bits, &level[0]);
	}
	UxMarkDirty(texture);
}

static HRESULT TextureFromImage(const Image &image, UINT width, UINT height, UINT levels, D3DFORMAT format,
                                D3DCOLOR key, D3DXIMAGE_INFO *srcInfo, D3DTexture **texture)
{
	if (srcInfo != NULL)
	{
		srcInfo->Width = image.width;
		srcInfo->Height = image.height;
		srcInfo->Depth = 1;
		srcInfo->MipLevels = 1;
		srcInfo->Format = (D3DFORMAT)image.format;
	}
	// D3DX on the Xbox rounds the size up to powers of two, unless told.
	UINT w = (width == 0 || width == D3DX_DEFAULT) ? NextPow2(image.width) : width;
	UINT h = (height == 0 || height == D3DX_DEFAULT) ? NextPow2(image.height) : height;
	if ((DWORD)format == D3DX_DEFAULT || (DWORD)format == (DWORD)D3DFMT_UNKNOWN)
		format = (D3DFORMAT)(image.dxt.empty() ? 0x06 : image.format);
	UxPixelInfo test;
	DWORD f, s;
	UxEncodeFormat(format, w, h, 1, &f, &s);
	UxDescribePixels(f, s, &test);
	if (!test.linear && ((w & (w - 1)) != 0 || (h & (h - 1)) != 0))
	{
		w = NextPow2(w);
		h = NextPow2(h);
	}
	if (levels == D3DX_DEFAULT)
		levels = 0;   // a full chain (for a swizzled texture)
	if (test.linear)
		levels = 1;
	if (FAILED(D3DDevice_CreateTexture(w, h, levels, 0, format, D3DPOOL_MANAGED, texture)))
		return E_FAIL;
	FillTexture(*texture, image, key);
	return S_OK;
}

extern "C" {

HRESULT WINAPI D3DXCreateTexture(LPDIRECT3DDEVICE8, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
                                 D3DPOOL pool, LPDIRECT3DTEXTURE8 *texture)
{
	if ((DWORD)format == D3DX_DEFAULT || (DWORD)format == (DWORD)D3DFMT_UNKNOWN)
		format = D3DFMT_A8R8G8B8;
	if (levels == D3DX_DEFAULT)
		levels = 0;
	return D3DDevice_CreateTexture(width, height, levels, usage, format, pool, texture);
}

HRESULT WINAPI D3DXCreateTextureFromFileInMemoryEx(LPDIRECT3DDEVICE8, LPCVOID data, UINT size, UINT width,
                                                   UINT height, UINT levels, DWORD, D3DFORMAT format, D3DPOOL, DWORD,
                                                   DWORD, D3DCOLOR key, D3DXIMAGE_INFO *srcInfo, PALETTEENTRY *,
                                                   LPDIRECT3DTEXTURE8 *texture)
{
	*texture = NULL;
	Image image;
	if (!LoadImage(data, size, image))
		return D3DXERR_INVALIDDATA;
	return TextureFromImage(image, width, height, levels, format, key, srcInfo, texture);
}

HRESULT WINAPI D3DXCreateTextureFromFileInMemory(LPDIRECT3DDEVICE8 device, LPCVOID data, UINT size,
                                                 LPDIRECT3DTEXTURE8 *texture)
{
	return D3DXCreateTextureFromFileInMemoryEx(device, data, size, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0,
	                                           (D3DFORMAT)D3DX_DEFAULT, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0,
	                                           NULL, NULL, texture);
}

HRESULT WINAPI D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE8 device, LPCSTR file, UINT width, UINT height, UINT levels,
                                            DWORD usage, D3DFORMAT format, D3DPOOL pool, DWORD filter,
                                            DWORD mipFilter, D3DCOLOR key, D3DXIMAGE_INFO *srcInfo,
                                            PALETTEENTRY *palette, LPDIRECT3DTEXTURE8 *texture)
{
	*texture = NULL;
	std::vector<unsigned char> data;
	if (!UxReadFile(file, data) || data.empty())
		return D3DXERR_INVALIDDATA;
	return D3DXCreateTextureFromFileInMemoryEx(device, &data[0], (UINT)data.size(), width, height, levels, usage,
	                                           format, pool, filter, mipFilter, key, srcInfo, palette, texture);
}

HRESULT WINAPI D3DXCreateTextureFromFileA(LPDIRECT3DDEVICE8 device, LPCSTR file, LPDIRECT3DTEXTURE8 *texture)
{
	return D3DXCreateTextureFromFileExA(device, file, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0,
	                                    (D3DFORMAT)D3DX_DEFAULT, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, NULL,
	                                    NULL, texture);
}

// A surface's pixels, rect and all.
static void WriteSurface(LPDIRECT3DSURFACE8 surface, CONST RECT *rect, const std::vector<DWORD> &argb, UINT sw,
                         UINT sh, D3DCOLOR key)
{
	UxPixelInfo info;
	UxDescribeContainer(surface, &info);
	BYTE *bits = (BYTE *)(ULONG_PTR)surface->Data;
	if (bits == NULL)
		return;
	std::vector<DWORD> target;
	UxDecodeLevel(info, 0, bits, target);
	RECT r = {0, 0, (LONG)info.width, (LONG)info.height};
	if (rect != NULL)
		r = *rect;
	UINT rw = r.right > r.left ? r.right - r.left : 0, rh = r.bottom > r.top ? r.bottom - r.top : 0;
	std::vector<DWORD> scaled;
	Resample(argb, sw, sh, scaled, rw ? rw : 1, rh ? rh : 1);
	ApplyColorKey(scaled, key);
	for (UINT y = 0; y < rh; y++)
		for (UINT x = 0; x < rw; x++)
		{
			UINT tx = r.left + x, ty = r.top + y;
			if (tx < info.width && ty < info.height)
				target[(size_t)ty * info.width + tx] = scaled[(size_t)y * rw + x];
		}
	UxEncodeLevel(info, 0, bits, &target[0]);
	UxMarkDirty(surface);
}

HRESULT WINAPI D3DXLoadSurfaceFromFileInMemory(LPDIRECT3DSURFACE8 surface, CONST PALETTEENTRY *, CONST RECT *rect,
                                               LPCVOID data, UINT size, CONST RECT *, DWORD, D3DCOLOR key,
                                               D3DXIMAGE_INFO *srcInfo)
{
	Image image;
	if (!LoadImage(data, size, image))
		return D3DXERR_INVALIDDATA;
	if (srcInfo != NULL)
	{
		srcInfo->Width = image.width;
		srcInfo->Height = image.height;
		srcInfo->Depth = 1;
		srcInfo->MipLevels = 1;
		srcInfo->Format = (D3DFORMAT)image.format;
	}
	WriteSurface(surface, rect, image.argb, image.width, image.height, key);
	return S_OK;
}

HRESULT WINAPI D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE8 dst, CONST PALETTEENTRY *, CONST RECT *dstRect,
                                          LPDIRECT3DSURFACE8 src, CONST PALETTEENTRY *, CONST RECT *srcRect, DWORD,
                                          D3DCOLOR key)
{
	if (dst == NULL || src == NULL)
		return D3DERR_INVALIDCALL;
	UxPixelInfo from, to;
	UxDescribeContainer(src, &from);
	UxDescribeContainer(dst, &to);
	// The same format and size: copied as it is (DXT stays exact).
	if (dstRect == NULL && srcRect == NULL && key == 0 && from.format == to.format && from.width == to.width &&
	    from.height == to.height && src->Data != 0 && dst->Data != 0)
	{
		if (src != dst)
			memcpy((void *)(ULONG_PTR)dst->Data, (const void *)(ULONG_PTR)src->Data, UxLevelSize(to, 0));
		UxMarkDirty(dst);
		return S_OK;
	}
	std::vector<DWORD> argb;
	UxDecodeLevel(from, 0, (const BYTE *)(ULONG_PTR)src->Data, argb);
	UINT w = from.width, h = from.height;
	if (srcRect != NULL)
	{
		UINT rw = srcRect->right - srcRect->left, rh = srcRect->bottom - srcRect->top;
		std::vector<DWORD> part((size_t)rw * rh);
		for (UINT y = 0; y < rh; y++)
			for (UINT x = 0; x < rw; x++)
			{
				UINT sx = srcRect->left + x, sy = srcRect->top + y;
				part[(size_t)y * rw + x] = sx < w && sy < h ? argb[(size_t)sy * w + sx] : 0;
			}
		argb.swap(part);
		w = rw;
		h = rh;
	}
	WriteSurface(dst, dstRect, argb, w, h, key);
	return S_OK;
}

}   // extern "C"
