// Xbox pixel containers (textures and surfaces) in memory, for the Windows
// build: what their Format/Size fields say, where each mip level is, and
// conversion of a level to and from plain A8R8G8B8 rows. Xbox textures are
// swizzled (Morton order) unless their format is one of the LIN_ ones;
// DXT textures are stored as on the PC.
#pragma once

#include <xtl.h>
#include <vector>

struct UxPixelInfo
{
	DWORD format;       // the Xbox D3DFORMAT code (D3DFMT_*)
	UINT width, height;
	UINT levels;
	UINT bitsPerPixel;  // 4 for DXT1, 8 for DXT3/5
	bool swizzled;
	bool linear;        // LIN_ formats: texel addressing, pitch from Size
	int dxt;            // 0, or 1/3/5
	UINT pitch;         // linear: bytes per row
	bool known;         // a format this code can convert
};

// From a container's Format and Size fields.
void UxDescribePixels(DWORD format, DWORD size, UxPixelInfo *info);
void UxDescribeContainer(const D3DPixelContainer *container, UxPixelInfo *info);
// The Format and Size fields for a new texture or surface.
void UxEncodeFormat(DWORD xboxFormat, UINT width, UINT height, UINT levels, DWORD *format, DWORD *size);
// Bits per pixel and the kind of a format code.
void UxFormatTraits(DWORD xboxFormat, UINT *bitsPerPixel, bool *swizzled, bool *linear, int *dxt, bool *known);

UINT UxLevelWidth(const UxPixelInfo &info, UINT level);
UINT UxLevelHeight(const UxPixelInfo &info, UINT level);
UINT UxLevelPitch(const UxPixelInfo &info, UINT level);   // bytes per row (DXT: per row of blocks)
UINT UxLevelSize(const UxPixelInfo &info, UINT level);
UINT UxLevelOffset(const UxPixelInfo &info, UINT level);
UINT UxTotalSize(const UxPixelInfo &info);

// A level as width*height A8R8G8B8 values (DXT decompressed).
void UxDecodeLevel(const UxPixelInfo &info, UINT level, const BYTE *bits, std::vector<DWORD> &argb);
// ...and back (DXT compressed, roughly).
void UxEncodeLevel(const UxPixelInfo &info, UINT level, BYTE *bits, const DWORD *argb);
