// The Xbox's Direct3D 8 on Direct3D 9 (WinGfx.cpp), for the Windows build.
//
// Resources keep the Xbox's layout in memory (the code fills textures and
// vertex buffers itself, and registers .xpr resources in place), and are
// converted for Direct3D 9 when they're drawn with: textures unswizzled to
// A8R8G8B8 (DXT kept), linear textures' texel coordinates normalised, quads
// and polygons turned into triangles. Render and texture stage states are
// translated by name (XD3DStates.inc) and value (the Xbox uses OpenGL's
// numbers for blend factors, comparisons, culling...). The font's vertex
// shader and pixel shader are the only programs the dash uses; they're
// recognised and drawn with the fixed function.
#include <xtl.h>

#include <map>
#include <string>
#include <vector>
#include <string.h>

#include "UxWin.h"
#include "XPixels.h"

#include "XD3DStates.inc"

// ---- the lock (the dash loads textures on other threads) ----

static CRITICAL_SECTION s_lock;
static bool s_lockReady;

// (Each file has its own: in an unnamed namespace, so they stay apart.)
namespace
{
struct Locker
{
	Locker()
	{
		if (!s_lockReady)
		{
			InitializeCriticalSection(&s_lock);
			s_lockReady = true;
		}
		EnterCriticalSection(&s_lock);
	}
	~Locker() { LeaveCriticalSection(&s_lock); }
};
}

// ---- resources ----

enum ResourceKind
{
	KIND_TEXTURE,
	KIND_SURFACE,        // a texture's level, or an image surface
	KIND_BACKBUFFER,
	KIND_DEPTH,
	KIND_VERTEXBUFFER,
	KIND_INDEXBUFFER,
};

// What goes with each resource the device made or saw.
struct Backing
{
	ResourceKind kind;
	BYTE *memory;          // owned memory (made by the device), else NULL
	DWORD memorySize;
	void *gpu;             // the Direct3D 9 texture
	DWORD gpuData, gpuFormat, gpuSize;   // what it was made from
	bool dirty;
	D3DTexture *parent;    // surfaces of a texture
	UINT level;
};

// (Objects the dash's global constructors can reach are made on first use:
// the order of those constructors between files is the compiler's.)
static std::map<D3DResource *, Backing> &s_resources_()
{
	static std::map<D3DResource *, Backing> value;
	return value;
}
#define s_resources s_resources_()

static Backing *Find(D3DResource *resource)
{
	std::map<D3DResource *, Backing>::iterator it = s_resources.find(resource);
	return it == s_resources.end() ? NULL : &it->second;
}

static Backing &Track(D3DResource *resource, ResourceKind kind)
{
	Backing &b = s_resources[resource];
	memset(&b, 0, sizeof(b));
	b.kind = kind;
	b.dirty = true;
	return b;
}

static void MarkDirty(D3DResource *resource)
{
	Backing *b = Find(resource);
	if (b != NULL)
	{
		b->dirty = true;
		if (b->parent != NULL)
			MarkDirty(b->parent);
	}
}

static D3DSurface s_backBuffer;
static D3DSurface s_depthBuffer;
static D3DSurface *s_renderTarget = &s_backBuffer;

// ---- the device's state ----

static DWORD s_renderStates[D3DRS_MAX];
static DWORD s_textureStates[4][D3DTSS_MAX];
static D3DBaseTexture *s_textures[4];
static DWORD s_vertexShader;           // an FVF, or a program handle (odd)
static DWORD s_pixelShader;
static D3DVertexBuffer *s_streamSource;
static UINT s_streamStride;
static D3DIndexBuffer *s_indices;
static UINT s_baseVertex;
static D3DMATRIX s_transforms[D3DTS_MAX];
static D3DVIEWPORT8 s_viewport;
static D3DPRESENT_PARAMETERS s_present;
static bool s_pixelShaderApplied;

// Vertex shader programs: only the font's (screen-space x, y, u, v and a
// color) is drawn; others draw as an FVF of XYZRHW | DIFFUSE | TEX1.
struct Program
{
	bool screenSpaceFont;
};
static std::vector<Program> &s_programs_()
{
	static std::vector<Program> value;
	return value;
}
#define s_programs s_programs_()

static bool IsProgram(DWORD handle)
{
	return (handle & 1) != 0;
}

// ---- value translation ----

static DWORD Blend(DWORD v)
{
	switch (v)
	{
	case D3DBLEND_ZERO: return 1;
	case D3DBLEND_ONE: return 2;
	case D3DBLEND_SRCCOLOR: return 3;
	case D3DBLEND_INVSRCCOLOR: return 4;
	case D3DBLEND_SRCALPHA: return 5;
	case D3DBLEND_INVSRCALPHA: return 6;
	case D3DBLEND_DESTALPHA: return 7;
	case D3DBLEND_INVDESTALPHA: return 8;
	case D3DBLEND_DESTCOLOR: return 9;
	case D3DBLEND_INVDESTCOLOR: return 10;
	case D3DBLEND_SRCALPHASAT: return 11;
	case D3DBLEND_CONSTANTCOLOR: return 14;      // D3DBLEND_BLENDFACTOR
	case D3DBLEND_INVCONSTANTCOLOR: return 15;
	default: return 2;
	}
}

static DWORD Compare(DWORD v)
{
	return v >= 0x200 && v <= 0x207 ? v - 0x200 + 1 : 8;   // NEVER..ALWAYS = 1..8
}

static DWORD StencilOp(DWORD v)
{
	switch (v)
	{
	case D3DSTENCILOP_KEEP: return 1;
	case D3DSTENCILOP_ZERO: return 2;
	case D3DSTENCILOP_REPLACE: return 3;
	case D3DSTENCILOP_INCRSAT: return 4;
	case D3DSTENCILOP_DECRSAT: return 5;
	case D3DSTENCILOP_INVERT: return 6;
	case D3DSTENCILOP_INCR: return 7;
	case D3DSTENCILOP_DECR: return 8;
	default: return 1;
	}
}

static DWORD BlendOp(DWORD v)
{
	switch (v)
	{
	case D3DBLENDOP_ADD: return 1;
	case D3DBLENDOP_SUBTRACT: return 2;
	case D3DBLENDOP_REVSUBTRACT: return 3;
	case D3DBLENDOP_MIN: return 4;
	case D3DBLENDOP_MAX: return 5;
	default: return 1;
	}
}

static DWORD TextureOp(DWORD v)
{
	switch (v)
	{
	case D3DTOP_BLENDCURRENTALPHA: return 16;
	case D3DTOP_BLENDTEXTUREALPHA: return 13;
	case D3DTOP_BLENDFACTORALPHA: return 14;
	case D3DTOP_BLENDTEXTUREALPHAPM: return 15;
	default: return v;
	}
}

static DWORD Address(DWORD v)
{
	return v == D3DTADDRESS_CLAMPTOEDGE ? 3 : v;   // (CLAMP is to the edge on the PC)
}

static DWORD Filter(DWORD v)
{
	return v > 3 ? 2 : v;   // QUINCUNX/GAUSSIAN: linear
}

// D3D9's numbers for a few render states (D3DRENDERSTATETYPE).
enum
{
	RS9_FILLMODE = 8, RS9_SHADEMODE = 9, RS9_SRCBLEND = 19, RS9_DESTBLEND = 20, RS9_CULLMODE = 22, RS9_ZFUNC = 23,
	RS9_ALPHAFUNC = 25, RS9_STENCILFAIL = 53, RS9_STENCILZFAIL = 54, RS9_STENCILPASS = 55, RS9_STENCILFUNC = 56,
	RS9_BLENDOP = 171, RS9_ZENABLE = 7, RS9_DITHERENABLE = 26, RS9_MULTISAMPLEANTIALIAS = 161,
	RS9_COLORWRITEENABLE = 168,
};

