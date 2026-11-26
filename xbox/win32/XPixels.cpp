// Xbox pixel containers in memory (see XPixels.h).
#include "XPixels.h"

#include <string.h>

void UxFormatTraits(DWORD f, UINT *bpp, bool *swizzled, bool *linear, int *dxt, bool *known)
{
	*bpp = 32;
	*swizzled = true;
	*linear = false;
	*dxt = 0;
	*known = true;
	switch (f)
	{
	case 0x00: case 0x01: case 0x0B: case 0x19: *bpp = 8; break;                      // L8 AL8 P8 A8
	case 0x02: case 0x03: case 0x04: case 0x05: case 0x1A: case 0x27: case 0x28: *bpp = 16; break;
	case 0x06: case 0x07: case 0x3A: case 0x3B: case 0x3C: case 0x2A: case 0x2B: *bpp = 32; break;
	case 0x0C: *bpp = 4; *swizzled = false; *dxt = 1; break;
	case 0x0E: *bpp = 8; *swizzled = false; *dxt = 3; break;
	case 0x0F: *bpp = 8; *swizzled = false; *dxt = 5; break;
	case 0x13: case 0x1B: case 0x1F: *bpp = 8; *swizzled = false; *linear = true; break;
	case 0x10: case 0x11: case 0x1C: case 0x1D: case 0x20: case 0x24: case 0x25: case 0x37: case 0x16: case 0x17:
		*bpp = 16; *swizzled = false; *linear = true; break;
	case 0x12: case 0x1E: case 0x3F: case 0x40: case 0x41: case 0x2E: case 0x2F:
		*bpp = 32; *swizzled = false; *linear = true; break;
	default: *known = false; break;
	}
}

