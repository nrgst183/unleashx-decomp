// The Xbox's Direct3D 8 for the Windows build (xbox/win32): the XDK's API
// shape (static device methods, resources with the Xbox's in-memory header
// so .xpr files can be registered in place, D3DDevice_* C functions), the
// types and constants from RXDK's d3d8types.h. The functions are in
// xbox/win32/XD3D8.cpp, drawing with Direct3D 9.
#pragma once

#include <stdlib.h>

#define D3DFASTCALL __fastcall
#define D3D_SDK_VERSION 0

typedef struct Direct3D Direct3D;
typedef struct D3DDevice D3DDevice;
typedef struct D3DResource D3DResource;
typedef struct D3DBaseTexture D3DBaseTexture;
typedef struct D3DTexture D3DTexture;
typedef struct D3DVolumeTexture D3DVolumeTexture;
typedef struct D3DCubeTexture D3DCubeTexture;
typedef struct D3DVertexBuffer D3DVertexBuffer;
typedef struct D3DIndexBuffer D3DIndexBuffer;
typedef struct D3DPalette D3DPalette;
typedef struct D3DSurface D3DSurface;
typedef struct D3DVolume D3DVolume;
typedef struct D3DPushBuffer D3DPushBuffer;
typedef struct D3DFixup D3DFixup;

#define IDirect3D8 Direct3D
#define IDirect3DDevice8 D3DDevice
#define IDirect3DResource8 D3DResource
#define IDirect3DBaseTexture8 D3DBaseTexture
#define IDirect3DTexture8 D3DTexture
#define IDirect3DVolumeTexture8 D3DVolumeTexture
#define IDirect3DCubeTexture8 D3DCubeTexture
#define IDirect3DVertexBuffer8 D3DVertexBuffer
#define IDirect3DIndexBuffer8 D3DIndexBuffer
#define IDirect3DPalette8 D3DPalette
#define IDirect3DSurface8 D3DSurface
#define IDirect3DVolume8 D3DVolume
#define IDirect3DPushBuffer8 D3DPushBuffer
#define IDirect3DFixup8 D3DFixup

typedef struct Direct3D *LPDIRECT3D8, *PDIRECT3D8;
typedef struct D3DDevice *LPDIRECT3DDEVICE8, *PDIRECT3DDEVICE8;
typedef struct D3DResource *LPDIRECT3DRESOURCE8, *PDIRECT3DRESOURCE8;
typedef struct D3DBaseTexture *LPDIRECT3DBASETEXTURE8, *PDIRECT3DBASETEXTURE8;
typedef struct D3DTexture *LPDIRECT3DTEXTURE8, *PDIRECT3DTEXTURE8;
typedef struct D3DVertexBuffer *LPDIRECT3DVERTEXBUFFER8, *PDIRECT3DVERTEXBUFFER8;
typedef struct D3DIndexBuffer *LPDIRECT3DINDEXBUFFER8, *PDIRECT3DINDEXBUFFER8;
typedef struct D3DSurface *LPDIRECT3DSURFACE8, *PDIRECT3DSURFACE8;
typedef struct D3DCubeTexture *LPDIRECT3DCUBETEXTURE8, *PDIRECT3DCUBETEXTURE8;
typedef struct D3DVolumeTexture *LPDIRECT3DVOLUMETEXTURE8, *PDIRECT3DVOLUMETEXTURE8;
typedef struct D3DVolume *LPDIRECT3DVOLUME8, *PDIRECT3DVOLUME8;
typedef struct D3DPalette *LPDIRECT3DPALETTE8, *PDIRECT3DPALETTE8;

#include "d3d8types.h"
#include "d3d8caps.h"
#include "d3d8_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

Direct3D *WINAPI Direct3DCreate8(UINT SDKVersion);
HRESULT WINAPI Direct3D_CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, void *pUnused, DWORD BehaviorFlags,
                                     D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDevice **ppReturnedDeviceInterface);