static void ApplyRenderState(DWORD xboxState, DWORD value)
{
	if (xboxState >= D3DRS_MAX)
		return;
	DWORD state = kRenderStateMap[xboxState];
	if (state == 0)
		return;   // Xbox only (pixel shader registers, YUV, ...)
	switch (state)
	{
	case RS9_FILLMODE: value = value == D3DFILL_POINT ? 1 : value == D3DFILL_WIREFRAME ? 2 : 3; break;
	case RS9_SHADEMODE: value = value == D3DSHADE_FLAT ? 1 : 2; break;
	case RS9_SRCBLEND: case RS9_DESTBLEND: value = Blend(value); break;
	case RS9_CULLMODE: value = value == D3DCULL_CW ? 2 : value == D3DCULL_CCW ? 3 : 1; break;
	case RS9_ZFUNC: case RS9_ALPHAFUNC: case RS9_STENCILFUNC: value = Compare(value); break;
	case RS9_STENCILFAIL: case RS9_STENCILZFAIL: case RS9_STENCILPASS: value = StencilOp(value); break;
	case RS9_BLENDOP: value = BlendOp(value); break;
	case RS9_COLORWRITEENABLE:
		// Xbox: R 0x10000, G 0x100, B 1, A 0x1000000; PC: R 1, G 2, B 4, A 8.
		value = ((value & 0x00010000) ? 1 : 0) | ((value & 0x00000100) ? 2 : 0) | ((value & 0x00000001) ? 4 : 0) |
		        ((value & 0x01000000) ? 8 : 0);
		break;
	default: break;
	}
	UxGfxSetRenderState(state, value);
}

static void ApplyTextureStageState(DWORD stage, DWORD type, DWORD value)
{
	if (stage >= 4 || type >= D3DTSS_MAX)
		return;
	DWORD map = kTextureStateMap[type];
	if (map == 0)
		return;
	DWORD kind = map >> 8, number = map & 0xFF;
	if (kind == 2)
	{
		if (type == D3DTSS_ADDRESSU || type == D3DTSS_ADDRESSV || type == D3DTSS_ADDRESSW)
			value = Address(value);
		else if (type == D3DTSS_MAGFILTER || type == D3DTSS_MINFILTER || type == D3DTSS_MIPFILTER)
			value = Filter(value);
		UxGfxSetSamplerState(stage, number, value);
	}
	else
	{
		if (type == D3DTSS_COLOROP || type == D3DTSS_ALPHAOP)
			value = TextureOp(value);
		UxGfxSetTextureStageState(stage, number, value);
	}
}

// ---- textures for Direct3D 9 ----

struct StageTexture
{
	void *gpu;
	bool linear;   // texel coordinates
	float width, height;
};

static StageTexture Upload(D3DBaseTexture *texture)
{
	StageTexture result = {NULL, false, 1.0f, 1.0f};
	if (texture == NULL)
		return result;
	UxPixelInfo info;
	UxDescribeContainer(texture, &info);
	Backing *b = Find(texture);
	if (b == NULL)
		b = &Track(texture, KIND_TEXTURE);
	result.linear = info.linear;
	result.width = (float)info.width;
	result.height = (float)info.height;
	if (b->gpu != NULL && !b->dirty && b->gpuData == texture->Data && b->gpuFormat == texture->Format &&
	    b->gpuSize == texture->Size)
	{
		result.gpu = b->gpu;
		return result;
	}
	if (b->gpu != NULL)
	{
		UxGfxReleaseTexture(b->gpu);
		b->gpu = NULL;
	}
	const BYTE *bits = (const BYTE *)(ULONG_PTR)texture->Data;
	if (bits == NULL || info.width == 0 || info.height == 0 || info.width > 4096 || info.height > 4096)
		return result;
	// DXT is kept (when the size suits Direct3D 9); everything else becomes A8R8G8B8.
	bool keepDxt = info.dxt != 0 && (info.width % 4) == 0 && (info.height % 4) == 0;
	UINT levels = info.levels;
	for (UINT l = 1; l < levels; l++)
	{
		if (keepDxt && (UxLevelWidth(info, l) % 4 != 0 || UxLevelHeight(info, l) % 4 != 0))
		{
			levels = l;
			break;
		}
	}
	int format = keepDxt ? (info.dxt == 1 ? UX_TEX_DXT1 : info.dxt == 3 ? UX_TEX_DXT3 : UX_TEX_DXT5) : UX_TEX_ARGB;
	b->gpu = UxGfxCreateTexture(info.width, info.height, levels, format);
	if (b->gpu == NULL)
		return result;
	std::vector<DWORD> argb;
	for (UINT l = 0; l < levels; l++)
	{
		if (keepDxt)
		{
			UxGfxUploadTexture(b->gpu, l, bits + UxLevelOffset(info, l), UxLevelPitch(info, l));
		}
		else
		{
			UxDecodeLevel(info, l, bits, argb);
			UxGfxUploadTexture(b->gpu, l, &argb[0], UxLevelWidth(info, l) * 4);
		}
	}
	b->gpuData = texture->Data;
	b->gpuFormat = texture->Format;
	b->gpuSize = texture->Size;
	b->dirty = false;
	result.gpu = b->gpu;
	return result;
}

static StageTexture s_stageTextures[4];

// The textures, and the font's pixel shader as stage settings.
static void ApplyForDraw()
{
	for (int i = 0; i < 4; i++)
	{
		s_stageTextures[i] = Upload(s_textures[i]);
		UxGfxSetTexture(i, s_stageTextures[i].gpu);
	}
	if (s_pixelShader != 0)
	{
		// The font's: the texture times the diffuse color, alpha too. (The
		// Xbox's also squeezes the color into 16..235, TV's range: not
		// here.)
		UxGfxSetTextureStageState(0, 1 /* COLOROP */, 4 /* MODULATE */);
		UxGfxSetTextureStageState(0, 2 /* COLORARG1 */, 2 /* TEXTURE */);
		UxGfxSetTextureStageState(0, 3 /* COLORARG2 */, 0 /* DIFFUSE */);
		UxGfxSetTextureStageState(0, 4 /* ALPHAOP */, 4 /* MODULATE */);
		UxGfxSetTextureStageState(0, 5 /* ALPHAARG1 */, 2 /* TEXTURE */);
		UxGfxSetTextureStageState(0, 6 /* ALPHAARG2 */, 0 /* DIFFUSE */);
		UxGfxSetTextureStageState(1, 1, 1 /* DISABLE */);
		s_pixelShaderApplied = true;
	}
	else if (s_pixelShaderApplied)
	{
		s_pixelShaderApplied = false;
		for (DWORD t = D3DTSS_COLOROP; t <= D3DTSS_ALPHAARG2; t++)
		{
			ApplyTextureStageState(0, t, s_textureStates[0][t]);
			ApplyTextureStageState(1, t, s_textureStates[1][t]);
		}
	}
}

// ---- drawing ----

static UINT FvfStride(DWORD fvf)
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
		UINT format = (fvf >> (16 + i * 2)) & 3;   // D3DFVF_TEXCOORDSIZEn
		size += format == 0 ? 8 : format == 1 ? 12 : format == 2 ? 16 : 4;
	}
	return size;
}

// Where texture coordinate set n starts in an FVF vertex.
static UINT TexCoordOffset(DWORD fvf, UINT n)
{
	UINT offset = FvfStride(fvf & (D3DFVF_POSITION_MASK | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_SPECULAR));
	for (UINT i = 0; i < n; i++)
	{
		UINT format = (fvf >> (16 + i * 2)) & 3;
		offset += format == 0 ? 8 : format == 1 ? 12 : format == 2 ? 16 : 4;
	}
	return offset;
}

// Direct3D 9 primitive types (D3DPRIMITIVETYPE).
enum { PT9_POINTLIST = 1, PT9_LINELIST = 2, PT9_LINESTRIP = 3, PT9_TRIANGLELIST = 4, PT9_TRIANGLESTRIP = 5,
       PT9_TRIANGLEFAN = 6 };