void UxDescribePixels(DWORD format, DWORD size, UxPixelInfo *info)
{
	memset(info, 0, sizeof(*info));
	info->format = (format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT;
	UxFormatTraits(info->format, &info->bitsPerPixel, &info->swizzled, &info->linear, &info->dxt, &info->known);
	info->levels = (format & D3DFORMAT_MIPMAP_MASK) >> D3DFORMAT_MIPMAP_SHIFT;
	if (info->levels == 0)
		info->levels = 1;
	if (size != 0)
	{
		info->width = (size & D3DSIZE_WIDTH_MASK) + 1;
		info->height = ((size & D3DSIZE_HEIGHT_MASK) >> D3DSIZE_HEIGHT_SHIFT) + 1;
		info->pitch = (((size & D3DSIZE_PITCH_MASK) >> D3DSIZE_PITCH_SHIFT) + 1) * 64;
		info->levels = 1;
	}
	else
	{
		info->width = 1u << ((format & D3DFORMAT_USIZE_MASK) >> D3DFORMAT_USIZE_SHIFT);
		info->height = 1u << ((format & D3DFORMAT_VSIZE_MASK) >> D3DFORMAT_VSIZE_SHIFT);
		info->pitch = info->width * info->bitsPerPixel / 8;
	}
}

void UxDescribeContainer(const D3DPixelContainer *container, UxPixelInfo *info)
{
	UxDescribePixels(container->Format, container->Size, info);
}

static UINT Log2(UINT v)
{
	UINT n = 0;
	while ((1u << n) < v)
		n++;
	return n;
}

void UxEncodeFormat(DWORD xboxFormat, UINT width, UINT height, UINT levels, DWORD *format, DWORD *size)
{
	UINT bpp;
	bool swizzled, linear, known;
	int dxt;
	UxFormatTraits(xboxFormat, &bpp, &swizzled, &linear, &dxt, &known);
	bool pow2 = (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
	if (levels == 0)
		levels = 1;
	*format = (xboxFormat << D3DFORMAT_FORMAT_SHIFT) | (2 << D3DFORMAT_DIMENSION_SHIFT) |
	          (levels << D3DFORMAT_MIPMAP_SHIFT) | 0x00000001 /* DMA channel A */;
	if (!linear && pow2)
	{
		*format |= (Log2(width) << D3DFORMAT_USIZE_SHIFT) | (Log2(height) << D3DFORMAT_VSIZE_SHIFT);
		*size = 0;
	}
	else
	{
		UINT pitch = (width * bpp / 8 + 63) & ~63u;
		if (pitch == 0)
			pitch = 64;
		*format &= ~D3DFORMAT_MIPMAP_MASK;
		*format |= 1 << D3DFORMAT_MIPMAP_SHIFT;
		*size = (width - 1) | ((height - 1) << D3DSIZE_HEIGHT_SHIFT) | ((pitch / 64 - 1) << D3DSIZE_PITCH_SHIFT);
	}
}

UINT UxLevelWidth(const UxPixelInfo &info, UINT level)
{
	UINT w = info.width >> level;
	return w ? w : 1;
}

UINT UxLevelHeight(const UxPixelInfo &info, UINT level)
{
	UINT h = info.height >> level;
	return h ? h : 1;
}

UINT UxLevelPitch(const UxPixelInfo &info, UINT level)
{
	if (info.dxt)
		return ((UxLevelWidth(info, level) + 3) / 4) * (info.dxt == 1 ? 8 : 16);
	if (info.linear || (!info.swizzled && level == 0))
		return info.pitch;
	return UxLevelWidth(info, level) * info.bitsPerPixel / 8;
}

UINT UxLevelSize(const UxPixelInfo &info, UINT level)
{
	if (info.dxt)
		return UxLevelPitch(info, level) * ((UxLevelHeight(info, level) + 3) / 4);
	return UxLevelPitch(info, level) * UxLevelHeight(info, level);
}

UINT UxLevelOffset(const UxPixelInfo &info, UINT level)
{
	UINT offset = 0;
	for (UINT i = 0; i < level; i++)
		offset += UxLevelSize(info, i);
	return offset;
}

UINT UxTotalSize(const UxPixelInfo &info)
{
	return UxLevelOffset(info, info.levels);
}

// The Xbox's swizzle: the bits of x and y interleaved, x first, until the
// smaller dimension runs out.
static UINT Swizzle(UINT x, UINT y, UINT width, UINT height)
{
	UINT result = 0, bit = 1;
	for (UINT mask = 1; mask < width || mask < height; mask <<= 1)
	{
		if (mask < width)
		{
			if (x & mask)
				result |= bit;
			bit <<= 1;
		}
		if (mask < height)
		{
			if (y & mask)
				result |= bit;
			bit <<= 1;
		}
	}
	return result;
}

static DWORD Expand(DWORD v, int bits)
{
	return bits >= 8 ? v : (v << (8 - bits)) | (v >> (2 * bits - 8 > 0 ? 2 * bits - 8 : 0));
}

static DWORD TexelToArgb(DWORD f, const BYTE *p)
{
	DWORD v;
	switch (f)
	{
	case 0x00: case 0x13: case 0x0B: v = p[0]; return 0xFF000000 | (v << 16) | (v << 8) | v;   // L8, P8
	case 0x01: case 0x1B: v = p[0]; return (v << 24) | (v << 16) | (v << 8) | v;           // AL8
	case 0x19: case 0x1F: return ((DWORD)p[0] << 24) | 0xFFFFFF;                          // A8
	case 0x1A: case 0x20: v = p[0]; return ((DWORD)p[1] << 24) | (v << 16) | (v << 8) | v;  // A8L8
	case 0x02: case 0x10: case 0x03: case 0x1C:
	{
		v = p[0] | (p[1] << 8);
		DWORD a = (f == 0x02 || f == 0x10) ? ((v & 0x8000) ? 0xFF : 0) : 0xFF;
		return (a << 24) | (Expand((v >> 10) & 31, 5) << 16) | (Expand((v >> 5) & 31, 5) << 8) | Expand(v & 31, 5);
	}
	case 0x04: case 0x1D:
		v = p[0] | (p[1] << 8);
		return (((v >> 12) & 15) * 17 << 24) | (((v >> 8) & 15) * 17 << 16) | (((v >> 4) & 15) * 17 << 8) |
		       ((v & 15) * 17);
	case 0x05: case 0x11:
		v = p[0] | (p[1] << 8);
		return 0xFF000000 | (Expand((v >> 11) & 31, 5) << 16) | (Expand((v >> 5) & 63, 6) << 8) | Expand(v & 31, 5);
	case 0x06: case 0x12: return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24);
	case 0x07: case 0x1E: return p[0] | (p[1] << 8) | (p[2] << 16) | 0xFF000000;
	case 0x3A: case 0x3F: return p[2] | (p[1] << 8) | (p[0] << 16) | ((DWORD)p[3] << 24);   // A8B8G8R8
	case 0x3B: case 0x40: return p[3] | (p[2] << 8) | (p[1] << 16) | ((DWORD)p[0] << 24);   // B8G8R8A8
	case 0x3C: case 0x41: return p[1] | (p[2] << 8) | (p[3] << 16) | ((DWORD)p[0] << 24);   // R8G8B8A8
	default: return 0xFF808080;
	}
}

static void ArgbToTexel(DWORD f, DWORD c, BYTE *p)
{
	DWORD a = c >> 24, r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
	DWORD l = (r * 30 + g * 59 + b * 11) / 100;
	DWORD v;
	switch (f)
	{
	case 0x00: case 0x13: case 0x0B: p[0] = (BYTE)l; break;
	case 0x01: case 0x1B: p[0] = (BYTE)a; break;
	case 0x19: case 0x1F: p[0] = (BYTE)a; break;
	case 0x1A: case 0x20: p[0] = (BYTE)l; p[1] = (BYTE)a; break;
	case 0x02: case 0x10: case 0x03: case 0x1C:
		v = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3) | ((f == 0x02 || f == 0x10) ? (a >= 128 ? 0x8000 : 0) : 0x8000);
		p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8);
		break;
	case 0x04: case 0x1D:
		v = ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
		p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8);
		break;
	case 0x05: case 0x11:
		v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
		p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8);
		break;
	case 0x06: case 0x12: case 0x07: case 0x1E:
		p[0] = (BYTE)b; p[1] = (BYTE)g; p[2] = (BYTE)r; p[3] = (BYTE)a;
		break;
	case 0x3A: case 0x3F: p[0] = (BYTE)r; p[1] = (BYTE)g; p[2] = (BYTE)b; p[3] = (BYTE)a; break;
	case 0x3B: case 0x40: p[0] = (BYTE)a; p[1] = (BYTE)r; p[2] = (BYTE)g; p[3] = (BYTE)b; break;
	case 0x3C: case 0x41: p[0] = (BYTE)a; p[1] = (BYTE)b; p[2] = (BYTE)g; p[3] = (BYTE)r; break;
	default: break;
	}
}

