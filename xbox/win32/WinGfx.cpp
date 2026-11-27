// The window and Direct3D 9 for the Windows build (see UxWin.h). The Xbox's
// screen (640x480, or 720x480/1280x720 in the HD modes) is the backbuffer,
// shown scaled to the window in its own shape (4:3, or 16:9 in a widescreen
// mode) with black bars; Alt+Enter toggles a borderless full screen.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include "UxWin.h"

static HWND s_hwnd;
static IDirect3D9 *s_d3d;
static IDirect3DDevice9 *s_device;
static D3DPRESENT_PARAMETERS s_pp;
static CRITICAL_SECTION s_lock;
static bool s_lockReady;
static bool s_inScene;
static bool s_closed;
static bool s_fullscreen;
static RECT s_windowedRect;
// The window's own swap chain, the client area's size: each frame is scaled
// into it (smoothly, keeping its shape). The device's backbuffer stays the
// Xbox's size and is never presented itself.
static IDirect3DSwapChain9 *s_windowChain;
static UINT s_windowWidth, s_windowHeight;
static bool s_widescreen;
// The dash draws at the window's own size (it was resized to it): shown 1:1,
// in the backbuffer's own shape.
static bool s_ownShape;
// A drag-resize is under way (the window's sizing loop): the new size is
// handed over when it ends.
static bool s_sizing;
static LARGE_INTEGER s_lastVBlank;
// Frame pacing (see PaceFrame): the window's monitor's refresh rate (0: look
// it up again), the next frame's time, and the timer slept on until then.
static UINT s_refresh;
static LONGLONG s_nextFrame;
static HANDLE s_frameTimer;

// (Each file has its own: in an unnamed namespace, so they stay apart.)
namespace
{
struct Locker
{
	Locker() { EnterCriticalSection(&s_lock); }
	~Locker() { LeaveCriticalSection(&s_lock); }
};
}

static void ToggleFullscreen()
{
	s_fullscreen = !s_fullscreen;
	if (s_fullscreen)
	{
		GetWindowRect(s_hwnd, &s_windowedRect);
		MONITORINFO info = {sizeof(info)};
		GetMonitorInfoA(MonitorFromWindow(s_hwnd, MONITOR_DEFAULTTONEAREST), &info);
		SetWindowLongA(s_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
		SetWindowPos(s_hwnd, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
		             info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top,
		             SWP_FRAMECHANGED);
	}
	else
	{
		SetWindowLongA(s_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
		SetWindowPos(s_hwnd, NULL, s_windowedRect.left, s_windowedRect.top, s_windowedRect.right - s_windowedRect.left,
		             s_windowedRect.bottom - s_windowedRect.top, SWP_FRAMECHANGED | SWP_NOZORDER);
	}
}

static void RepaintScaled();

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CLOSE:
		s_closed = true;
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_SYSKEYDOWN:
		if (wParam == VK_RETURN && (lParam & (1 << 29)))
		{
			ToggleFullscreen();
			return 0;
		}
		break;
	case WM_SYSCHAR:
		if (wParam == VK_RETURN)
			return 0;   // (no beep for Alt+Enter)
		break;
	case WM_ENTERSIZEMOVE:
		s_sizing = true;
		break;
	case WM_EXITSIZEMOVE:
		s_sizing = false;
		s_refresh = 0;   // (perhaps on another monitor now)
		break;
	case WM_DISPLAYCHANGE:
		s_refresh = 0;
		break;
	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED)
			RepaintScaled();
		break;
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT)
		{
			SetCursor(NULL);   // the dash draws its own pointer
			return TRUE;
		}
		break;
	}
	UxInputOnMessage(hwnd, message, wParam, lParam);
	return DefWindowProcA(hwnd, message, wParam, lParam);
}

int UxPumpMessages(void)
{
	MSG msg;
	while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
			s_closed = true;
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}
	if (s_closed)
	{
		// The Xbox code never returns from its loop: end here.
		UxLog("The window was closed");
		ExitProcess(0);
	}
	return 1;
}