// Draws vertexCount vertices of an Xbox primitive type. indices: optional.
static void Draw(D3DPRIMITIVETYPE type, UINT vertexCount, const BYTE *vertices, UINT stride, DWORD fvf,
                 const WORD *indices)
{
	if (vertices == NULL || vertexCount == 0 || stride == 0)
		return;
	// (Drawing into a texture isn't done: SetRenderTarget.)
	if (s_renderTarget != &s_backBuffer)
		return;
	ApplyForDraw();

	// Linear textures are addressed in texels on the Xbox: scale their
	// coordinates to 0..1 on a copy.
	std::vector<BYTE> copy;
	UINT texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	UINT maxIndex = vertexCount;
	if (indices != NULL)
	{
		maxIndex = 0;
		for (UINT i = 0; i < vertexCount; i++)
			if ((UINT)indices[i] + 1 > maxIndex)
				maxIndex = indices[i] + 1;
	}
	for (UINT stage = 0; stage < 4; stage++)
	{
		if (!s_stageTextures[stage].linear || s_stageTextures[stage].gpu == NULL)
			continue;
		UINT set = s_textureStates[stage][D3DTSS_TEXCOORDINDEX] & 0xFFFF;
		if (set >= texCount || (s_textureStates[stage][D3DTSS_TEXCOORDINDEX] & 0xFFFF0000) != 0)
			continue;
		if (copy.empty())
		{
			copy.assign(vertices, vertices + (size_t)maxIndex * stride);
			vertices = &copy[0];
		}
		UINT offset = TexCoordOffset(fvf, set);
		for (UINT v = 0; v < maxIndex; v++)
		{
			float *uv = (float *)(&copy[0] + (size_t)v * stride + offset);
			uv[0] /= s_stageTextures[stage].width;
			uv[1] /= s_stageTextures[stage].height;
		}
	}

	// Screen-space vertices with an rhw of 0 (DrawRect's, so dialogs and
	// DimScreen) draw on the Xbox; Direct3D 9 divides by it and loses them,
	// so they get 1.
	if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW)
	{
		for (UINT v = 0; v < maxIndex; v++)
		{
			const float *rhw = (const float *)(vertices + (size_t)v * stride + 12);
			if (*rhw > 0.0f)
				continue;
			if (copy.empty())
			{
				copy.assign(vertices, vertices + (size_t)maxIndex * stride);
				vertices = &copy[0];
			}
			*(float *)(&copy[0] + (size_t)v * stride + 12) = 1.0f;
		}
	}

	std::vector<WORD> list;
	switch (type)
	{
	case D3DPT_QUADLIST:
		for (UINT q = 0; q + 3 < vertexCount; q += 4)
		{
			WORD a = (WORD)q, b = (WORD)(q + 1), c = (WORD)(q + 2), d = (WORD)(q + 3);
			WORD tri[6] = {a, b, c, a, c, d};
			list.insert(list.end(), tri, tri + 6);
		}
		break;
	case D3DPT_QUADSTRIP:
		for (UINT q = 0; q + 3 < vertexCount; q += 2)
		{
			WORD a = (WORD)q, b = (WORD)(q + 1), c = (WORD)(q + 3), d = (WORD)(q + 2);
			WORD tri[6] = {a, b, c, a, c, d};
			list.insert(list.end(), tri, tri + 6);
		}
		break;
	case D3DPT_LINELOOP:
		for (UINT i = 0; i < vertexCount; i++)
			list.push_back((WORD)i);
		list.push_back(0);
		break;
	default:
		break;
	}
	if (!list.empty())
	{
		if (indices != NULL)
			for (size_t i = 0; i < list.size(); i++)
				list[i] = indices[list[i]];
		UINT primitive = type == D3DPT_LINELOOP ? PT9_LINESTRIP : PT9_TRIANGLELIST;
		UINT count = type == D3DPT_LINELOOP ? (UINT)list.size() - 1 : (UINT)list.size() / 3;
		UxGfxDrawIndexed(primitive, maxIndex, count, &list[0], vertices, stride, fvf);
		return;
	}

	UINT primitive, count;
	switch (type)
	{
	case D3DPT_POINTLIST: primitive = PT9_POINTLIST; count = vertexCount; break;
	case D3DPT_LINELIST: primitive = PT9_LINELIST; count = vertexCount / 2; break;
	case D3DPT_LINESTRIP: primitive = PT9_LINESTRIP; count = vertexCount - 1; break;
	case D3DPT_TRIANGLELIST: primitive = PT9_TRIANGLELIST; count = vertexCount / 3; break;
	case D3DPT_TRIANGLESTRIP: primitive = PT9_TRIANGLESTRIP; count = vertexCount - 2; break;
	case D3DPT_TRIANGLEFAN: case D3DPT_POLYGON: primitive = PT9_TRIANGLEFAN; count = vertexCount - 2; break;
	default: return;
	}
	if ((int)count <= 0)
		return;
	if (indices != NULL)
		UxGfxDrawIndexed(primitive, maxIndex, count, indices, vertices, stride, fvf);
	else
		UxGfxDraw(primitive, count, vertices, stride, fvf);
}

// The current vertex format: the FVF, or for a program XYZRHW | DIFFUSE | TEX1.
static DWORD CurrentFvf()
{
	if (IsProgram(s_vertexShader))
		return D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
	return s_vertexShader;
}

// Primitive count (Direct3D 8's) to vertex count.
static UINT VerticesFor(D3DPRIMITIVETYPE type, UINT primitives)
{
	switch (type)
	{
	case D3DPT_POINTLIST: return primitives;
	case D3DPT_LINELIST: return primitives * 2;
	case D3DPT_LINESTRIP: return primitives + 1;
	case D3DPT_LINELOOP: return primitives;
	case D3DPT_TRIANGLELIST: return primitives * 3;
	case D3DPT_TRIANGLESTRIP: case D3DPT_TRIANGLEFAN: return primitives + 2;
	case D3DPT_QUADLIST: return primitives * 4;
	case D3DPT_QUADSTRIP: return primitives * 2 + 2;
	case D3DPT_POLYGON: return primitives + 2;
	default: return 0;
	}
}

// ---- immediate mode (Begin, SetVertexData*, End) ----

struct ImmediateVertex
{
	float x, y, z, rhw;
	DWORD color;
	float u0, v0, u1, v1;
};
static D3DPRIMITIVETYPE s_immediateType;
static bool s_inBegin;
static std::vector<ImmediateVertex> &s_immediate_()
{
	static std::vector<ImmediateVertex> value;
	return value;
}
#define s_immediate s_immediate_()
static ImmediateVertex s_current = {0, 0, 0, 1, 0xFFFFFFFF, 0, 0, 0, 0};

// ---- Direct3D ----

