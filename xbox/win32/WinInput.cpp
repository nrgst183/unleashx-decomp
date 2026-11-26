// Input for the Windows build: controllers through XInput (loaded at run
// time, so the program starts without it), and the keyboard as the Xbox's
// debug keyboard (the dash's own keys: Enter, Esc, the arrows, typing),
// plus keys for the first controller's buttons:
//
//   F1 A, F2 B, F3 X, F4 Y, F5 white, F6 black, Page Up / Page Down the
//   triggers, Tab back, F9 start.
#include <windows.h>
#include <xinput.h>

#include <string.h>

#include "UxWin.h"

typedef DWORD(WINAPI *XInputGetStateProc)(DWORD, XINPUT_STATE *);

static XInputGetStateProc s_getState;
static bool s_loaded;
static DWORD s_retryTick[4];
static bool s_missing[4];

static CRITICAL_SECTION s_lock;
static bool s_lockReady;
static bool s_keys[256];
static UxKey s_queue[64];
static int s_head, s_tail;

static void Lock()
{
	static LONG s_ready;
	if (InterlockedCompareExchange(&s_ready, 1, 0) == 0)
	{
		InitializeCriticalSection(&s_lock);
		s_lockReady = true;
		InterlockedExchange(&s_ready, 2);
	}
	while (s_ready != 2)
		Sleep(0);
	EnterCriticalSection(&s_lock);
}

static void Unlock()
{
	LeaveCriticalSection(&s_lock);
}

static void LoadXInput()
{
	if (s_loaded)
		return;
	s_loaded = true;
	const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
	for (size_t i = 0; i < sizeof(dlls) / sizeof(dlls[0]) && s_getState == NULL; i++)
	{
		HMODULE module = LoadLibraryA(dlls[i]);
		if (module != NULL)
			s_getState = (XInputGetStateProc)GetProcAddress(module, "XInputGetState");
	}
}

// Windows' buttons to the Xbox's: the d-pad, start, back and the stick
// clicks are the same bits; A B X Y, the shoulders (white and black) and the
// triggers are analog buttons on the Xbox.
static bool ReadXInput(int port, UxPad *pad)
{
	LoadXInput();
	if (s_getState == NULL)
		return false;
	// A missing controller is asked about again once a second (asking is slow).
	DWORD now = GetTickCount();
	if (s_missing[port] && now - s_retryTick[port] < 1000)
		return false;
	XINPUT_STATE state;
	memset(&state, 0, sizeof(state));
	if (s_getState(port, &state) != ERROR_SUCCESS)
	{
		s_missing[port] = true;
		s_retryTick[port] = now;
		return false;
	}
	s_missing[port] = false;
	WORD b = state.Gamepad.wButtons;
	pad->buttons |= b & 0x00FF;
	if (b & XINPUT_GAMEPAD_A)
		pad->analog[0] = 255;
	if (b & XINPUT_GAMEPAD_B)
		pad->analog[1] = 255;
	if (b & XINPUT_GAMEPAD_X)
		pad->analog[2] = 255;
	if (b & XINPUT_GAMEPAD_Y)
		pad->analog[3] = 255;
	if (b & XINPUT_GAMEPAD_RIGHT_SHOULDER)
		pad->analog[4] = 255;   // black
	if (b & XINPUT_GAMEPAD_LEFT_SHOULDER)
		pad->analog[5] = 255;   // white
	if (state.Gamepad.bLeftTrigger > pad->analog[6])
		pad->analog[6] = state.Gamepad.bLeftTrigger;
	if (state.Gamepad.bRightTrigger > pad->analog[7])
		pad->analog[7] = state.Gamepad.bRightTrigger;
	pad->thumbLX = state.Gamepad.sThumbLX;
	pad->thumbLY = state.Gamepad.sThumbLY;
	pad->thumbRX = state.Gamepad.sThumbRX;
	pad->thumbRY = state.Gamepad.sThumbRY;
	return true;
}

static void ReadKeyboardPad(UxPad *pad)
{
	// A B X Y black white LT RT, as the Xbox's analog buttons.
	static const BYTE kAnalogKeys[8] = {VK_F1, VK_F2, VK_F3, VK_F4, VK_F6, VK_F5, VK_PRIOR, VK_NEXT};
	Lock();
	for (int i = 0; i < 8; i++)
		if (s_keys[kAnalogKeys[i]] && !(kAnalogKeys[i] == VK_F4 && s_keys[VK_MENU]))
			pad->analog[i] = 255;
	if (s_keys[VK_TAB])
		pad->buttons |= 0x20;   // back
	if (s_keys[VK_F9])
		pad->buttons |= 0x10;   // start
	Unlock();
}

extern "C" int UxInputGetPad(int port, UxPad *pad)
{
	memset(pad, 0, sizeof(*pad));
	if (port < 0 || port > 3)
		return 0;
	bool connected = ReadXInput(port, pad);
	if (port == 0)
	{
		ReadKeyboardPad(pad);
		connected = true;
	}
	return connected;
}

extern "C" int UxInputGetKey(UxKey *key)
{
	Lock();
	bool any = s_head != s_tail;
	if (any)
	{
		*key = s_queue[s_head];
		s_head = (s_head + 1) % 64;
	}
	Unlock();
	if (!any)
		memset(key, 0, sizeof(*key));
	return any;
}

static void Queue(const UxKey &key)
{
	int next = (s_tail + 1) % 64;
	if (next == s_head)
		return;   // full: dropped
	s_queue[s_tail] = key;
	s_tail = next;
}

extern "C" void UxInputOnMessage(void *, ux_u32 message, size_t wParam, ptrdiff_t lParam)
{
	switch (message)
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	{
		BYTE vk = (BYTE)wParam;
		Lock();
		s_keys[vk] = true;
		if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU)
		{
			Unlock();
			break;
		}
		UxKey key;
		key.virtualKey = vk;
		key.flags = 0;
		if (GetKeyState(VK_CONTROL) & 0x8000)
			key.flags |= 0x01;
		if (GetKeyState(VK_SHIFT) & 0x8000)
			key.flags |= 0x02;
		if (message == WM_SYSKEYDOWN || (GetKeyState(VK_MENU) & 0x8000))
			key.flags |= 0x04;
		if (GetKeyState(VK_CAPITAL) & 1)
			key.flags |= 0x08;
		key.ascii = 0;
		BYTE state[256];
		WORD chars[2] = {0, 0};
		if (!(key.flags & 0x05) && GetKeyboardState(state) &&
		    ToAscii(vk, (UINT)((lParam >> 16) & 0xFF), state, chars, 0) == 1)
		{
			BYTE c = (BYTE)chars[0];
			if ((c >= 0x20 && c < 0x7F) || c == 0x08)
				key.ascii = (char)c;
		}
		Queue(key);
		Unlock();
		break;
	}
	case WM_KEYUP:
	case WM_SYSKEYUP:
		Lock();
		s_keys[(BYTE)wParam] = false;
		Unlock();
		break;
	case WM_KILLFOCUS:
		Lock();
		memset(s_keys, 0, sizeof(s_keys));
		Unlock();
		break;
	}
}