// The device.
HRESULT WINAPI D3DDevice_GetDisplayMode(D3DDISPLAYMODE *pMode);
HRESULT WINAPI D3DDevice_GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters);
HRESULT WINAPI D3DDevice_Reset(D3DPRESENT_PARAMETERS *pPresentationParameters);
HRESULT WINAPI D3DDevice_Present(CONST RECT *pSourceRect, CONST RECT *pDestRect, void *pUnused, void *pUnused2);
D3DSurface *WINAPI D3DDevice_GetBackBuffer2(INT BackBuffer);
HRESULT WINAPI D3DDevice_CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format,
                                       D3DPOOL Pool, D3DTexture **ppTexture);
D3DTexture *WINAPI D3DDevice_CreateTexture2(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage,
                                            D3DFORMAT Format, D3DRESOURCETYPE D3DType);
HRESULT WINAPI D3DDevice_CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
                                            D3DVertexBuffer **ppVertexBuffer);
D3DVertexBuffer *WINAPI D3DDevice_CreateVertexBuffer2(UINT Length);
HRESULT WINAPI D3DDevice_CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                           D3DIndexBuffer **ppIndexBuffer);
D3DIndexBuffer *WINAPI D3DDevice_CreateIndexBuffer2(UINT Length);
HRESULT WINAPI D3DDevice_CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DSurface **ppSurface);
D3DSurface *WINAPI D3DDevice_CreateSurface2(UINT Width, UINT Height, D3DFORMAT Format);
HRESULT WINAPI D3DDevice_CopyRects(D3DSurface *pSourceSurface, CONST RECT *pSourceRectsArray, UINT cRects,
                                   D3DSurface *pDestinationSurface, CONST POINT *pDestPointsArray);
HRESULT WINAPI D3DDevice_SetRenderTarget(D3DSurface *pRenderTarget, D3DSurface *pNewZStencil);
HRESULT WINAPI D3DDevice_GetRenderTarget(D3DSurface **ppRenderTarget);
D3DSurface *WINAPI D3DDevice_GetRenderTarget2();
HRESULT WINAPI D3DDevice_GetDepthStencilSurface(D3DSurface **ppZStencilSurface);
D3DSurface *WINAPI D3DDevice_GetDepthStencilSurface2();
HRESULT WINAPI D3DDevice_BeginScene();
HRESULT WINAPI D3DDevice_EndScene();
HRESULT WINAPI D3DDevice_Clear(DWORD Count, CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z,
                               DWORD Stencil);
HRESULT WINAPI D3DDevice_SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix);
HRESULT WINAPI D3DDevice_GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix);
HRESULT WINAPI D3DDevice_SetViewport(CONST D3DVIEWPORT8 *pViewport);
HRESULT WINAPI D3DDevice_GetViewport(D3DVIEWPORT8 *pViewport);
HRESULT WINAPI D3DDevice_SetMaterial(CONST D3DMATERIAL8 *pMaterial);
HRESULT WINAPI D3DDevice_SetLight(DWORD Index, CONST D3DLIGHT8 *pLight);
HRESULT WINAPI D3DDevice_LightEnable(DWORD Index, BOOL Enable);
HRESULT WINAPI D3DDevice_SetRenderState(D3DRENDERSTATETYPE State, DWORD Value);
HRESULT WINAPI D3DDevice_GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue);
HRESULT WINAPI D3DDevice_SetTexture(DWORD Stage, D3DBaseTexture *pTexture);
HRESULT WINAPI D3DDevice_GetTexture(DWORD Stage, D3DBaseTexture **ppTexture);
HRESULT WINAPI D3DDevice_SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value);
HRESULT WINAPI D3DDevice_GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue);
void WINAPI D3DDevice_SetTextureState_TexCoordIndex(DWORD Stage, DWORD Value);
HRESULT WINAPI D3DDevice_DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount);
HRESULT WINAPI D3DDevice_DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT MinIndex, UINT NumIndices,
                                              UINT StartIndex, UINT PrimitiveCount);
HRESULT WINAPI D3DDevice_DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
                                         CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride);