static DWORD Color565(WORD c)
{
	return 0xFF000000 | (Expand((c >> 11) & 31, 5) << 16) | (Expand((c >> 5) & 63, 6) << 8) | Expand(c & 31, 5);
}

static DWORD Mix(DWORD a, DWORD b, int wa, int wb, int div)
{
	DWORD out = 0;
	for (int s = 0; s < 24; s += 8)
		out |= ((((a >> s) & 0xFF) * wa + ((b >> s) & 0xFF) * wb) / div) << s;
	return out | 0xFF000000;
}

static void DecodeDxtBlock(int dxt, const BYTE *block, DWORD out[16])
{
	const BYTE *colorBlock = dxt == 1 ? block : block + 8;
	WORD c0 = colorBlock[0] | (colorBlock[1] << 8), c1 = colorBlock[2] | (colorBlock[3] << 8);
	DWORD colors[4];
	colors[0] = Color565(c0);
	colors[1] = Color565(c1);
	if (dxt != 1 || c0 > c1)
	{
		colors[2] = Mix(colors[0], colors[1], 2, 1, 3);
		colors[3] = Mix(colors[0], colors[1], 1, 2, 3);
	}
	else
	{
		colors[2] = Mix(colors[0], colors[1], 1, 1, 2);
		colors[3] = 0;   // transparent black
	}
	DWORD indices = colorBlock[4] | (colorBlock[5] << 8) | (colorBlock[6] << 16) | ((DWORD)colorBlock[7] << 24);
	for (int i = 0; i < 16; i++)
		out[i] = colors[(indices >> (2 * i)) & 3];
	if (dxt == 3)
	{
		for (int i = 0; i < 16; i++)
		{
			DWORD a = (block[i / 2] >> ((i & 1) * 4)) & 15;
			out[i] = (out[i] & 0xFFFFFF) | ((a * 17) << 24);
		}
	}
	else if (dxt == 5)
	{
		DWORD alphas[8];
		alphas[0] = block[0];
		alphas[1] = block[1];
		if (alphas[0] > alphas[1])
		{
			for (int i = 1; i < 7; i++)
				alphas[i + 1] = ((7 - i) * alphas[0] + i * alphas[1]) / 7;
		}
		else
		{
			for (int i = 1; i < 5; i++)
				alphas[i + 1] = ((5 - i) * alphas[0] + i * alphas[1]) / 5;
			alphas[6] = 0;
			alphas[7] = 255;
		}
		unsigned long long bits = 0;
		for (int i = 0; i < 6; i++)
			bits |= (unsigned long long)block[2 + i] << (8 * i);
		for (int i = 0; i < 16; i++)
			out[i] = (out[i] & 0xFFFFFF) | (alphas[(bits >> (3 * i)) & 7] << 24);
	}
}