static void FillPresentParameters(int width, int height, int vsync)
{
	ZeroMemory(&s_pp, sizeof(s_pp));
	s_pp.BackBufferWidth = width;
	s_pp.BackBufferHeight = height;
	s_pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	s_pp.BackBufferCount = 1;
	s_pp.SwapEffect = D3DSWAPEFFECT_COPY;   // (stretched to the window, kept for reading back)
	s_pp.hDeviceWindow = s_hwnd;
	s_pp.Windowed = TRUE;
	s_pp.EnableAutoDepthStencil = TRUE;
	s_pp.AutoDepthStencilFormat = D3DFMT_D24S8;
	s_pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
	s_pp.PresentationInterval = vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
}

int UxGfxInit(int width, int height, int vsync)
{
	if (!s_lockReady)
	{
		InitializeCriticalSection(&s_lock);
		s_lockReady = true;
	}
	if (s_device != NULL)
		return UxGfxReset(width, height, vsync);

	HINSTANCE instance = GetModuleHandleA(NULL);
	WNDCLASSA wc = {0};
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = instance;
	wc.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(1));
	wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName = "UnleashX";
	RegisterClassA(&wc);

	// 1.5 times the Xbox's picture, or what fits.
	RECT rect = {0, 0, width * 3 / 2, height * 3 / 2};
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);
	if (rect.bottom > screenHeight - 100)
	{
		rect.right = width;
		rect.bottom = height;
	}
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	s_hwnd = CreateWindowA("UnleashX", "UnleashX", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
	                       rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, instance, NULL);
	if (s_hwnd == NULL)
		return 0;
	ShowWindow(s_hwnd, SW_SHOW);
	UpdateWindow(s_hwnd);

	s_d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (s_d3d == NULL)
	{
		MessageBoxA(s_hwnd, "Direct3D 9 isn't available.", "UnleashX", MB_ICONERROR);
		return 0;
	}
	FillPresentParameters(width, height, vsync);
	HRESULT hr = s_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, s_hwnd,
	                                 D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED |
	                                     D3DCREATE_FPU_PRESERVE,
	                                 &s_pp, &s_device);
	if (FAILED(hr))
		hr = s_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, s_hwnd,
		                         D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
		                         &s_pp, &s_device);
	if (FAILED(hr))
	{
		MessageBoxA(s_hwnd, "Couldn't create the Direct3D 9 device.", "UnleashX", MB_ICONERROR);
		return 0;
	}
	s_device->BeginScene();
	s_inScene = true;
	QueryPerformanceCounter(&s_lastVBlank);
	return 1;
}

static void ReleaseWindowChain()
{
	if (s_windowChain != NULL)
	{
		s_windowChain->Release();
		s_windowChain = NULL;
	}
}

// The window's swap chain, made again when the client area changes size.
static bool WindowChain()
{
	RECT client;
	GetClientRect(s_hwnd, &client);
	UINT width = (UINT)(client.right - client.left);
	UINT height = (UINT)(client.bottom - client.top);
	if (width == 0 || height == 0)
		return false;
	if (s_windowChain != NULL && width == s_windowWidth && height == s_windowHeight)
		return true;
	ReleaseWindowChain();
	D3DPRESENT_PARAMETERS pp = s_pp;
	pp.BackBufferWidth = width;
	pp.BackBufferHeight = height;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.EnableAutoDepthStencil = FALSE;
	pp.Flags = 0;
	if (FAILED(s_device->CreateAdditionalSwapChain(&pp, &s_windowChain)))
	{
		s_windowChain = NULL;
		return false;
	}
	s_windowWidth = width;
	s_windowHeight = height;
	return true;
}