HRESULT WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT VertexCount);
HRESULT WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE PrimitiveType, UINT VertexCount,
                                             CONST WORD *pIndexData);
HRESULT WINAPI D3DDevice_DrawVerticesUP(D3DPRIMITIVETYPE PrimitiveType, UINT VertexCount,
                                        CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride);
HRESULT WINAPI D3DDevice_CreateVertexShader(CONST DWORD *pDeclaration, CONST DWORD *pFunction, DWORD *pHandle,
                                            DWORD Usage);
HRESULT WINAPI D3DDevice_SetVertexShader(DWORD Handle);
HRESULT WINAPI D3DDevice_DeleteVertexShader(DWORD Handle);
HRESULT WINAPI D3DDevice_SetVertexShaderConstant(INT Register, CONST void *pConstantData, DWORD ConstantCount);
HRESULT WINAPI D3DDevice_SetStreamSource(UINT StreamNumber, D3DVertexBuffer *pStreamData, UINT Stride);
HRESULT WINAPI D3DDevice_SetIndices(D3DIndexBuffer *pIndexData, UINT BaseVertexIndex);
HRESULT WINAPI D3DDevice_CreatePixelShader(CONST D3DPIXELSHADERDEF *pPSDef, DWORD *pHandle);
HRESULT WINAPI D3DDevice_SetPixelShader(DWORD Handle);
HRESULT WINAPI D3DDevice_DeletePixelShader(DWORD Handle);
HRESULT WINAPI D3DDevice_SetVertexData2f(INT Register, float a, float b);
HRESULT WINAPI D3DDevice_SetVertexData4f(INT Register, float a, float b, float c, float d);
HRESULT WINAPI D3DDevice_SetVertexDataColor(INT Register, D3DCOLOR Color);
HRESULT WINAPI D3DDevice_Begin(D3DPRIMITIVETYPE PrimitiveType);
HRESULT WINAPI D3DDevice_End();
HRESULT WINAPI D3DDevice_UpdateOverlay(D3DSurface *pSurface, CONST RECT *SrcRect, CONST RECT *DstRect,
                                       BOOL EnableColorKey, D3DCOLOR ColorKey);
HRESULT WINAPI D3DDevice_EnableOverlay(BOOL Enable);
BOOL WINAPI D3DDevice_GetOverlayUpdateStatus();
HRESULT WINAPI D3DDevice_SetScissors(DWORD Count, BOOL Exclusive, CONST D3DRECT *pRects);
BOOL WINAPI D3DDevice_IsBusy();
void WINAPI D3DDevice_BlockUntilIdle();
void WINAPI D3DDevice_BlockUntilVerticalBlank();
void WINAPI D3DDevice_SetFlickerFilter(DWORD Filter);
void WINAPI D3DDevice_SetSoftDisplayFilter(BOOL Enable);
HRESULT WINAPI D3DDevice_PersistDisplay();
DWORD WINAPI D3DDevice_Swap(DWORD Flags);
void WINAPI D3DDevice_SetGammaRamp(DWORD Flags, CONST D3DGAMMARAMP *pRamp);

// Resources.
ULONG WINAPI D3DResource_AddRef(D3DResource *pThis);
ULONG WINAPI D3DResource_Release(D3DResource *pThis);
D3DRESOURCETYPE WINAPI D3DResource_GetType(D3DResource *pThis);
BOOL WINAPI D3DResource_IsBusy(D3DResource *pThis);
void WINAPI D3DResource_Register(D3DResource *pThis, void *pBase);
void WINAPI D3DResource_BlockUntilNotBusy(D3DResource *pThis);
DWORD WINAPI D3DBaseTexture_GetLevelCount(D3DBaseTexture *pThis);
HRESULT WINAPI D3DTexture_GetLevelDesc(D3DTexture *pThis, UINT Level, D3DSURFACE_DESC *pDesc);
HRESULT WINAPI D3DTexture_GetSurfaceLevel(D3DTexture *pThis, UINT Level, D3DSurface **ppSurfaceLevel);
D3DSurface *WINAPI D3DTexture_GetSurfaceLevel2(D3DTexture *pThis, UINT Level);
HRESULT WINAPI D3DTexture_LockRect(D3DTexture *pThis, UINT Level, D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect,
                                   DWORD Flags);
