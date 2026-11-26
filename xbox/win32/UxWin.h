// Between the two halves of xbox/win32: the X*.cpp files implement the Xbox
// APIs with the Xbox headers, the Win*.cpp files talk to Windows (Direct3D 9,
// XInput, the window) with the real ones. The two sets of
// headers can't meet in one file (both define D3DFORMAT, IDirectSound...),
// so they talk through these plain C functions. Direct3D numbers here are
// Direct3D 9's.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int ux_u32;

// ---- The window and Direct3D 9 (WinGfx.cpp) ----

// Creates the window and the device: a backbuffer of width x height, shown
// scaled in the window. vsync: wait for the vertical blank on present.
int UxGfxInit(int width, int height, int vsync);
int UxGfxReset(int width, int height, int vsync);
// The picture's shape: 16:9 when widescreen, else 4:3. It's shown that shape
// in the window, with black bars.
void UxGfxSetWidescreen(int widescreen);
void UxGfxPresent(void);
// Waits for the next vertical blank (or ~16 ms).
void UxGfxWaitVBlank(void);
void UxGfxSetRenderState(ux_u32 state, ux_u32 value);
void UxGfxSetTextureStageState(ux_u32 stage, ux_u32 type, ux_u32 value);
void UxGfxSetSamplerState(ux_u32 stage, ux_u32 type, ux_u32 value);
void UxGfxSetTransform(ux_u32 type, const float *matrix);
void UxGfxSetViewport(ux_u32 x, ux_u32 y, ux_u32 width, ux_u32 height, float minZ, float maxZ);
void UxGfxSetMaterial(const void *material);     // D3DMATERIAL9 layout (= D3DMATERIAL8)
void UxGfxSetLight(ux_u32 index, const void *light);   // D3DLIGHT9 layout (= D3DLIGHT8)
void UxGfxLightEnable(ux_u32 index, int enable);
void UxGfxClear(ux_u32 count, const long *rects, ux_u32 flags, ux_u32 color, float z, ux_u32 stencil);
void UxGfxSetScissor(int enable, long left, long top, long right, long bottom);

// Textures: A8R8G8B8, or DXT1/3/5 kept compressed.
enum { UX_TEX_ARGB = 0, UX_TEX_DXT1 = 1, UX_TEX_DXT3 = 2, UX_TEX_DXT5 = 3 };
void *UxGfxCreateTexture(ux_u32 width, ux_u32 height, ux_u32 levels, int format);
void UxGfxUploadTexture(void *texture, ux_u32 level, const void *data, ux_u32 pitch);
void UxGfxReleaseTexture(void *texture);
void UxGfxSetTexture(ux_u32 stage, void *texture);

// Draws from memory: Direct3D 9 primitive type and count, the FVF.
void UxGfxDraw(ux_u32 primitive, ux_u32 count, const void *vertices, ux_u32 stride, ux_u32 fvf);
void UxGfxDrawIndexed(ux_u32 primitive, ux_u32 vertexCount, ux_u32 count, const unsigned short *indices,
                      const void *vertices, ux_u32 stride, ux_u32 fvf);

// The backbuffer as A8R8G8B8 rows (pitch in bytes); 0 on failure.
int UxGfxReadBackbuffer(void *pixels, ux_u32 width, ux_u32 height, ux_u32 pitch);
void UxGfxBackbufferSize(ux_u32 *width, ux_u32 *height);

// Messages: processes the window's queue (call it often). Returns 0 once the
// window is closed.
int UxPumpMessages(void);

// ---- Input (WinInput.cpp) ----

// One Xbox controller's state from XInput (Windows) and, for the first, the
// keyboard's function keys: buttons as the Xbox's wButtons, analog buttons
// A B X Y black white LT RT (0..255), sticks. Returns 0 if nothing is
// connected there (port 0, with the keyboard, always is).
typedef struct
{
	unsigned short buttons;
	unsigned char analog[8];
	short thumbLX, thumbLY, thumbRX, thumbRY;
} UxPad;
int UxInputGetPad(int port, UxPad *pad);
// The next key pressed (or repeated), for the debug keyboard: its virtual
// key, its character (printable ones and backspace; 0 otherwise) and the
// XINPUT_DEBUG_KEYSTROKE flags (1 ctrl, 2 shift, 4 alt, 8 caps lock). 0 when
// there's none.
typedef struct
{
	unsigned char virtualKey;
	char ascii;
	unsigned char flags;
} UxKey;
int UxInputGetKey(UxKey *key);
// Called by the window procedure (WinGfx.cpp).
void UxInputOnMessage(void *hwnd, ux_u32 message, size_t wParam, ptrdiff_t lParam);

// ---- Images (WinImage.cpp): PNG, JPEG, BMP, GIF, TIFF through WIC ----

// Decodes to A8R8G8B8 rows (*argb from malloc; free it). 0 on failure.
int UxDecodeImage(const void *data, size_t size, ux_u32 *width, ux_u32 *height, ux_u32 **argb);
// Writes A8R8G8B8 rows as a .bmp file (PC path). 0 on failure.
int UxWriteBmp(const char *path, const ux_u32 *argb, ux_u32 width, ux_u32 height);

// ---- The PC side of the Xbox's drives (XFiles.cpp) ----

// The folder that holds the Xbox's drives (C\, E\, F\, ...): next to the
// program, or UNLEASHX_XBOX_ROOT.
const char *UxXboxRoot(void);
// A line in UnleashX.log, next to the program (and the debugger's output).
void UxLog(const char *format, ...);

#ifdef __cplusplus
}
#endif