static WORD To565(DWORD c)
{
	return (WORD)((((c >> 16) & 0xFF) >> 3) << 11 | (((c >> 8) & 0xFF) >> 2) << 5 | ((c & 0xFF) >> 3));
}

// A plain range-fit encoder: the two extreme colors and the nearest of four.
static void EncodeDxtBlock(int dxt, const DWORD in[16], BYTE *block)
{
	BYTE *colorBlock = dxt == 1 ? block : block + 8;
	DWORD lo = in[0], hi = in[0];
	int loL = 1 << 30, hiL = -1;
	for (int i = 0; i < 16; i++)
	{
		DWORD c = in[i];
		int l = (int)(((c >> 16) & 0xFF) * 30 + ((c >> 8) & 0xFF) * 59 + (c & 0xFF) * 11);
		if (l < loL) { loL = l; lo = c; }
		if (l > hiL) { hiL = l; hi = c; }
	}
	WORD c0 = To565(hi), c1 = To565(lo);
	if (c0 < c1)
	{
		WORD t = c0; c0 = c1; c1 = t;
	}
	else if (c0 == c1 && c0 > 0)
	{
		c1 = c0 - 1;
	}
	DWORD palette[4] = {Color565(c0), Color565(c1)};
	palette[2] = Mix(palette[0], palette[1], 2, 1, 3);
	palette[3] = Mix(palette[0], palette[1], 1, 2, 3);
	DWORD indices = 0;
	for (int i = 0; i < 16; i++)
	{
		int best = 0;
		long bestDistance = 1L << 30;
		for (int k = 0; k < 4; k++)
		{
			long d = 0;
			for (int s = 0; s < 24; s += 8)
			{
				long diff = (long)((in[i] >> s) & 0xFF) - (long)((palette[k] >> s) & 0xFF);
				d += diff * diff;
			}
			if (d < bestDistance) { bestDistance = d; best = k; }
		}
		indices |= (DWORD)best << (2 * i);
	}
	colorBlock[0] = (BYTE)c0; colorBlock[1] = (BYTE)(c0 >> 8);
	colorBlock[2] = (BYTE)c1; colorBlock[3] = (BYTE)(c1 >> 8);
	memcpy(colorBlock + 4, &indices, 4);
	if (dxt == 3)
	{
		for (int i = 0; i < 8; i++)
			block[i] = (BYTE)(((in[2 * i] >> 28) & 15) | (((in[2 * i + 1] >> 28) & 15) << 4));
	}
	else if (dxt == 5)
	{
		// Alpha 255..0 in the 8-value mode.
		block[0] = 255;
		block[1] = 0;
		unsigned long long bits = 0;
		for (int i = 0; i < 16; i++)
		{
			DWORD a = in[i] >> 24;
			int index = (int)((255 - a) * 7 + 127) / 255;   // 0 = 255 ... 7 = 0
			int code = index == 0 ? 0 : index == 7 ? 1 : index + 1;
			bits |= (unsigned long long)code << (3 * i);
		}
		for (int i = 0; i < 6; i++)
			block[2 + i] = (BYTE)(bits >> (8 * i));
	}
}