HRESULT WINAPI D3DTexture_UnlockRect(D3DTexture *pThis, UINT Level);
HRESULT WINAPI D3DSurface_GetDesc(D3DSurface *pThis, D3DSURFACE_DESC *pDesc);
HRESULT WINAPI D3DSurface_LockRect(D3DSurface *pThis, D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags);
HRESULT WINAPI D3DSurface_UnlockRect(D3DSurface *pThis);
HRESULT WINAPI D3DSurface_GetContainer(D3DSurface *pThis, D3DBaseTexture **ppBaseTexture);
HRESULT WINAPI D3DVertexBuffer_Lock(D3DVertexBuffer *pThis, UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData,
                                    DWORD Flags);
BYTE *WINAPI D3DVertexBuffer_Lock2(D3DVertexBuffer *pThis, DWORD Flags);
HRESULT WINAPI D3DVertexBuffer_Unlock(D3DVertexBuffer *pThis);
HRESULT WINAPI D3DIndexBuffer_Lock(D3DIndexBuffer *pThis, UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData,
                                   DWORD Flags);
HRESULT WINAPI D3DIndexBuffer_Unlock(D3DIndexBuffer *pThis);

// Memory the GPU can see (here: plain heap memory).
void *WINAPI D3D_AllocContiguousMemory(DWORD dwSize, DWORD dwAllocAttributes);
void WINAPI D3D_FreeContiguousMemory(void *pMemory);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

// The Xbox resource header: what an .xpr carries for each resource.
struct D3DResource
{
	DWORD Common;
	DWORD Data;
	DWORD Lock;

	ULONG WINAPI AddRef() { return D3DResource_AddRef(this); }
	ULONG WINAPI Release() { return D3DResource_Release(this); }
	D3DRESOURCETYPE WINAPI GetType() { return D3DResource_GetType(this); }
	BOOL WINAPI IsBusy() { return D3DResource_IsBusy(this); }
	void WINAPI BlockUntilNotBusy() { D3DResource_BlockUntilNotBusy(this); }
	void WINAPI Register(void *pBase) { D3DResource_Register(this, pBase); }
};

struct D3DPixelContainer : public D3DResource
{
	DWORD Format;   // D3DFORMAT_* fields: the format, dimensions and levels
	DWORD Size;     // linear textures: width, height and pitch
};

struct D3DBaseTexture : public D3DPixelContainer
{
	DWORD WINAPI GetLevelCount() { return D3DBaseTexture_GetLevelCount(this); }
};

struct D3DTexture : public D3DBaseTexture
{
	HRESULT WINAPI GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) { return D3DTexture_GetLevelDesc(this, Level, pDesc); }
	HRESULT WINAPI GetSurfaceLevel(UINT Level, D3DSurface **ppSurfaceLevel)
	{
		return D3DTexture_GetSurfaceLevel(this, Level, ppSurfaceLevel);
	}
	HRESULT WINAPI LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags)
	{
		return D3DTexture_LockRect(this, Level, pLockedRect, pRect, Flags);
	}
	HRESULT WINAPI UnlockRect(UINT Level) { return D3DTexture_UnlockRect(this, Level); }
};

struct D3DSurface : public D3DPixelContainer
{
	D3DBaseTexture *Parent;

	HRESULT WINAPI GetDesc(D3DSURFACE_DESC *pDesc) { return D3DSurface_GetDesc(this, pDesc); }
	HRESULT WINAPI LockRect(D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags)
	{
		return D3DSurface_LockRect(this, pLockedRect, pRect, Flags);
	}
	HRESULT WINAPI UnlockRect() { return D3DSurface_UnlockRect(this); }
	HRESULT WINAPI GetContainer(D3DBaseTexture **ppBaseTexture) { return D3DSurface_GetContainer(this, ppBaseTexture); }
};