extern "C" {

Direct3D *WINAPI Direct3DCreate8(UINT)
{
	static Direct3D s_direct3D;
	return &s_direct3D;
}

static void DefaultStates()
{
	memset(s_renderStates, 0, sizeof(s_renderStates));
	s_renderStates[D3DRS_ZENABLE] = TRUE;
	s_renderStates[D3DRS_ZWRITEENABLE] = TRUE;
	s_renderStates[D3DRS_SRCBLEND] = D3DBLEND_ONE;
	s_renderStates[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
	s_renderStates[D3DRS_CULLMODE] = D3DCULL_CCW;
	s_renderStates[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
	s_renderStates[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
	s_renderStates[D3DRS_LIGHTING] = TRUE;
	s_renderStates[D3DRS_COLORWRITEENABLE] = D3DCOLORWRITEENABLE_ALL;
	for (int s = 0; s < 4; s++)
	{
		memset(s_textureStates[s], 0, sizeof(s_textureStates[s]));
		s_textureStates[s][D3DTSS_COLOROP] = s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
		s_textureStates[s][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
		s_textureStates[s][D3DTSS_COLORARG2] = D3DTA_CURRENT;
		s_textureStates[s][D3DTSS_ALPHAOP] = s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
		s_textureStates[s][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
		s_textureStates[s][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
		s_textureStates[s][D3DTSS_TEXCOORDINDEX] = s;
		s_textureStates[s][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
		s_textureStates[s][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
		s_textureStates[s][D3DTSS_MAGFILTER] = D3DTEXF_POINT;
		s_textureStates[s][D3DTSS_MINFILTER] = D3DTEXF_POINT;
	}
	for (int i = 0; i < D3DTS_MAX; i++)
	{
		memset(&s_transforms[i], 0, sizeof(D3DMATRIX));
		s_transforms[i]._11 = s_transforms[i]._22 = s_transforms[i]._33 = s_transforms[i]._44 = 1.0f;
	}
	// (Direct3D 9's own defaults match the PC's; send the Xbox's.)
	for (DWORD rs = 0; rs < D3DRS_MAX; rs++)
		if (kRenderStateMap[rs] != 0)
			ApplyRenderState(rs, s_renderStates[rs]);
	for (int s = 0; s < 4; s++)
		for (DWORD t = 0; t < D3DTSS_MAX; t++)
			ApplyTextureStageState(s, t, s_textureStates[s][t]);
	UxGfxSetRenderState(137 /* LIGHTING */, TRUE);
}

static void BackBufferInfo()
{
	UINT w = s_present.BackBufferWidth ? s_present.BackBufferWidth : 640;
	UINT h = s_present.BackBufferHeight ? s_present.BackBufferHeight : 480;
	s_backBuffer.Common = D3DCOMMON_TYPE_SURFACE | 1;
	UxEncodeFormat(0x1E /* LIN_X8R8G8B8 */, w, h, 1, &s_backBuffer.Format, &s_backBuffer.Size);
	s_depthBuffer.Common = D3DCOMMON_TYPE_SURFACE | 1;
	UxEncodeFormat(0x2E /* LIN_D24S8 */, w, h, 1, &s_depthBuffer.Format, &s_depthBuffer.Size);
	Track(&s_backBuffer, KIND_BACKBUFFER);
	Track(&s_depthBuffer, KIND_DEPTH);
	s_viewport.X = 0;
	s_viewport.Y = 0;
	s_viewport.Width = w;
	s_viewport.Height = h;
	s_viewport.MinZ = 0.0f;
	s_viewport.MaxZ = 1.0f;
}

static bool VSync(const D3DPRESENT_PARAMETERS *pp)
{
	return pp->FullScreen_PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE;
}

HRESULT WINAPI Direct3D_CreateDevice(UINT, D3DDEVTYPE, void *, DWORD, D3DPRESENT_PARAMETERS *pp,
                                     D3DDevice **device)
{
	Locker lock;
	s_present = *pp;
	if (s_present.BackBufferWidth == 0)
		s_present.BackBufferWidth = 640;
	if (s_present.BackBufferHeight == 0)
		s_present.BackBufferHeight = 480;
	if (!UxGfxInit(s_present.BackBufferWidth, s_present.BackBufferHeight, VSync(&s_present)))
	{
		UxLog("Direct3D 9: no device");
		return E_FAIL;
	}
	UxGfxSetWidescreen((s_present.Flags & D3DPRESENTFLAG_WIDESCREEN) != 0);
	UxLog("Direct3D 9: %ux%u", s_present.BackBufferWidth, s_present.BackBufferHeight);
	BackBufferInfo();
	DefaultStates();
	static D3DDevice s_device;
	*device = &s_device;
	return S_OK;
}

static void ReapplyDeviceState();

HRESULT WINAPI D3DDevice_Reset(D3DPRESENT_PARAMETERS *pp)
{
	Locker lock;
	s_present = *pp;
	if (s_present.BackBufferWidth == 0)
		s_present.BackBufferWidth = 640;
	if (s_present.BackBufferHeight == 0)
		s_present.BackBufferHeight = 480;
	UxGfxReset(s_present.BackBufferWidth, s_present.BackBufferHeight, VSync(&s_present));
	UxGfxSetWidescreen((s_present.Flags & D3DPRESENTFLAG_WIDESCREEN) != 0);
	BackBufferInfo();
	// (Direct3D 9 forgets its state on a reset.)
	for (DWORD rs = 0; rs < D3DRS_MAX; rs++)
		if (kRenderStateMap[rs] != 0)
			ApplyRenderState(rs, s_renderStates[rs]);
	for (int s = 0; s < 4; s++)
		for (DWORD t = 0; t < D3DTSS_MAX; t++)
			ApplyTextureStageState(s, t, s_textureStates[s][t]);
	// (Fix: the transforms, material and lights too, which a reset also
	// clears; the projection and view were set once, at startup.)
	ReapplyDeviceState();
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetDisplayMode(D3DDISPLAYMODE *mode)
{
	mode->Width = s_present.BackBufferWidth;
	mode->Height = s_present.BackBufferHeight;
	mode->RefreshRate = 60;
	mode->Flags = s_present.Flags;
	mode->Format = D3DFMT_LIN_X8R8G8B8;
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *parameters)
{
	memset(parameters, 0, sizeof(*parameters));
	parameters->AdapterOrdinal = D3DADAPTER_DEFAULT;
	parameters->DeviceType = D3DDEVTYPE_HAL;
	parameters->BehaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
	return S_OK;
}

HRESULT WINAPI D3DDevice_Present(CONST RECT *, CONST RECT *, void *, void *)
{
	D3DDevice_Swap(0);
	return S_OK;
}

DWORD WINAPI D3DDevice_Swap(DWORD)
{
	static DWORD s_frames;
	UxGfxPresent();
	return ++s_frames;
}

D3DSurface *WINAPI D3DDevice_GetBackBuffer2(INT)
{
	D3DResource_AddRef(&s_backBuffer);
	return &s_backBuffer;
}

HRESULT WINAPI D3DDevice_GetRenderTarget(D3DSurface **target)
{
	*target = D3DDevice_GetRenderTarget2();
	return S_OK;
}

D3DSurface *WINAPI D3DDevice_GetRenderTarget2()
{
	D3DResource_AddRef(s_renderTarget);
	return s_renderTarget;
}

HRESULT WINAPI D3DDevice_GetDepthStencilSurface(D3DSurface **surface)
{
	*surface = D3DDevice_GetDepthStencilSurface2();
	return S_OK;
}

D3DSurface *WINAPI D3DDevice_GetDepthStencilSurface2()
{
	D3DResource_AddRef(&s_depthBuffer);
	return &s_depthBuffer;
}

HRESULT WINAPI D3DDevice_SetRenderTarget(D3DSurface *target, D3DSurface *)
{
	// Rendering into textures isn't done here: everything goes to the
	// backbuffer (the dash only switches to its own targets briefly).
	if (target != NULL)
		s_renderTarget = target;
	return S_OK;
}

static D3DTexture *NewTexture(UINT width, UINT height, UINT levels, D3DFORMAT format)
{
	if (width == 0 || height == 0)
		return NULL;
	if (levels == 0)
	{
		levels = 1;
		while ((width >> levels) > 0 && (height >> levels) > 0)
			levels++;
	}
	D3DTexture *texture = new D3DTexture;
	memset(texture, 0, sizeof(*texture));
	texture->Common = D3DCOMMON_TYPE_TEXTURE | D3DCOMMON_D3DCREATED | 1;
	UxEncodeFormat(format, width, height, levels, &texture->Format, &texture->Size);
	UxPixelInfo info;
	UxDescribeContainer(texture, &info);
	Backing &b = Track(texture, KIND_TEXTURE);
	b.memorySize = UxTotalSize(info) + 64;
	b.memory = (BYTE *)calloc(1, b.memorySize);
	texture->Data = (DWORD)(ULONG_PTR)b.memory;
	return texture;
}

HRESULT WINAPI D3DDevice_CreateTexture(UINT width, UINT height, UINT levels, DWORD, D3DFORMAT format, D3DPOOL,
                                       D3DTexture **texture)
{
	Locker lock;
	*texture = NewTexture(width, height, levels, format);
	return *texture != NULL ? S_OK : D3DERR_INVALIDCALL;
}

D3DTexture *WINAPI D3DDevice_CreateTexture2(UINT width, UINT height, UINT, UINT levels, DWORD, D3DFORMAT format,
                                            D3DRESOURCETYPE)
{
	Locker lock;
	return NewTexture(width, height, levels, format);
}

static D3DSurface *NewImageSurface(UINT width, UINT height, D3DFORMAT format)
{
	if (width == 0 || height == 0)
		return NULL;
	D3DSurface *surface = new D3DSurface;
	memset(surface, 0, sizeof(*surface));
	surface->Common = D3DCOMMON_TYPE_SURFACE | D3DCOMMON_D3DCREATED | 1;
	UxEncodeFormat(format, width, height, 1, &surface->Format, &surface->Size);
	UxPixelInfo info;
	UxDescribeContainer(surface, &info);
	Backing &b = Track(surface, KIND_SURFACE);
	b.memorySize = UxTotalSize(info) + 64;
	b.memory = (BYTE *)calloc(1, b.memorySize);
	surface->Data = (DWORD)(ULONG_PTR)b.memory;
	return surface;
}

HRESULT WINAPI D3DDevice_CreateImageSurface(UINT width, UINT height, D3DFORMAT format, D3DSurface **surface)
{
	Locker lock;
	*surface = NewImageSurface(width, height, format);
	return *surface != NULL ? S_OK : D3DERR_INVALIDCALL;
}

D3DSurface *WINAPI D3DDevice_CreateSurface2(UINT width, UINT height, D3DFORMAT format)
{
	Locker lock;
	return NewImageSurface(width, height, format);
}

static D3DResource *NewBuffer(UINT length, bool index)
{
	D3DResource *buffer = index ? (D3DResource *)new D3DIndexBuffer : (D3DResource *)new D3DVertexBuffer;
	memset(buffer, 0, sizeof(D3DResource));
	buffer->Common = (index ? D3DCOMMON_TYPE_INDEXBUFFER : D3DCOMMON_TYPE_VERTEXBUFFER) | D3DCOMMON_D3DCREATED | 1;
	Backing &b = Track(buffer, index ? KIND_INDEXBUFFER : KIND_VERTEXBUFFER);
	b.memorySize = length + 16;
	b.memory = (BYTE *)calloc(1, b.memorySize);
	buffer->Data = (DWORD)(ULONG_PTR)b.memory;
	return buffer;
}

HRESULT WINAPI D3DDevice_CreateVertexBuffer(UINT length, DWORD, DWORD, D3DPOOL, D3DVertexBuffer **buffer)
{
	Locker lock;
	*buffer = (D3DVertexBuffer *)NewBuffer(length, false);
	return S_OK;
}

D3DVertexBuffer *WINAPI D3DDevice_CreateVertexBuffer2(UINT length)
{
	Locker lock;
	return (D3DVertexBuffer *)NewBuffer(length, false);
}

HRESULT WINAPI D3DDevice_CreateIndexBuffer(UINT length, DWORD, D3DFORMAT, D3DPOOL, D3DIndexBuffer **buffer)
{
	Locker lock;
	*buffer = (D3DIndexBuffer *)NewBuffer(length, true);
	return S_OK;
}

D3DIndexBuffer *WINAPI D3DDevice_CreateIndexBuffer2(UINT length)
{
	Locker lock;
	return (D3DIndexBuffer *)NewBuffer(length, true);
}

// The backbuffer read into a surface (screenshots, the dash's own captures).
HRESULT WINAPI D3DDevice_CopyRects(D3DSurface *source, CONST RECT *, UINT, D3DSurface *destination, CONST POINT *)
{
	Locker lock;
	if (source == NULL || destination == NULL)
		return D3DERR_INVALIDCALL;
	UxPixelInfo to;
	UxDescribeContainer(destination, &to);
	std::vector<DWORD> argb((size_t)to.width * to.height, 0xFF000000);
	if (source == &s_backBuffer)
	{
		UxGfxReadBackbuffer(&argb[0], to.width, to.height, to.width * 4);
	}
	else
	{
		UxPixelInfo from;
		UxDescribeContainer(source, &from);
		std::vector<DWORD> src;
		UxDecodeLevel(from, 0, (const BYTE *)(ULONG_PTR)source->Data, src);
		for (UINT y = 0; y < to.height && y < from.height; y++)
			memcpy(&argb[(size_t)y * to.width], &src[(size_t)y * from.width],
			       (to.width < from.width ? to.width : from.width) * 4);
	}
	if (destination->Data != 0)
		UxEncodeLevel(to, 0, (BYTE *)(ULONG_PTR)destination->Data, &argb[0]);
	MarkDirty(destination);
	return S_OK;
}

HRESULT WINAPI D3DDevice_BeginScene()
{
	return S_OK;
}

HRESULT WINAPI D3DDevice_EndScene()
{
	return S_OK;
}

HRESULT WINAPI D3DDevice_Clear(DWORD count, CONST D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	Locker lock;
	if (s_renderTarget != &s_backBuffer)
		return S_OK;
	DWORD pcFlags = ((flags & D3DCLEAR_TARGET) ? 1 : 0) | ((flags & D3DCLEAR_ZBUFFER) ? 2 : 0) |
	                ((flags & D3DCLEAR_STENCIL) ? 4 : 0);
	UxGfxClear(rects != NULL ? count : 0, (const long *)rects, pcFlags, color, z, stencil);
	return S_OK;
}

static DWORD TransformType(D3DTRANSFORMSTATETYPE state)
{
	switch (state)
	{
	case D3DTS_VIEW: return 2;
	case D3DTS_PROJECTION: return 3;
	case D3DTS_TEXTURE0: case D3DTS_TEXTURE1: case D3DTS_TEXTURE2: case D3DTS_TEXTURE3:
		return 16 + (state - D3DTS_TEXTURE0);
	case D3DTS_WORLD: return 256;
	case D3DTS_WORLD1: return 257;
	case D3DTS_WORLD2: return 258;
	case D3DTS_WORLD3: return 259;
	default: return 256;
	}
}

HRESULT WINAPI D3DDevice_SetTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX *matrix)
{
	Locker lock;
	if ((UINT)state >= D3DTS_MAX || matrix == NULL)
		return D3DERR_INVALIDCALL;
	s_transforms[state] = *matrix;
	UxGfxSetTransform(TransformType(state), &matrix->_11);
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
	if ((UINT)state >= D3DTS_MAX)
		return D3DERR_INVALIDCALL;
	*matrix = s_transforms[state];
	return S_OK;
}

HRESULT WINAPI D3DDevice_SetViewport(CONST D3DVIEWPORT8 *viewport)
{
	Locker lock;
	s_viewport = *viewport;
	UxGfxSetViewport(viewport->X, viewport->Y, viewport->Width, viewport->Height, viewport->MinZ, viewport->MaxZ);
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetViewport(D3DVIEWPORT8 *viewport)
{
	*viewport = s_viewport;
	return S_OK;
}

// The material and lights, kept to send again after a reset (Direct3D 9
// forgets them).
static D3DMATERIAL8 s_material;
static bool s_materialSet;
#define UX_LIGHTS 8
static D3DLIGHT8 s_lights[UX_LIGHTS];
static bool s_lightSet[UX_LIGHTS];
static BOOL s_lightEnabled[UX_LIGHTS];

HRESULT WINAPI D3DDevice_SetMaterial(CONST D3DMATERIAL8 *material)
{
	Locker lock;
	if (material != NULL)
	{
		s_material = *material;
		s_materialSet = true;
	}
	UxGfxSetMaterial(material);
	return S_OK;
}

HRESULT WINAPI D3DDevice_SetLight(DWORD index, CONST D3DLIGHT8 *light)
{
	Locker lock;
	if (index < UX_LIGHTS && light != NULL)
	{
		s_lights[index] = *light;
		s_lightSet[index] = true;
	}
	UxGfxSetLight(index, light);
	return S_OK;
}

HRESULT WINAPI D3DDevice_LightEnable(DWORD index, BOOL enable)
{
	Locker lock;
	if (index < UX_LIGHTS)
		s_lightEnabled[index] = enable;
	UxGfxLightEnable(index, enable);
	return S_OK;
}

// After a reset: the transforms, material, lights and viewport again.
static void ReapplyDeviceState()
{
	for (int i = 0; i < D3DTS_MAX; i++)
		UxGfxSetTransform(TransformType((D3DTRANSFORMSTATETYPE)i), &s_transforms[i]._11);
	if (s_materialSet)
		UxGfxSetMaterial(&s_material);
	for (DWORD i = 0; i < UX_LIGHTS; i++)
	{
		if (s_lightSet[i])
			UxGfxSetLight(i, &s_lights[i]);
		if (s_lightEnabled[i])
			UxGfxLightEnable(i, TRUE);
	}
	UxGfxSetViewport(s_viewport.X, s_viewport.Y, s_viewport.Width, s_viewport.Height, s_viewport.MinZ, s_viewport.MaxZ);
}

HRESULT WINAPI D3DDevice_SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
	Locker lock;
	if ((UINT)state >= D3DRS_MAX)
		return D3DERR_INVALIDCALL;
	s_renderStates[state] = value;
	ApplyRenderState(state, value);
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetRenderState(D3DRENDERSTATETYPE state, DWORD *value)
{
	if ((UINT)state >= D3DRS_MAX)
		return D3DERR_INVALIDCALL;
	*value = s_renderStates[state];
	return S_OK;
}

HRESULT WINAPI D3DDevice_SetTexture(DWORD stage, D3DBaseTexture *texture)
{
	Locker lock;
	if (stage >= 4)
		return D3DERR_INVALIDCALL;
	if (texture != NULL)
		D3DResource_AddRef(texture);
	if (s_textures[stage] != NULL)
		D3DResource_Release(s_textures[stage]);
	s_textures[stage] = texture;
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetTexture(DWORD stage, D3DBaseTexture **texture)
{
	*texture = stage < 4 ? s_textures[stage] : NULL;
	if (*texture != NULL)
		D3DResource_AddRef(*texture);
	return S_OK;
}

HRESULT WINAPI D3DDevice_SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	Locker lock;
	if (stage >= 4 || (UINT)type >= D3DTSS_MAX)
		return D3DERR_INVALIDCALL;
	s_textureStates[stage][type] = value;
	ApplyTextureStageState(stage, type, value);
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD *value)
{
	if (stage >= 4 || (UINT)type >= D3DTSS_MAX)
		return D3DERR_INVALIDCALL;
	*value = s_textureStates[stage][type];
	return S_OK;
}

void WINAPI D3DDevice_SetTextureState_TexCoordIndex(DWORD stage, DWORD value)
{
	D3DDevice_SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, value);
}

HRESULT WINAPI D3DDevice_DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitives, CONST void *vertices, UINT stride)
{
	Locker lock;
	Draw(type, VerticesFor(type, primitives), (const BYTE *)vertices, stride, CurrentFvf(), NULL);
	return S_OK;
}

HRESULT WINAPI D3DDevice_DrawVerticesUP(D3DPRIMITIVETYPE type, UINT count, CONST void *vertices, UINT stride)
{
	Locker lock;
	Draw(type, count, (const BYTE *)vertices, stride, CurrentFvf(), NULL);
	return S_OK;
}

static const BYTE *StreamData()
{
	return s_streamSource != NULL ? (const BYTE *)(ULONG_PTR)s_streamSource->Data : NULL;
}

HRESULT WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE type, UINT start, UINT count)
{
	Locker lock;
	const BYTE *data = StreamData();
	if (data != NULL)
		Draw(type, count, data + (size_t)start * s_streamStride, s_streamStride, CurrentFvf(), NULL);
	return S_OK;
}

HRESULT WINAPI D3DDevice_DrawPrimitive(D3DPRIMITIVETYPE type, UINT start, UINT primitives)
{
	return D3DDevice_DrawVertices(type, start, VerticesFor(type, primitives));
}

HRESULT WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE type, UINT count, CONST WORD *indices)
{
	Locker lock;
	const BYTE *data = StreamData();
	if (data != NULL && indices != NULL)
		Draw(type, count, data + (size_t)s_baseVertex * s_streamStride, s_streamStride, CurrentFvf(), indices);
	return S_OK;
}

HRESULT WINAPI D3DDevice_DrawIndexedPrimitive(D3DPRIMITIVETYPE type, UINT, UINT, UINT startIndex, UINT primitives)
{
	if (s_indices == NULL)
		return D3DERR_INVALIDCALL;
	const WORD *indices = (const WORD *)(ULONG_PTR)s_indices->Data;
	return D3DDevice_DrawIndexedVertices(type, VerticesFor(type, primitives), indices + startIndex);
}

// XGAssembleShader keeps the source (XGraphics.cpp); the font's is recognised.
HRESULT WINAPI D3DDevice_CreateVertexShader(CONST DWORD *, CONST DWORD *function, DWORD *handle, DWORD)
{
	Locker lock;
	Program program;
	const char *source = (const char *)function;
	program.screenSpaceFont = source != NULL && strstr(source, "screenspace") != NULL;
	s_programs.push_back(program);
	*handle = (DWORD)(s_programs.size() << 1) | 1;
	return S_OK;
}

HRESULT WINAPI D3DDevice_SetVertexShader(DWORD handle)
{
	Locker lock;
	s_vertexShader = handle;
	return S_OK;
}

HRESULT WINAPI D3DDevice_DeleteVertexShader(DWORD)
{
	return S_OK;
}

HRESULT WINAPI D3DDevice_SetVertexShaderConstant(INT, CONST void *, DWORD)
{
	return S_OK;   // (only programs read them; the font's doesn't)
}

HRESULT WINAPI D3DDevice_SetStreamSource(UINT stream, D3DVertexBuffer *buffer, UINT stride)
{
	Locker lock;
	if (stream != 0)
		return S_OK;
	s_streamSource = buffer;
	s_streamStride = stride;
	return S_OK;
}

HRESULT WINAPI D3DDevice_SetIndices(D3DIndexBuffer *indices, UINT baseVertex)
{
	Locker lock;
	s_indices = indices;
	s_baseVertex = baseVertex;
	return S_OK;
}

HRESULT WINAPI D3DDevice_CreatePixelShader(CONST D3DPIXELSHADERDEF *, DWORD *handle)
{
	*handle = 1;   // the font's: see ApplyForDraw
	return S_OK;
}

HRESULT WINAPI D3DDevice_SetPixelShader(DWORD handle)
{
	Locker lock;
	s_pixelShader = handle;
	return S_OK;
}

HRESULT WINAPI D3DDevice_DeletePixelShader(DWORD)
{
	return S_OK;
}

HRESULT WINAPI D3DDevice_Begin(D3DPRIMITIVETYPE type)
{
	Locker lock;
	s_inBegin = true;
	s_immediateType = type;
	s_immediate.clear();
	return S_OK;
}

static void EmitImmediate(float x, float y, float z, float w)
{
	ImmediateVertex v = s_current;
	if (IsProgram(s_vertexShader))
	{
		// The font's program: v0 = x, y, u, v in screen space.
		v.x = x;
		v.y = y;
		v.z = 0.0f;
		v.rhw = 1.0f;
		v.u0 = z;
		v.v0 = w;
	}
	else
	{
		v.x = x;
		v.y = y;
		v.z = z;
		v.rhw = w;
	}
	s_immediate.push_back(v);
}

HRESULT WINAPI D3DDevice_SetVertexData4f(INT reg, float a, float b, float c, float d)
{
	Locker lock;
	switch (reg)
	{
	case D3DVSDE_VERTEX: EmitImmediate(a, b, c, d); break;
	case D3DVSDE_POSITION: s_current.x = a; s_current.y = b; s_current.z = c; s_current.rhw = d; break;
	case D3DVSDE_DIFFUSE:
	{
		int r = (int)(a * 255.0f + 0.5f), g = (int)(b * 255.0f + 0.5f), bl = (int)(c * 255.0f + 0.5f),
		    al = (int)(d * 255.0f + 0.5f);
		s_current.color = ((DWORD)(al & 255) << 24) | ((r & 255) << 16) | ((g & 255) << 8) | (bl & 255);
		break;
	}
	case D3DVSDE_TEXCOORD0: s_current.u0 = a; s_current.v0 = b; break;
	case D3DVSDE_TEXCOORD1: s_current.u1 = a; s_current.v1 = b; break;
	default: break;
	}
	return S_OK;
}

HRESULT WINAPI D3DDevice_SetVertexData2f(INT reg, float a, float b)
{
	if (reg == D3DVSDE_VERTEX)
		return D3DDevice_SetVertexData4f(reg, a, b, 0.0f, 1.0f);
	return D3DDevice_SetVertexData4f(reg, a, b, 0.0f, 1.0f);
}

HRESULT WINAPI D3DDevice_SetVertexDataColor(INT reg, D3DCOLOR color)
{
	Locker lock;
	if (reg == D3DVSDE_DIFFUSE)
		s_current.color = color;
	return S_OK;
}

HRESULT WINAPI D3DDevice_End()
{
	Locker lock;
	if (!s_inBegin)
		return S_OK;
	s_inBegin = false;
	if (s_immediate.empty())
		return S_OK;
	// The vertices as XYZRHW (or XYZ, for an FVF with it) | DIFFUSE | TEX2.
	DWORD fvf = (IsProgram(s_vertexShader) || (s_vertexShader & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW)
	                ? D3DFVF_XYZRHW
	                : D3DFVF_XYZ;
	if (fvf == D3DFVF_XYZ)
	{
		// (XYZ vertices are 12 bytes: move the rest up.)
		std::vector<BYTE> packed(s_immediate.size() * 32);
		for (size_t i = 0; i < s_immediate.size(); i++)
		{
			const ImmediateVertex &v = s_immediate[i];
			float *p = (float *)&packed[i * 32];
			p[0] = v.x;
			p[1] = v.y;
			p[2] = v.z;
			memcpy(&p[3], &v.color, 4);
			p[4] = v.u0;
			p[5] = v.v0;
			p[6] = v.u1;
			p[7] = v.v1;
		}
		Draw(s_immediateType, (UINT)s_immediate.size(), &packed[0], 32,
		     D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2, NULL);
	}
	else
	{
		Draw(s_immediateType, (UINT)s_immediate.size(), (const BYTE *)&s_immediate[0], sizeof(ImmediateVertex),
		     D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2, NULL);
	}
	return S_OK;
}

HRESULT WINAPI D3DDevice_UpdateOverlay(D3DSurface *, CONST RECT *, CONST RECT *, BOOL, D3DCOLOR)
{
	return S_OK;   // (no video overlay: videos aren't played on Windows)
}

HRESULT WINAPI D3DDevice_EnableOverlay(BOOL)
{
	return S_OK;
}

BOOL WINAPI D3DDevice_GetOverlayUpdateStatus()
{
	return TRUE;
}

HRESULT WINAPI D3DDevice_SetScissors(DWORD count, BOOL exclusive, CONST D3DRECT *rects)
{
	Locker lock;
	if (count == 0 || rects == NULL || exclusive)
		UxGfxSetScissor(0, 0, 0, 0, 0);
	else
		UxGfxSetScissor(1, rects[0].x1, rects[0].y1, rects[0].x2, rects[0].y2);
	return S_OK;
}

BOOL WINAPI D3DDevice_IsBusy()
{
	return FALSE;
}

void WINAPI D3DDevice_BlockUntilIdle()
{
}

void WINAPI D3DDevice_BlockUntilVerticalBlank()
{
	UxGfxWaitVBlank();
}

void WINAPI D3DDevice_SetFlickerFilter(DWORD)
{
}

void WINAPI D3DDevice_SetSoftDisplayFilter(BOOL)
{
}

HRESULT WINAPI D3DDevice_PersistDisplay()
{
	return S_OK;
}

void WINAPI D3DDevice_SetGammaRamp(DWORD, CONST D3DGAMMARAMP *)
{
}

// ---- resources ----

ULONG WINAPI D3DResource_AddRef(D3DResource *resource)
{
	if (resource == NULL)
		return 0;
	Locker lock;
	DWORD count = (resource->Common & D3DCOMMON_REFCOUNT_MASK) + 1;
	resource->Common = (resource->Common & ~D3DCOMMON_REFCOUNT_MASK) | (count & D3DCOMMON_REFCOUNT_MASK);
	return count;
}

ULONG WINAPI D3DResource_Release(D3DResource *resource)
{
	if (resource == NULL)
		return 0;
	Locker lock;
	DWORD count = resource->Common & D3DCOMMON_REFCOUNT_MASK;
	if (count > 0)
		count--;
	resource->Common = (resource->Common & ~D3DCOMMON_REFCOUNT_MASK) | count;
	if (count != 0)
		return count;
	if (resource == &s_backBuffer || resource == &s_depthBuffer)
	{
		resource->Common |= 1;
		return 1;
	}
	for (int i = 0; i < 4; i++)
		if (s_textures[i] == resource)
			s_textures[i] = NULL;
	if (s_streamSource == resource)
		s_streamSource = NULL;
	if (s_indices == resource)
		s_indices = NULL;
	if (s_renderTarget == resource)
		s_renderTarget = &s_backBuffer;
	Backing *b = Find(resource);
	bool created = (resource->Common & D3DCOMMON_D3DCREATED) != 0;
	if (b != NULL)
	{
		if (b->gpu != NULL)
			UxGfxReleaseTexture(b->gpu);
		free(b->memory);
		D3DTexture *parent = b->parent;
		s_resources.erase(resource);
		if (parent != NULL)
			D3DResource_Release(parent);
	}
	if (created)
	{
		// (Made by the device: with new, as the right type.)
		switch (resource->Common & D3DCOMMON_TYPE_MASK)
		{
		case D3DCOMMON_TYPE_TEXTURE: delete (D3DTexture *)resource; break;
		case D3DCOMMON_TYPE_SURFACE: delete (D3DSurface *)resource; break;
		case D3DCOMMON_TYPE_INDEXBUFFER: delete (D3DIndexBuffer *)resource; break;
		default: delete (D3DVertexBuffer *)resource; break;
		}
	}
	return 0;
}

D3DRESOURCETYPE WINAPI D3DResource_GetType(D3DResource *resource)
{
	switch (resource->Common & D3DCOMMON_TYPE_MASK)
	{
	case D3DCOMMON_TYPE_VERTEXBUFFER: return D3DRTYPE_VERTEXBUFFER;
	case D3DCOMMON_TYPE_INDEXBUFFER: return D3DRTYPE_INDEXBUFFER;
	case D3DCOMMON_TYPE_PALETTE: return D3DRTYPE_PALETTE;
	case D3DCOMMON_TYPE_SURFACE: return D3DRTYPE_SURFACE;
	case D3DCOMMON_TYPE_PUSHBUFFER: return D3DRTYPE_PUSHBUFFER;
	case D3DCOMMON_TYPE_FIXUP: return D3DRTYPE_FIXUP;
	case D3DCOMMON_TYPE_TEXTURE:
	{
		DWORD dims = (((D3DPixelContainer *)resource)->Format & D3DFORMAT_DIMENSION_MASK) >> D3DFORMAT_DIMENSION_SHIFT;
		if (((D3DPixelContainer *)resource)->Format & D3DFORMAT_CUBEMAP)
			return D3DRTYPE_CUBETEXTURE;
		return dims == 3 ? D3DRTYPE_VOLUMETEXTURE : D3DRTYPE_TEXTURE;
	}
	default: return D3DRTYPE_NONE;
	}
}

BOOL WINAPI D3DResource_IsBusy(D3DResource *)
{
	return FALSE;
}

void WINAPI D3DResource_BlockUntilNotBusy(D3DResource *)
{
}

// An .xpr's resource in place: Data is the offset of its data from the base.
void WINAPI D3DResource_Register(D3DResource *resource, void *base)
{
	Locker lock;
	resource->Data = (DWORD)(ULONG_PTR)((BYTE *)base + resource->Data);
	Backing *b = Find(resource);
	if (b != NULL)
		b->dirty = true;
}

DWORD WINAPI D3DBaseTexture_GetLevelCount(D3DBaseTexture *texture)
{
	UxPixelInfo info;
	UxDescribeContainer(texture, &info);
	return info.levels;
}

static void LevelDesc(const UxPixelInfo &info, UINT level, D3DRESOURCETYPE type, D3DSURFACE_DESC *desc)
{
	desc->Format = (D3DFORMAT)info.format;
	desc->Type = type;
	desc->Usage = 0;
	desc->Size = UxLevelSize(info, level);
	desc->MultiSampleType = D3DMULTISAMPLE_NONE;
	desc->Width = UxLevelWidth(info, level);
	desc->Height = UxLevelHeight(info, level);
}

HRESULT WINAPI D3DTexture_GetLevelDesc(D3DTexture *texture, UINT level, D3DSURFACE_DESC *desc)
{
	UxPixelInfo info;
	UxDescribeContainer(texture, &info);
	if (level >= info.levels)
		return D3DERR_INVALIDCALL;
	LevelDesc(info, level, D3DRTYPE_SURFACE, desc);
	return S_OK;
}

D3DSurface *WINAPI D3DTexture_GetSurfaceLevel2(D3DTexture *texture, UINT level)
{
	Locker lock;
	UxPixelInfo info;
	UxDescribeContainer(texture, &info);
	if (level >= info.levels)
		return NULL;
	D3DSurface *surface = new D3DSurface;
	memset(surface, 0, sizeof(*surface));
	surface->Common = D3DCOMMON_TYPE_SURFACE | D3DCOMMON_D3DCREATED | 1;
	// The level as a surface of its own: the same format, its size.
	UINT w = UxLevelWidth(info, level), h = UxLevelHeight(info, level);
	if (info.linear || texture->Size != 0)
	{
		surface->Format = texture->Format;
		surface->Size = texture->Size;
	}
	else
	{
		UxEncodeFormat(info.format, w, h, 1, &surface->Format, &surface->Size);
	}
	surface->Data = texture->Data + UxLevelOffset(info, level);
	surface->Parent = texture;
	Backing &b = Track(surface, KIND_SURFACE);
	b.parent = texture;
	b.level = level;
	D3DResource_AddRef(texture);
	return surface;
}

HRESULT WINAPI D3DTexture_GetSurfaceLevel(D3DTexture *texture, UINT level, D3DSurface **surface)
{
	*surface = D3DTexture_GetSurfaceLevel2(texture, level);
	return *surface != NULL ? S_OK : D3DERR_INVALIDCALL;
}

HRESULT WINAPI D3DTexture_LockRect(D3DTexture *texture, UINT level, D3DLOCKED_RECT *locked, CONST RECT *, DWORD)
{
	Locker lock;
	UxPixelInfo info;
	UxDescribeContainer(texture, &info);
	if (level >= info.levels || texture->Data == 0)
	{
		locked->pBits = NULL;
		locked->Pitch = 0;
		return D3DERR_INVALIDCALL;
	}
	locked->pBits = (BYTE *)(ULONG_PTR)texture->Data + UxLevelOffset(info, level);
	locked->Pitch = UxLevelPitch(info, level);
	MarkDirty(texture);   // (the dash never unlocks: anything locked counts as changed)
	return S_OK;
}

HRESULT WINAPI D3DTexture_UnlockRect(D3DTexture *texture, UINT)
{
	MarkDirty(texture);
	return S_OK;
}

HRESULT WINAPI D3DSurface_GetDesc(D3DSurface *surface, D3DSURFACE_DESC *desc)
{
	UxPixelInfo info;
	UxDescribeContainer(surface, &info);
	LevelDesc(info, 0, D3DRTYPE_SURFACE, desc);
	return S_OK;
}

HRESULT WINAPI D3DSurface_LockRect(D3DSurface *surface, D3DLOCKED_RECT *locked, CONST RECT *, DWORD)
{
	Locker lock;
	UxPixelInfo info;
	UxDescribeContainer(surface, &info);
	if (surface == &s_backBuffer || surface == &s_depthBuffer || surface->Data == 0)
	{
		locked->pBits = NULL;
		locked->Pitch = 0;
		return D3DERR_INVALIDCALL;
	}
	locked->pBits = (BYTE *)(ULONG_PTR)surface->Data;
	locked->Pitch = UxLevelPitch(info, 0);
	MarkDirty(surface);
	return S_OK;
}

HRESULT WINAPI D3DSurface_UnlockRect(D3DSurface *surface)
{
	MarkDirty(surface);
	return S_OK;
}

HRESULT WINAPI D3DSurface_GetContainer(D3DSurface *surface, D3DBaseTexture **texture)
{
	*texture = surface->Parent;
	if (*texture != NULL)
		D3DResource_AddRef(*texture);
	return *texture != NULL ? S_OK : D3DERR_INVALIDCALL;
}

HRESULT WINAPI D3DVertexBuffer_Lock(D3DVertexBuffer *buffer, UINT offset, UINT, BYTE **data, DWORD)
{
	*data = buffer->Data != 0 ? (BYTE *)(ULONG_PTR)buffer->Data + offset : NULL;
	return *data != NULL ? S_OK : D3DERR_INVALIDCALL;
}

BYTE *WINAPI D3DVertexBuffer_Lock2(D3DVertexBuffer *buffer, DWORD)
{
	return (BYTE *)(ULONG_PTR)buffer->Data;
}

HRESULT WINAPI D3DVertexBuffer_Unlock(D3DVertexBuffer *)
{
	return S_OK;
}

HRESULT WINAPI D3DIndexBuffer_Lock(D3DIndexBuffer *buffer, UINT offset, UINT, BYTE **data, DWORD)
{
	*data = buffer->Data != 0 ? (BYTE *)(ULONG_PTR)buffer->Data + offset : NULL;
	return *data != NULL ? S_OK : D3DERR_INVALIDCALL;
}

HRESULT WINAPI D3DIndexBuffer_Unlock(D3DIndexBuffer *)
{
	return S_OK;
}

void *WINAPI D3D_AllocContiguousMemory(DWORD size, DWORD)
{
	return calloc(1, size + 64);
}

void WINAPI D3D_FreeContiguousMemory(void *memory)
{
	free(memory);
}

// The screen a title left behind (the Xbox keeps it across launches).
D3DSurface *WINAPI AvGetSavedDataAddress()
{
	return NULL;
}

}   // extern "C"

// The XDK's internal surface description, which the dash calls directly.
namespace D3D { namespace PixelJar {
void __stdcall Get2DSurfaceDesc(D3DPixelContainer *container, UINT level, D3DSURFACE_DESC *desc)
{
	UxPixelInfo info;
	UxDescribeContainer(container, &info);
	LevelDesc(info, level < info.levels ? level : 0,
	          (container->Common & D3DCOMMON_TYPE_MASK) == D3DCOMMON_TYPE_SURFACE ? D3DRTYPE_SURFACE : D3DRTYPE_TEXTURE,
	          desc);
}
} }

// For D3DX (XD3DX8.cpp): a surface's or texture's level changed.
void UxMarkDirty(D3DResource *resource)
{
	Locker lock;
	MarkDirty(resource);
}

bool UxIsBackBuffer(D3DResource *resource)
{
	return resource == &s_backBuffer;
}