// The frame into the window's swap chain: black, then the picture as large
// as fits in its shape, centered.
static HRESULT PresentScaled()
{
	IDirect3DSurface9 *source = NULL, *target = NULL;
	HRESULT hr = s_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &source);
	if (SUCCEEDED(hr))
		hr = s_windowChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &target);
	if (SUCCEEDED(hr))
	{
		float aspect = s_ownShape ? (float)s_pp.BackBufferWidth / (float)s_pp.BackBufferHeight
		                          : (s_widescreen ? 16.0f / 9.0f : 4.0f / 3.0f);
		float width = (float)s_windowWidth, height = (float)s_windowHeight;
		if (width > height * aspect)
			width = height * aspect;
		else
			height = width / aspect;
		RECT rect;
		rect.left = (LONG)(((float)s_windowWidth - width) * 0.5f);
		rect.top = (LONG)(((float)s_windowHeight - height) * 0.5f);
		rect.right = rect.left + (LONG)(width + 0.5f);
		rect.bottom = rect.top + (LONG)(height + 0.5f);
		s_device->ColorFill(target, NULL, D3DCOLOR_XRGB(0, 0, 0));
		hr = s_device->StretchRect(source, NULL, target, &rect, D3DTEXF_LINEAR);
	}
	if (target != NULL)
		target->Release();
	if (source != NULL)
		source->Release();
	if (SUCCEEDED(hr))
		hr = s_windowChain->Present(NULL, NULL, NULL, NULL, 0);
	return hr;
}

void UxGfxSetWidescreen(int widescreen)
{
	s_widescreen = widescreen != 0;
}

// The window's new client size once it differs from the backbuffer's (and
// isn't being dragged, or minimized): the dash then draws at that size.
extern "C" int UxGfxTakeResize(unsigned *width, unsigned *height)
{
	if (s_hwnd == NULL || s_device == NULL || s_sizing || IsIconic(s_hwnd))
		return 0;
	RECT client;
	GetClientRect(s_hwnd, &client);
	unsigned w = (unsigned)(client.right - client.left);
	unsigned h = (unsigned)(client.bottom - client.top);
	if (w < 160 || h < 120)
		return 0;
	if (w == s_pp.BackBufferWidth && h == s_pp.BackBufferHeight)
		return 0;
	*width = w;
	*height = h;
	s_ownShape = true;
	return 1;
}

// The last frame again, scaled to the window (while it's dragged to a new
// size, before the dash draws at it).
static void RepaintScaled()
{
	Locker lock;
	if (s_device == NULL)
		return;
	bool inScene = s_inScene;
	if (inScene)
		s_device->EndScene();
	if (WindowChain())
		PresentScaled();
	if (inScene)
		s_device->BeginScene();
}

int UxGfxReset(int width, int height, int vsync)
{
	Locker lock;
	if (s_device == NULL)
		return 0;
	ReleaseWindowChain();
	if (s_inScene)
	{
		s_device->EndScene();
		s_inScene = false;
	}
	FillPresentParameters(width, height, vsync);
	HRESULT hr = s_device->Reset(&s_pp);
	s_device->BeginScene();
	s_inScene = true;
	return SUCCEEDED(hr);
}

static LONGLONG TicksPerSecond();
static void PaceFrame();

// With UNLEASHX_LOG_FPS set, the frame rate in the log every 5 seconds.
static void LogFrameRate(LONGLONG now)
{
	static int s_enabled = -1;
	static LONGLONG s_since;
	static DWORD s_frames;
	if (s_enabled < 0)
		s_enabled = GetEnvironmentVariableA("UNLEASHX_LOG_FPS", NULL, 0) != 0;
	if (!s_enabled)
		return;
	if (s_since == 0)
		s_since = now;
	s_frames++;
	if (now - s_since >= TicksPerSecond() * 5)
	{
		UxLog("%.1f frames a second", (double)s_frames * TicksPerSecond() / (double)(now - s_since));
		s_since = now;
		s_frames = 0;
	}
}