struct D3DVertexBuffer : public D3DResource
{
	HRESULT WINAPI Lock(UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData, DWORD Flags)
	{
		return D3DVertexBuffer_Lock(this, OffsetToLock, SizeToLock, ppbData, Flags);
	}
	HRESULT WINAPI Unlock() { return D3DVertexBuffer_Unlock(this); }
};

struct D3DIndexBuffer : public D3DResource
{
	HRESULT WINAPI Lock(UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData, DWORD Flags)
	{
		return D3DIndexBuffer_Lock(this, OffsetToLock, SizeToLock, ppbData, Flags);
	}
	HRESULT WINAPI Unlock() { return D3DIndexBuffer_Unlock(this); }
};

struct D3DPalette : public D3DResource
{
};

struct Direct3D
{
	static ULONG WINAPI AddRef() { return 1; }
	static ULONG WINAPI Release() { return 1; }
	static HRESULT WINAPI CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, void *pUnused, DWORD BehaviorFlags,
	                                   D3DPRESENT_PARAMETERS *pPresentationParameters,
	                                   D3DDevice **ppReturnedDeviceInterface)
	{
		return Direct3D_CreateDevice(Adapter, DeviceType, pUnused, BehaviorFlags, pPresentationParameters,
		                             ppReturnedDeviceInterface);
	}
};

