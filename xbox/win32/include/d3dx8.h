// D3DX 8 (the Xbox's) for the Windows build: RXDK's math, core and texture
// headers (GPL-3.0-or-later, Team Resurgent; portions (c) Microsoft) and the
// two mesh helpers the XDK sample framework calls. Implemented in
// xbox/win32/XD3DX8.cpp.
#ifndef __D3DX8_H__
#define __D3DX8_H__

#include <d3d8.h>
#include <limits.h>
#include <float.h>

#ifndef D3DXINLINE
#ifdef __cplusplus
#define D3DXINLINE inline
#else
#define D3DXINLINE static __inline
#endif
#endif

#define D3DX_DEFAULT ULONG_MAX
#define D3DX_DEFAULT_FLOAT FLT_MAX

#define _FACDD 0x876
#define MAKE_DDHRESULT(code) MAKE_HRESULT(1, _FACDD, code)
enum _D3DXERR
{
	D3DXERR_CANNOTMODIFYINDEXBUFFER = MAKE_DDHRESULT(2900),
	D3DXERR_INVALIDMESH = MAKE_DDHRESULT(2901),
	D3DXERR_CANNOTATTRSORT = MAKE_DDHRESULT(2902),
	D3DXERR_SKINNINGNOTSUPPORTED = MAKE_DDHRESULT(2903),
	D3DXERR_TOOMANYINFLUENCES = MAKE_DDHRESULT(2904),
	D3DXERR_INVALIDDATA = MAKE_DDHRESULT(2905),
};

#include "d3dx8math.h"
#include "d3dx8core.h"
#include "d3dx8tex.h"

#ifdef __cplusplus
extern "C" {
#endif
HRESULT WINAPI D3DXComputeBoundingSphere(PVOID pPointsFVF, DWORD NumVertices, DWORD FVF, D3DXVECTOR3 *pCenter,
                                         FLOAT *pRadius);
#ifdef __cplusplus
}
#endif

#endif