void UxGfxPresent(void)
{
	{
		Locker lock;
		if (s_device == NULL)
			return;
		if (s_inScene)
			s_device->EndScene();
		s_inScene = false;
		HRESULT hr = WindowChain() ? PresentScaled() : D3DERR_INVALIDCALL;
		if (hr == D3DERR_INVALIDCALL)
			hr = s_device->Present(NULL, NULL, NULL, NULL);   // (stretched by Windows)
		if (hr == D3DERR_DEVICELOST)
		{
			// (Back after the lock screen, a mode change...)
			if (s_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
			{
				ReleaseWindowChain();
				s_device->Reset(&s_pp);
			}
		}
		s_device->BeginScene();
		s_inScene = true;
	}
	// (No 60 fps cap: the frames follow the monitor's refresh, 144 Hz on a 144
	// Hz monitor. The animations go by time.)
	PaceFrame();
	UxPumpMessages();
	QueryPerformanceCounter(&s_lastVBlank);
	LogFrameRate(s_lastVBlank.QuadPart);
}

// The refresh rate of the monitor the window is on, for the frame pacing and
// the vertical blank waits.
static UINT RefreshRate()
{
	if (s_refresh == 0)
	{
		s_refresh = 60;
		MONITORINFOEXA info;
		info.cbSize = sizeof(info);
		DEVMODEA mode;
		mode.dmSize = sizeof(mode);
		mode.dmDriverExtra = 0;
		if (s_hwnd != NULL && GetMonitorInfoA(MonitorFromWindow(s_hwnd, MONITOR_DEFAULTTONEAREST), &info) &&
		    EnumDisplaySettingsA(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency >= 24)
			s_refresh = mode.dmDisplayFrequency;
	}
	return s_refresh;
}

static LONGLONG Ticks()
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return now.QuadPart;
}

static LONGLONG TicksPerSecond()
{
	static LARGE_INTEGER s_frequency;
	if (s_frequency.QuadPart == 0)
		QueryPerformanceFrequency(&s_frequency);
	return s_frequency.QuadPart;
}

// Sleeps until the performance counter reaches deadline: on a high
// resolution timer (Windows 10 1803 on, about 0.5 ms late at most), else
// Sleep (1 ms ticks, timeBeginPeriod).
static void SleepUntil(LONGLONG deadline)
{
	if (s_frameTimer == NULL)
	{
		s_frameTimer = CreateWaitableTimerExW(NULL, NULL, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
		                                      TIMER_ALL_ACCESS);
		if (s_frameTimer == NULL)
			s_frameTimer = INVALID_HANDLE_VALUE;
	}
	LONGLONG remaining = deadline - Ticks();
	if (remaining <= 0)
		return;
	if (s_frameTimer != INVALID_HANDLE_VALUE)
	{
		LARGE_INTEGER due;
		due.QuadPart = -(remaining * 10000000 / TicksPerSecond());   // (relative, in 100 ns)
		if (due.QuadPart < 0 && SetWaitableTimer(s_frameTimer, &due, 0, NULL, NULL, FALSE))
		{
			WaitForSingleObject(s_frameTimer, INFINITE);
			return;
		}
	}
	Sleep((DWORD)(remaining * 1000 / TicksPerSecond()));
}

// (Not in the original, whose frames the Xbox's GPU paced.) A frame at most
// every refresh of the monitor. Without vsync ("Faster Refresh Rate", the
// dash's default) Direct3D would draw hundreds of frames a second that
// Windows' compositor throws away, using a core or two (the driver's thread
// as much as the dash's) for nothing; as fast as the screen shows them, a
// frame still goes out as soon as it's drawn. Minimized, 10 a second (the
// music, the FTP server and the rest still run).
static void PaceFrame()
{
	bool minimized = s_hwnd != NULL && IsIconic(s_hwnd);
	if (!minimized && s_pp.PresentationInterval == D3DPRESENT_INTERVAL_ONE)
	{
		s_nextFrame = 0;   // (Present waited for the vertical blank)
		return;
	}
	LONGLONG period = TicksPerSecond() / (minimized ? 10 : RefreshRate());
	LONGLONG now = Ticks();
	// After a slow frame (or the first), from now: no rush of frames to
	// catch up.
	if (s_nextFrame == 0 || now - s_nextFrame > period)
		s_nextFrame = now;
	s_nextFrame += period;
	SleepUntil(s_nextFrame);
}

// A millisecond clock for the dash's GetTickCount: Windows' own only moves
// every 15.6 ms, which made its timed fades and scrolling step unevenly.
// (It starts where GetTickCount was, so it wraps the same way.)
extern "C" DWORD WINAPI UxGetTickCount(void)
{
	static LARGE_INTEGER s_start, s_frequency;
	static DWORD s_base;
	if (s_frequency.QuadPart == 0)
	{
		QueryPerformanceFrequency(&s_frequency);
		QueryPerformanceCounter(&s_start);
		s_base = GetTickCount();
	}
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return s_base + (DWORD)((now.QuadPart - s_start.QuadPart) * 1000 / s_frequency.QuadPart);
}

void UxGfxWaitVBlank(void)
{
	// About one of the monitor's refreshes since the last one.
	LONGLONG now = Ticks();
	LONGLONG next = s_lastVBlank.QuadPart + TicksPerSecond() / RefreshRate();
	if (now < next)
	{
		SleepUntil(next);
		s_lastVBlank.QuadPart = next;
	}
	else
	{
		s_lastVBlank.QuadPart = now;
	}
	UxPumpMessages();
}

void UxGfxSetRenderState(ux_u32 state, ux_u32 value)
{
	Locker lock;
	if (s_device != NULL)
		s_device->SetRenderState((D3DRENDERSTATETYPE)state, value);
}

void UxGfxSetTextureStageState(ux_u32 stage, ux_u32 type, ux_u32 value)
{
	Locker lock;
	if (s_device != NULL)
		s_device->SetTextureStageState(stage, (D3DTEXTURESTAGESTATETYPE)type, value);
}

void UxGfxSetSamplerState(ux_u32 stage, ux_u32 type, ux_u32 value)
{
	Locker lock;
	if (s_device != NULL)
		s_device->SetSamplerState(stage, (D3DSAMPLERSTATETYPE)type, value);
}

void UxGfxSetTransform(ux_u32 type, const float *matrix)
{
	Locker lock;
	if (s_device != NULL)
		s_device->SetTransform((D3DTRANSFORMSTATETYPE)type, (const D3DMATRIX *)matrix);
}

void UxGfxSetViewport(ux_u32 x, ux_u32 y, ux_u32 width, ux_u32 height, float minZ, float maxZ)
{
	Locker lock;
	if (s_device == NULL)
		return;
	D3DVIEWPORT9 viewport = {x, y, width, height, minZ, maxZ};
	// Within the backbuffer, or Direct3D 9 refuses it.
	if (viewport.X >= s_pp.BackBufferWidth || viewport.Y >= s_pp.BackBufferHeight)
		return;
	if (viewport.X + viewport.Width > s_pp.BackBufferWidth)
		viewport.Width = s_pp.BackBufferWidth - viewport.X;
	if (viewport.Y + viewport.Height > s_pp.BackBufferHeight)
		viewport.Height = s_pp.BackBufferHeight - viewport.Y;
	s_device->SetViewport(&viewport);
}

void UxGfxSetMaterial(const void *material)
{
	Locker lock;
	if (s_device != NULL)
		s_device->SetMaterial((const D3DMATERIAL9 *)material);
}

void UxGfxSetLight(ux_u32 index, const void *light)
{
	Locker lock;
	if (s_device != NULL)
		s_device->SetLight(index, (const D3DLIGHT9 *)light);
}

void UxGfxLightEnable(ux_u32 index, int enable)
{
	Locker lock;
	if (s_device != NULL)
		s_device->LightEnable(index, enable);
}

void UxGfxClear(ux_u32 count, const long *rects, ux_u32 flags, ux_u32 color, float z, ux_u32 stencil)
{
	Locker lock;
	if (s_device != NULL)
		s_device->Clear(count, (const D3DRECT *)rects, flags, color, z, stencil);
}

void UxGfxSetScissor(int enable, long left, long top, long right, long bottom)
{
	Locker lock;
	if (s_device == NULL)
		return;
	if (enable)
	{
		RECT rect = {left, top, right, bottom};
		s_device->SetScissorRect(&rect);
	}
	s_device->SetRenderState(D3DRS_SCISSORTESTENABLE, enable ? TRUE : FALSE);
}

void *UxGfxCreateTexture(ux_u32 width, ux_u32 height, ux_u32 levels, int format)
{
	Locker lock;
	if (s_device == NULL)
		return NULL;
	D3DFORMAT d3dFormat = D3DFMT_A8R8G8B8;
	if (format == UX_TEX_DXT1)
		d3dFormat = D3DFMT_DXT1;
	else if (format == UX_TEX_DXT3)
		d3dFormat = D3DFMT_DXT3;
	else if (format == UX_TEX_DXT5)
		d3dFormat = D3DFMT_DXT5;
	IDirect3DTexture9 *texture = NULL;
	if (FAILED(s_device->CreateTexture(width, height, levels, 0, d3dFormat, D3DPOOL_MANAGED, &texture, NULL)))
		return NULL;
	return texture;
}

void UxGfxUploadTexture(void *texture, ux_u32 level, const void *data, ux_u32 pitch)
{
	Locker lock;
	IDirect3DTexture9 *tex = (IDirect3DTexture9 *)texture;
	if (tex == NULL)
		return;
	D3DSURFACE_DESC desc;
	if (FAILED(tex->GetLevelDesc(level, &desc)))
		return;
	D3DLOCKED_RECT locked;
	if (FAILED(tex->LockRect(level, &locked, NULL, 0)))
		return;
	bool compressed = desc.Format == D3DFMT_DXT1 || desc.Format == D3DFMT_DXT3 || desc.Format == D3DFMT_DXT5;
	UINT rows = compressed ? (desc.Height + 3) / 4 : desc.Height;
	UINT rowBytes = compressed ? ((desc.Width + 3) / 4) * (desc.Format == D3DFMT_DXT1 ? 8 : 16) : desc.Width * 4;
	for (UINT y = 0; y < rows; y++)
		memcpy((BYTE *)locked.pBits + y * locked.Pitch, (const BYTE *)data + y * pitch, rowBytes);
	tex->UnlockRect(level);
}

void UxGfxReleaseTexture(void *texture)
{
	Locker lock;
	if (texture != NULL)
		((IDirect3DTexture9 *)texture)->Release();
}

void UxGfxSetTexture(ux_u32 stage, void *texture)
{
	Locker lock;
	if (s_device != NULL)
		s_device->SetTexture(stage, (IDirect3DBaseTexture9 *)texture);
}

void UxGfxDraw(ux_u32 primitive, ux_u32 count, const void *vertices, ux_u32 stride, ux_u32 fvf)
{
	Locker lock;
	if (s_device == NULL || count == 0)
		return;
	s_device->SetVertexShader(NULL);
	s_device->SetFVF(fvf);
	s_device->DrawPrimitiveUP((D3DPRIMITIVETYPE)primitive, count, vertices, stride);
}

void UxGfxDrawIndexed(ux_u32 primitive, ux_u32 vertexCount, ux_u32 count, const unsigned short *indices,
                      const void *vertices, ux_u32 stride, ux_u32 fvf)
{
	Locker lock;
	if (s_device == NULL || count == 0)
		return;
	s_device->SetVertexShader(NULL);
	s_device->SetFVF(fvf);
	s_device->DrawIndexedPrimitiveUP((D3DPRIMITIVETYPE)primitive, 0, vertexCount, count, indices, D3DFMT_INDEX16,
	                                 vertices, stride);
}

int UxGfxReadBackbuffer(void *pixels, ux_u32 width, ux_u32 height, ux_u32 pitch)
{
	Locker lock;
	if (s_device == NULL)
		return 0;
	IDirect3DSurface9 *back = NULL;
	if (FAILED(s_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
		return 0;
	int ok = 0;
	D3DLOCKED_RECT locked;
	if (SUCCEEDED(back->LockRect(&locked, NULL, D3DLOCK_READONLY)))
	{
		if (width > s_pp.BackBufferWidth)
			width = s_pp.BackBufferWidth;
		if (height > s_pp.BackBufferHeight)
			height = s_pp.BackBufferHeight;
		for (UINT y = 0; y < height; y++)
		{
			const DWORD *src = (const DWORD *)((const BYTE *)locked.pBits + y * locked.Pitch);
			DWORD *dst = (DWORD *)((BYTE *)pixels + y * pitch);
			for (UINT x = 0; x < width; x++)
				dst[x] = src[x] | 0xFF000000;
		}
		back->UnlockRect();
		ok = 1;
	}
	back->Release();
	return ok;
}

void UxGfxBackbufferSize(ux_u32 *width, ux_u32 *height)
{
	*width = s_pp.BackBufferWidth;
	*height = s_pp.BackBufferHeight;
}