void UxDecodeLevel(const UxPixelInfo &info, UINT level, const BYTE *bits, std::vector<DWORD> &argb)
{
	UINT w = UxLevelWidth(info, level), h = UxLevelHeight(info, level);
	argb.assign((size_t)w * h, 0xFF808080);
	if (bits == NULL)
		return;
	const BYTE *base = bits + UxLevelOffset(info, level);
	if (info.dxt)
	{
		UINT pitch = UxLevelPitch(info, level), blockBytes = info.dxt == 1 ? 8 : 16;
		DWORD block[16];
		for (UINT by = 0; by < (h + 3) / 4; by++)
		{
			for (UINT bx = 0; bx < (w + 3) / 4; bx++)
			{
				DecodeDxtBlock(info.dxt, base + by * pitch + bx * blockBytes, block);
				for (UINT i = 0; i < 16; i++)
				{
					UINT x = bx * 4 + (i & 3), y = by * 4 + i / 4;
					if (x < w && y < h)
						argb[(size_t)y * w + x] = block[i];
				}
			}
		}
		return;
	}
	if (info.format == 0x24 || info.format == 0x25)
	{
		// YUY2 / UYVY: two pixels in four bytes.
		UINT pitch = UxLevelPitch(info, level);
		for (UINT y = 0; y < h; y++)
		{
			const BYTE *row = base + (size_t)y * pitch;
			for (UINT x = 0; x + 1 < w + 1 && x < w; x += 2)
			{
				const BYTE *q = row + x * 2;
				int y0 = info.format == 0x24 ? q[0] : q[1], y1 = info.format == 0x24 ? q[2] : q[3];
				int u = (info.format == 0x24 ? q[1] : q[0]) - 128, v = (info.format == 0x24 ? q[3] : q[2]) - 128;
				for (int k = 0; k < 2 && x + k < w; k++)
				{
					int yy = (k ? y1 : y0) - 16;
					int r = (298 * yy + 409 * v + 128) >> 8, g = (298 * yy - 100 * u - 208 * v + 128) >> 8,
					    b = (298 * yy + 516 * u + 128) >> 8;
					r = r < 0 ? 0 : r > 255 ? 255 : r;
					g = g < 0 ? 0 : g > 255 ? 255 : g;
					b = b < 0 ? 0 : b > 255 ? 255 : b;
					argb[(size_t)y * w + x + k] = 0xFF000000 | (r << 16) | (g << 8) | b;
				}
			}
		}
		return;
	}
	UINT bytes = info.bitsPerPixel / 8;
	if (info.swizzled)
	{
		for (UINT y = 0; y < h; y++)
			for (UINT x = 0; x < w; x++)
				argb[(size_t)y * w + x] = TexelToArgb(info.format, base + (size_t)Swizzle(x, y, w, h) * bytes);
	}
	else
	{
		UINT pitch = UxLevelPitch(info, level);
		for (UINT y = 0; y < h; y++)
			for (UINT x = 0; x < w; x++)
				argb[(size_t)y * w + x] = TexelToArgb(info.format, base + (size_t)y * pitch + x * bytes);
	}
}

void UxEncodeLevel(const UxPixelInfo &info, UINT level, BYTE *bits, const DWORD *argb)
{
	UINT w = UxLevelWidth(info, level), h = UxLevelHeight(info, level);
	BYTE *base = bits + UxLevelOffset(info, level);
	if (info.dxt)
	{
		UINT pitch = UxLevelPitch(info, level), blockBytes = info.dxt == 1 ? 8 : 16;
		DWORD block[16];
		for (UINT by = 0; by < (h + 3) / 4; by++)
		{
			for (UINT bx = 0; bx < (w + 3) / 4; bx++)
			{
				for (UINT i = 0; i < 16; i++)
				{
					UINT x = bx * 4 + (i & 3), y = by * 4 + i / 4;
					block[i] = argb[(size_t)(y < h ? y : h - 1) * w + (x < w ? x : w - 1)];
				}
				EncodeDxtBlock(info.dxt, block, base + by * pitch + bx * blockBytes);
			}
		}
		return;
	}
	UINT bytes = info.bitsPerPixel / 8;
	if (info.swizzled)
	{
		for (UINT y = 0; y < h; y++)
			for (UINT x = 0; x < w; x++)
				ArgbToTexel(info.format, argb[(size_t)y * w + x], base + (size_t)Swizzle(x, y, w, h) * bytes);
	}
	else
	{
		UINT pitch = UxLevelPitch(info, level);
		for (UINT y = 0; y < h; y++)
			for (UINT x = 0; x < w; x++)
				ArgbToTexel(info.format, argb[(size_t)y * w + x], base + (size_t)y * pitch + x * bytes);
	}
}