struct D3DDevice
{
	static ULONG WINAPI AddRef() { return 1; }
	static ULONG WINAPI Release() { return 1; }
	static HRESULT WINAPI GetDisplayMode(D3DDISPLAYMODE *pMode) { return D3DDevice_GetDisplayMode(pMode); }
	static HRESULT WINAPI GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters)
	{
		return D3DDevice_GetCreationParameters(pParameters);
	}
	static HRESULT WINAPI Reset(D3DPRESENT_PARAMETERS *pPresentationParameters)
	{
		return D3DDevice_Reset(pPresentationParameters);
	}
	static HRESULT WINAPI Present(CONST RECT *pSourceRect, CONST RECT *pDestRect, void *pUnused, void *pUnused2)
	{
		return D3DDevice_Present(pSourceRect, pDestRect, pUnused, pUnused2);
	}
	static HRESULT WINAPI GetBackBuffer(INT BackBuffer, D3DBACKBUFFER_TYPE, D3DSurface **ppBackBuffer)
	{
		*ppBackBuffer = D3DDevice_GetBackBuffer2(BackBuffer);
		return *ppBackBuffer != NULL ? S_OK : E_FAIL;
	}
	static HRESULT WINAPI CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format,
	                                    D3DPOOL Pool, D3DTexture **ppTexture)
	{
		return D3DDevice_CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture);
	}
	static HRESULT WINAPI CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
	                                         D3DVertexBuffer **ppVertexBuffer)
	{
		return D3DDevice_CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer);
	}
	static HRESULT WINAPI CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
	                                        D3DIndexBuffer **ppIndexBuffer)
	{
		return D3DDevice_CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer);
	}
	static HRESULT WINAPI CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DSurface **ppSurface)
	{
		return D3DDevice_CreateImageSurface(Width, Height, Format, ppSurface);
	}
	static HRESULT WINAPI CopyRects(D3DSurface *pSourceSurface, CONST RECT *pSourceRectsArray, UINT cRects,
	                                D3DSurface *pDestinationSurface, CONST POINT *pDestPointsArray)
	{
		return D3DDevice_CopyRects(pSourceSurface, pSourceRectsArray, cRects, pDestinationSurface, pDestPointsArray);
	}
	static HRESULT WINAPI SetRenderTarget(D3DSurface *pRenderTarget, D3DSurface *pNewZStencil)
	{
		return D3DDevice_SetRenderTarget(pRenderTarget, pNewZStencil);
	}
	static HRESULT WINAPI GetRenderTarget(D3DSurface **ppRenderTarget) { return D3DDevice_GetRenderTarget(ppRenderTarget); }
	static HRESULT WINAPI GetDepthStencilSurface(D3DSurface **ppZStencilSurface)
	{
		return D3DDevice_GetDepthStencilSurface(ppZStencilSurface);
	}
	static HRESULT WINAPI BeginScene() { return D3DDevice_BeginScene(); }
	static HRESULT WINAPI EndScene() { return D3DDevice_EndScene(); }
	static HRESULT WINAPI Clear(DWORD Count, CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil)
	{
		return D3DDevice_Clear(Count, pRects, Flags, Color, Z, Stencil);
	}
	static HRESULT WINAPI SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix)
	{
		return D3DDevice_SetTransform(State, pMatrix);
	}
	static HRESULT WINAPI GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix)
	{
		return D3DDevice_GetTransform(State, pMatrix);
	}
	static HRESULT WINAPI SetViewport(CONST D3DVIEWPORT8 *pViewport) { return D3DDevice_SetViewport(pViewport); }
	static HRESULT WINAPI GetViewport(D3DVIEWPORT8 *pViewport) { return D3DDevice_GetViewport(pViewport); }
	static HRESULT WINAPI SetMaterial(CONST D3DMATERIAL8 *pMaterial) { return D3DDevice_SetMaterial(pMaterial); }
	static HRESULT WINAPI SetLight(DWORD Index, CONST D3DLIGHT8 *pLight) { return D3DDevice_SetLight(Index, pLight); }
	static HRESULT WINAPI LightEnable(DWORD Index, BOOL Enable) { return D3DDevice_LightEnable(Index, Enable); }
	static HRESULT WINAPI SetRenderState(D3DRENDERSTATETYPE State, DWORD Value)
	{
		return D3DDevice_SetRenderState(State, Value);
	}
	static HRESULT WINAPI GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue)
	{
		return D3DDevice_GetRenderState(State, pValue);
	}
	static HRESULT WINAPI SetTexture(DWORD Stage, D3DBaseTexture *pTexture) { return D3DDevice_SetTexture(Stage, pTexture); }
	static HRESULT WINAPI GetTexture(DWORD Stage, D3DBaseTexture **ppTexture) { return D3DDevice_GetTexture(Stage, ppTexture); }
	static HRESULT WINAPI SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
	{
		return D3DDevice_SetTextureStageState(Stage, Type, Value);
	}
	static HRESULT WINAPI GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue)
	{
		return D3DDevice_GetTextureStageState(Stage, Type, pValue);
	}
	static HRESULT WINAPI DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
	{
		return D3DDevice_DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
	}
	static HRESULT WINAPI DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT MinIndex, UINT NumIndices,
	                                           UINT StartIndex, UINT PrimitiveCount)
	{
		return D3DDevice_DrawIndexedPrimitive(PrimitiveType, MinIndex, NumIndices, StartIndex, PrimitiveCount);
	}
	static HRESULT WINAPI DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
	                                      CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
	{
		return D3DDevice_DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
	}
	static HRESULT WINAPI DrawVertices(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT VertexCount)
	{
		return D3DDevice_DrawVertices(PrimitiveType, StartVertex, VertexCount);
	}
	static HRESULT WINAPI DrawIndexedVertices(D3DPRIMITIVETYPE PrimitiveType, UINT VertexCount, CONST WORD *pIndexData)
	{
		return D3DDevice_DrawIndexedVertices(PrimitiveType, VertexCount, pIndexData);
	}
	static HRESULT WINAPI DrawVerticesUP(D3DPRIMITIVETYPE PrimitiveType, UINT VertexCount,
	                                     CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
	{
		return D3DDevice_DrawVerticesUP(PrimitiveType, VertexCount, pVertexStreamZeroData, VertexStreamZeroStride);
	}
	static HRESULT WINAPI CreateVertexShader(CONST DWORD *pDeclaration, CONST DWORD *pFunction, DWORD *pHandle,
	                                         DWORD Usage)
	{
		return D3DDevice_CreateVertexShader(pDeclaration, pFunction, pHandle, Usage);
	}
	static HRESULT WINAPI SetVertexShader(DWORD Handle) { return D3DDevice_SetVertexShader(Handle); }
	static HRESULT WINAPI DeleteVertexShader(DWORD Handle) { return D3DDevice_DeleteVertexShader(Handle); }
	static HRESULT WINAPI SetVertexShaderConstant(INT Register, CONST void *pConstantData, DWORD ConstantCount)
	{
		return D3DDevice_SetVertexShaderConstant(Register, pConstantData, ConstantCount);
	}
	static HRESULT WINAPI SetStreamSource(UINT StreamNumber, D3DVertexBuffer *pStreamData, UINT Stride)
	{
		return D3DDevice_SetStreamSource(StreamNumber, pStreamData, Stride);
	}
	static HRESULT WINAPI SetIndices(D3DIndexBuffer *pIndexData, UINT BaseVertexIndex)
	{
		return D3DDevice_SetIndices(pIndexData, BaseVertexIndex);
	}
	static HRESULT WINAPI CreatePixelShader(CONST D3DPIXELSHADERDEF *pPSDef, DWORD *pHandle)
	{
		return D3DDevice_CreatePixelShader(pPSDef, pHandle);
	}
	static HRESULT WINAPI SetPixelShader(DWORD Handle) { return D3DDevice_SetPixelShader(Handle); }
	static HRESULT WINAPI SetVertexData2f(INT Register, float a, float b) { return D3DDevice_SetVertexData2f(Register, a, b); }
	static HRESULT WINAPI SetVertexData4f(INT Register, float a, float b, float c, float d)
	{
		return D3DDevice_SetVertexData4f(Register, a, b, c, d);
	}
	static HRESULT WINAPI SetVertexDataColor(INT Register, D3DCOLOR Color) { return D3DDevice_SetVertexDataColor(Register, Color); }
	static HRESULT WINAPI Begin(D3DPRIMITIVETYPE PrimitiveType) { return D3DDevice_Begin(PrimitiveType); }
	static HRESULT WINAPI End() { return D3DDevice_End(); }
	static HRESULT WINAPI UpdateOverlay(D3DSurface *pSurface, CONST RECT *SrcRect, CONST RECT *DstRect,
	                                   BOOL EnableColorKey, D3DCOLOR ColorKey)
	{
		return D3DDevice_UpdateOverlay(pSurface, SrcRect, DstRect, EnableColorKey, ColorKey);
	}
	static HRESULT WINAPI EnableOverlay(BOOL Enable) { return D3DDevice_EnableOverlay(Enable); }
	static BOOL WINAPI GetOverlayUpdateStatus() { return D3DDevice_GetOverlayUpdateStatus(); }
	static HRESULT WINAPI SetScissors(DWORD Count, BOOL Exclusive, CONST D3DRECT *pRects)
	{
		return D3DDevice_SetScissors(Count, Exclusive, pRects);
	}
	static BOOL WINAPI IsBusy() { return D3DDevice_IsBusy(); }
	static void WINAPI BlockUntilIdle() { D3DDevice_BlockUntilIdle(); }
	static void WINAPI BlockUntilVerticalBlank() { D3DDevice_BlockUntilVerticalBlank(); }
	static void WINAPI SetFlickerFilter(DWORD Filter) { D3DDevice_SetFlickerFilter(Filter); }
	static void WINAPI SetSoftDisplayFilter(BOOL Enable) { D3DDevice_SetSoftDisplayFilter(Enable); }
	static HRESULT WINAPI PersistDisplay() { return D3DDevice_PersistDisplay(); }
	static DWORD WINAPI Swap(DWORD Flags) { return D3DDevice_Swap(Flags); }
	static void WINAPI SetGammaRamp(DWORD Flags, CONST D3DGAMMARAMP *pRamp) { D3DDevice_SetGammaRamp(Flags, pRamp); }
};

#endif // __cplusplus
