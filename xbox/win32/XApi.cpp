// XAPI for the Windows build: the XBE's sections (resources in the .exe),
// controllers (XInput) and the keyboard (WinInput.cpp), save games, the
// console's settings, and launching, which a PC can't do.
// (Windows' file functions here take PC paths.)
#define UX_NO_FILEMAP
#include <xtl.h>

#include <map>
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "../../src/common/XboxKernel.h"
#include "UxWin.h"
#include "XFiles.h"

DWORD UxQueryDwordSetting(ULONG index, DWORD fallback);   // XKernel.cpp

static CRITICAL_SECTION s_lock;

static void Lock()
{
	static LONG s_ready;
	if (InterlockedCompareExchange(&s_ready, 1, 0) == 0)
	{
		InitializeCriticalSection(&s_lock);
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

extern "C" {

XPP_DEVICE_TYPE XDEVICE_TYPE_GAMEPAD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_MEMORY_UNIT_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_MOUSE_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_IR_REMOTE_TABLE;

// ---- sections: the RCDATA resources of UnleashX.rc ----

struct Section
{
	void *data;
	DWORD size;
	int loads;
};
// (Objects the dash's global constructors can reach are made on first use:
// the order of those constructors between files is the compiler's.)
static std::map<std::string, Section> &s_sections_()
{
	static std::map<std::string, Section> value;
	return value;
}
#define s_sections s_sections_()

// "$$XSIMAGE" is the resource XSIMAGE.
static std::string ResourceName(LPCSTR name)
{
	std::string n = name != NULL ? name : "";
	while (!n.empty() && n[0] == '$')
		n.erase(0, 1);
	for (size_t i = 0; i < n.size(); i++)
		n[i] = (char)toupper((unsigned char)n[i]);
	return n;
}

static Section *FindSection(LPCSTR name)
{
	std::string key = ResourceName(name);
	std::map<std::string, Section>::iterator it = s_sections.find(key);
	if (it != s_sections.end())
		return &it->second;
	HRSRC resource = FindResourceA(NULL, key.c_str(), MAKEINTRESOURCEA(10) /* RT_RCDATA */);
	if (resource == NULL)
		return NULL;
	HGLOBAL loaded = LoadResource(NULL, resource);
	DWORD size = SizeofResource(NULL, resource);
	const void *bits = loaded != NULL ? LockResource(loaded) : NULL;
	if (bits == NULL)
		return NULL;
	// A writable copy, as the Xbox's sections are (and padded, as they are
	// to their virtual size).
	Section section;
	section.size = size;
	section.loads = 0;
	section.data = VirtualAlloc(NULL, size + 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (section.data == NULL)
		return NULL;
	memcpy(section.data, bits, size);
	s_sections[key] = section;
	return &s_sections[key];
}

PVOID WINAPI XLoadSectionA(LPCSTR name)
{
	Lock();
	Section *section = FindSection(name);
	void *data = NULL;
	if (section != NULL)
	{
		section->loads++;
		data = section->data;
	}
	Unlock();
	if (data == NULL)
		SetLastError(ERROR_NOT_FOUND);
	return data;
}

// (The data stays: other code may still hold it.)
BOOL WINAPI XFreeSectionA(LPCSTR name)
{
	Lock();
	Section *section = FindSection(name);
	if (section != NULL && section->loads > 0)
		section->loads--;
	Unlock();
	return section != NULL;
}

HANDLE WINAPI XGetSectionHandleA(LPCSTR name)
{
	Lock();
	Section *section = FindSection(name);
	Unlock();
	return section != NULL ? (HANDLE)section : INVALID_HANDLE_VALUE;
}

DWORD WINAPI XGetSectionSize(HANDLE handle)
{
	if (handle == NULL || handle == INVALID_HANDLE_VALUE)
		return 0;
	return ((Section *)handle)->size;
}

// ---- devices and input ----

// Controllers are polled in each call; changes are the difference from the
// last time asked, per device type.
static DWORD s_reportedPads;
static bool s_padsReported;

static DWORD ConnectedPads()
{
	DWORD mask = 0;
	UxPad pad;
	for (int port = 0; port < 4; port++)
		if (UxInputGetPad(port, &pad))
			mask |= 1u << port;
	return mask;
}

void WINAPI XInitDevices(DWORD, PXDEVICE_PREALLOC_TYPE)
{
}

DWORD WINAPI XGetDevices(PXPP_DEVICE_TYPE type)
{
	if (type == &XDEVICE_TYPE_GAMEPAD_TABLE)
	{
		Lock();
		s_reportedPads = ConnectedPads();
		s_padsReported = true;
		DWORD mask = s_reportedPads;
		Unlock();
		return mask;
	}
	if (type == &XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE)
		return 1;   // the PC's keyboard, on port 0
	return 0;
}

BOOL WINAPI XGetDeviceChanges(PXPP_DEVICE_TYPE type, PDWORD insertions, PDWORD removals)
{
	*insertions = 0;
	*removals = 0;
	if (type != &XDEVICE_TYPE_GAMEPAD_TABLE)
		return FALSE;
	Lock();
	DWORD now = ConnectedPads();
	if (!s_padsReported)
	{
		s_reportedPads = 0;
		s_padsReported = true;
	}
	*insertions = now & ~s_reportedPads;
	*removals = s_reportedPads & ~now;
	s_reportedPads = now;
	Unlock();
	return *insertions != 0 || *removals != 0;
}

// A handle: the device type's tag and the port.
#define PAD_HANDLE_BASE 0x50414400u
#define KEYBOARD_HANDLE ((HANDLE)(ULONG_PTR)0x4B424400u)

HANDLE WINAPI XInputOpen(PXPP_DEVICE_TYPE type, DWORD port, DWORD, PXINPUT_POLLING_PARAMETERS)
{
	if (port > 3)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return NULL;
	}
	if (type == &XDEVICE_TYPE_GAMEPAD_TABLE)
		return (HANDLE)(ULONG_PTR)(PAD_HANDLE_BASE + port);
	if (type == &XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE && port == 0)
		return KEYBOARD_HANDLE;
	SetLastError(ERROR_DEVICE_NOT_CONNECTED);
	return NULL;
}

void WINAPI XInputClose(HANDLE)
{
}

static int PadPort(HANDLE device)
{
	DWORD value = (DWORD)(ULONG_PTR)device;
	if (value >= PAD_HANDLE_BASE && value < PAD_HANDLE_BASE + 4)
		return (int)(value - PAD_HANDLE_BASE);
	return -1;
}

DWORD WINAPI XInputGetCapabilities(HANDLE device, PXINPUT_CAPABILITIES caps)
{
	memset(caps, 0, sizeof(*caps));
	if (PadPort(device) < 0)
		return ERROR_DEVICE_NOT_CONNECTED;
	caps->SubType = XINPUT_DEVSUBTYPE_GC_GAMEPAD;
	return ERROR_SUCCESS;
}

static DWORD s_packets[4];
static UxPad s_lastPads[4];

DWORD WINAPI XInputGetState(HANDLE device, PXINPUT_STATE state)
{
	int port = PadPort(device);
	if (port < 0)
		return ERROR_DEVICE_NOT_CONNECTED;
	UxPad pad;
	if (!UxInputGetPad(port, &pad))
	{
		memset(state, 0, sizeof(*state));
		return ERROR_DEVICE_NOT_CONNECTED;
	}
	Lock();
	if (memcmp(&pad, &s_lastPads[port], sizeof(pad)) != 0)
	{
		s_lastPads[port] = pad;
		s_packets[port]++;
	}
	state->dwPacketNumber = s_packets[port];
	Unlock();
	state->Gamepad.wButtons = pad.buttons;
	memcpy(state->Gamepad.bAnalogButtons, pad.analog, 8);
	state->Gamepad.sThumbLX = pad.thumbLX;
	state->Gamepad.sThumbLY = pad.thumbLY;
	state->Gamepad.sThumbRX = pad.thumbRX;
	state->Gamepad.sThumbRY = pad.thumbRY;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugInitKeyboardQueue(PXINPUT_DEBUG_KEYQUEUE_PARAMETERS)
{
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugGetKeystroke(PXINPUT_DEBUG_KEYSTROKE keystroke)
{
	UxKey key;
	if (!UxInputGetKey(&key))
	{
		memset(keystroke, 0, sizeof(*keystroke));
		return ERROR_HANDLE_EOF;
	}
	keystroke->VirtualKey = key.virtualKey;
	keystroke->Ascii = key.ascii;
	keystroke->Flags = key.flags;
	return ERROR_SUCCESS;
}

// ---- memory units: none ----

DWORD WINAPI XMUNameFromDriveLetter(CHAR, LPWSTR name, UINT length)
{
	if (name != NULL && length > 0)
		name[0] = 0;
	return ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XUnmountMU(DWORD, DWORD)
{
	return ERROR_DEVICE_NOT_CONNECTED;
}

DWORD __stdcall XMountMURootA(DWORD, DWORD, char *drive)
{
	if (drive != NULL)
		*drive = 0;
	return ERROR_DEVICE_NOT_CONNECTED;
}

NTSTATUS __stdcall MU_CreateDeviceObject(DWORD, DWORD, PANSI_STRING)
{
	return (NTSTATUS)0xC000000E;   // STATUS_NO_SUCH_DEVICE
}

void __stdcall MU_CloseDeviceObject(DWORD, DWORD)
{
}

BOOL __stdcall XapiFormatFATVolume(PANSI_STRING)
{
	return FALSE;
}

// The utility drive (Z:) is a folder like the rest.
BOOL WINAPI XMountUtilityDrive(BOOL)
{
	UxSetDriveDevice('Z', "\\Device\\Harddisk0\\Partition5");
	return TRUE;
}

BOOL WINAPI XFormatUtilityDrive(VOID)
{
	return TRUE;
}

// ---- launching: not on a PC ----

// An XBE's path as the dash shows it: D: (where it was mapped to launch)
// and the partitions' device names as drive letters.
static std::string FriendlyPath(LPCSTR path)
{
	std::string p = path != NULL ? path : "";
	if (p.size() >= 2 && (p[0] == 'D' || p[0] == 'd') && p[1] == ':')
	{
		std::string device = UxDriveDevice('D');
		if (!device.empty())
			p = device + p.substr(2);
	}
	static const char kPartitions[] = "?ECXYZFG";
	const char *kHarddisk = "\\Device\\Harddisk0\\Partition";
	if (_strnicmp(p.c_str(), kHarddisk, strlen(kHarddisk)) == 0)
	{
		int n = p[strlen(kHarddisk)] - '0';
		if (n >= 1 && n <= 7)
			p = std::string(1, kPartitions[n]) + ":" + p.substr(strlen(kHarddisk) + 1);
	}
	else if (_strnicmp(p.c_str(), "\\Device\\CdRom0", 14) == 0)
	{
		p = "DVD:" + p.substr(14);
	}
	return p;
}

static void CannotLaunch(LPCSTR what)
{
	std::string text = "UnleashX for Windows can't launch Xbox programs";
	if (what != NULL && what[0] != 0)
	{
		text += ":\n\n";
		text += what;
	}
	text += ".";
	MessageBoxA(NULL, text.c_str(), "UnleashX", MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
}

DWORD WINAPI XLaunchNewImageA(LPCSTR path, PLAUNCH_DATA)
{
	if (path == NULL)
	{
		// To the dashboard: that's us.
		HalReturnToFirmware(ReturnFirmwareQuickReboot);
		return ERROR_SUCCESS;
	}
	CannotLaunch(FriendlyPath(path).c_str());
	return ERROR_NOT_SUPPORTED;
}

DWORD __stdcall XWriteTitleInfoAndRebootA(LPCSTR launchPath, LPCSTR dDrivePath, DWORD, DWORD, PLAUNCH_DATA)
{
	std::string what = dDrivePath != NULL ? dDrivePath : "";
	if (launchPath != NULL && launchPath[0] != 0)
	{
		if (!what.empty() && what[what.size() - 1] != '\\')
			what += "\\";
		what += launchPath;
	}
	CannotLaunch(FriendlyPath(what.c_str()).c_str());
	return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI XGetLaunchInfo(PDWORD type, PLAUNCH_DATA data)
{
	*type = 0;
	memset(data, 0, sizeof(*data));
	return ERROR_NOT_FOUND;
}

// ---- memory, threads, the file cache ----

LPVOID WINAPI XPhysicalAlloc(SIZE_T size, ULONG_PTR, ULONG_PTR, DWORD protect)
{
	// (Write-combined or not, it's ordinary memory here.)
	DWORD p = protect & 0xFF;
	return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, p != 0 ? p : PAGE_READWRITE);
}

void WINAPI XPhysicalFree(LPVOID address)
{
	VirtualFree(address, 0, MEM_RELEASE);
}

void WINAPI XRegisterThreadNotifyRoutine(PXTHREAD_NOTIFICATION, BOOL)
{
}

static SIZE_T s_fileCacheSize = 64 * 1024;
static DWORD s_quantum = 20;

SIZE_T WINAPI XGetFileCacheSize(VOID)
{
	return s_fileCacheSize;
}

BOOL WINAPI XSetFileCacheSize(SIZE_T size)
{
	s_fileCacheSize = size;
	return TRUE;
}

DWORD WINAPI XGetProcessQuantumLength(VOID)
{
	return s_quantum;
}

void WINAPI XSetProcessQuantumLength(DWORD ms)
{
	s_quantum = ms;
}

// ---- the console's settings ----

DWORD WINAPI XGetLanguage(VOID)
{
	DWORD language = UxQueryDwordSetting(7, 1);
	return language >= 1 && language <= 9 ? language : 1;
}

DWORD WINAPI XGetVideoFlags(VOID)
{
	return UxQueryDwordSetting(8, 0) & 0xFF;
}

DWORD WINAPI XGetAudioFlags(VOID)
{
	return UxQueryDwordSetting(9, 0);
}

DWORD WINAPI XGetVideoStandard(VOID)
{
	DWORD region = UxQueryDwordSetting(0x103, 0x00400100);
	DWORD standard = (region >> 8) & 0xFF;
	return standard != 0 ? standard : XC_VIDEO_STANDARD_NTSC_M;
}

DWORD WINAPI XGetGameRegion(VOID)
{
	DWORD region = UxQueryDwordSetting(0x104, XC_GAME_REGION_NA);
	return region != 0 ? region : XC_GAME_REGION_NA;
}

DWORD WINAPI XGetAVPack(VOID)
{
	return XC_AV_PACK_HDTV;
}

DWORD WINAPI XGetParentalControlSetting(VOID)
{
	return 0;
}

// ---- nicknames and soundtracks: none ----

HANDLE WINAPI XFindFirstNicknameW(BOOL, LPWSTR nickname, UINT size)
{
	if (nickname != NULL && size > 0)
		nickname[0] = 0;
	return INVALID_HANDLE_VALUE;
}

BOOL WINAPI XSetNicknameW(LPCWSTR, BOOL)
{
	return TRUE;
}

HANDLE WINAPI XFindFirstSoundtrack(PXSOUNDTRACK_DATA)
{
	SetLastError(ERROR_NO_MORE_FILES);
	return INVALID_HANDLE_VALUE;
}

BOOL WINAPI XFindNextSoundtrack(HANDLE, PXSOUNDTRACK_DATA)
{
	SetLastError(ERROR_NO_MORE_FILES);
	return FALSE;
}

BOOL WINAPI XGetSoundtrackSongInfo(DWORD, UINT, PDWORD, PDWORD, PWSTR name, UINT size)
{
	if (name != NULL && size > 0)
		name[0] = 0;
	return FALSE;
}

HANDLE WINAPI XOpenSoundtrackSong(DWORD, BOOL)
{
	return INVALID_HANDLE_VALUE;
}

// ---- save games: folders under the root (U:\) with a SaveMeta.xbx ----

// The folder name of a save: 12 hex digits from its name, as XAPI's are.
static std::string SaveFolderName(LPCWSTR name)
{
	unsigned long long hash = 0xCBF29CE484222325ull;
	for (const wchar_t *p = name; p != NULL && *p != 0; p++)
	{
		hash ^= (unsigned short)towupper(*p);
		hash *= 0x100000001B3ull;
	}
	char text[16];
	sprintf(text, "%012llX", hash & 0xFFFFFFFFFFFFull);
	return text;
}

static std::string WithSlash(LPCSTR root)
{
	std::string r = root != NULL ? root : "";
	if (!r.empty() && r[r.size() - 1] != '\\')
		r += "\\";
	return r;
}

// (The Windows file functions here take PC paths: UxMapPath.)

// The name in a SaveMeta.xbx (UTF-16, "Name=...").
static bool ReadSaveName(const std::string &metaPath, WCHAR *name, size_t size)
{
	std::vector<unsigned char> data;
	if (!UxReadFile(metaPath.c_str(), data) || data.size() < 2)
		return false;
	std::wstring text((const wchar_t *)&data[0], data.size() / 2);
	if (!text.empty() && text[0] == 0xFEFF)
		text.erase(0, 1);
	size_t at = text.find(L"Name=");
	if (at == std::wstring::npos)
		return false;
	at += 5;
	size_t end = text.find_first_of(L"\r\n", at);
	std::wstring value = text.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at);
	wcsncpy(name, value.c_str(), size - 1);
	name[size - 1] = 0;
	return true;
}

DWORD WINAPI XCreateSaveGame(LPCSTR root, LPCWSTR name, DWORD disposition, DWORD, LPSTR pathBuffer, UINT size)
{
	if (name == NULL || name[0] == 0)
		return ERROR_INVALID_NAME;
	std::string folder = WithSlash(root) + SaveFolderName(name) + "\\";
	std::string pc = UxMapPath(folder.c_str());
	bool exists = GetFileAttributesA(pc.c_str()) != INVALID_FILE_ATTRIBUTES;
	if (exists && disposition == CREATE_NEW)
		return ERROR_ALREADY_EXISTS;
	if (!exists && disposition == OPEN_EXISTING)
		return ERROR_PATH_NOT_FOUND;
	if (!exists)
	{
		if (!CreateDirectoryA(pc.substr(0, pc.size() - 1).c_str(), NULL))
		{
			DWORD error = GetLastError();
			UxLog("XCreateSaveGame: %s: error %lu", pc.c_str(), (unsigned long)error);
			return error;
		}
		std::wstring meta = std::wstring(L"\xFEFFName=") + name + L"\r\n";
		HANDLE f = CreateFileA((pc + "SaveMeta.xbx").c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		                       FILE_ATTRIBUTE_NORMAL, NULL);
		if (f != INVALID_HANDLE_VALUE)
		{
			DWORD written;
			WriteFile(f, meta.c_str(), (DWORD)(meta.size() * sizeof(wchar_t)), &written, NULL);
			CloseHandle(f);
		}
	}
	if (pathBuffer != NULL && size > 0)
	{
		strncpy(pathBuffer, folder.c_str(), size - 1);
		pathBuffer[size - 1] = 0;
	}
	return ERROR_SUCCESS;
}

static void DeleteTree(const std::string &folder)
{
	WIN32_FIND_DATAA data;
	HANDLE find = FindFirstFileA((folder + "*").c_str(), &data);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
				continue;
			std::string path = folder + data.cFileName;
			if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				DeleteTree(path + "\\");
			else
			{
				SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileA(path.c_str());
			}
		} while (FindNextFileA(find, &data));
		FindClose(find);
	}
	std::string bare = folder.substr(0, folder.size() - 1);
	RemoveDirectoryA(bare.c_str());
}

DWORD WINAPI XDeleteSaveGame(LPCSTR root, LPCWSTR name)
{
	std::string folder = UxMapPath((WithSlash(root) + SaveFolderName(name)).c_str()) + "\\";
	if (GetFileAttributesA(folder.c_str()) == INVALID_FILE_ATTRIBUTES)
		return ERROR_PATH_NOT_FOUND;
	DeleteTree(folder);
	return ERROR_SUCCESS;
}

// A save-game search (XFindClose closes it).
struct SaveFind
{
	DWORD magic;
	std::string root;     // the Xbox path
	std::string pcRoot;   // and the PC's
	HANDLE find;
};
#define SAVE_FIND_MAGIC 0x53415646u

static BOOL NextSave(SaveFind *search, WIN32_FIND_DATAA &data, bool first, PXGAME_FIND_DATA out)
{
	for (;;)
	{
		if (!first && !FindNextFileA(search->find, &data))
			return FALSE;
		first = false;
		if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;
		std::string folder = search->root + data.cFileName + "\\";
		WCHAR name[MAX_GAMENAME];
		if (!ReadSaveName(folder + "SaveMeta.xbx", name, MAX_GAMENAME))
			continue;
		out->wfd = data;
		strncpy(out->szSaveGameDirectory, folder.c_str(), MAX_PATH - 1);
		out->szSaveGameDirectory[MAX_PATH - 1] = 0;
		memcpy(out->szSaveGameName, name, sizeof(name));
		return TRUE;
	}
}

HANDLE WINAPI XFindFirstSaveGame(LPCSTR root, PXGAME_FIND_DATA data)
{
	SaveFind *search = new SaveFind;
	search->magic = SAVE_FIND_MAGIC;
	search->root = WithSlash(root);
	search->pcRoot = WithSlash(UxMapPath(search->root.c_str()).c_str());
	WIN32_FIND_DATAA found;
	search->find = FindFirstFileA((search->pcRoot + "*").c_str(), &found);
	if (search->find == INVALID_HANDLE_VALUE || !NextSave(search, found, true, data))
	{
		if (search->find != INVALID_HANDLE_VALUE)
			FindClose(search->find);
		delete search;
		SetLastError(ERROR_NO_MORE_FILES);
		return INVALID_HANDLE_VALUE;
	}
	return (HANDLE)search;
}

BOOL WINAPI XFindNextSaveGame(HANDLE handle, PXGAME_FIND_DATA data)
{
	SaveFind *search = (SaveFind *)handle;
	if (search == NULL || handle == INVALID_HANDLE_VALUE || search->magic != SAVE_FIND_MAGIC)
		return FALSE;
	WIN32_FIND_DATAA found;
	if (!NextSave(search, found, false, data))
	{
		SetLastError(ERROR_NO_MORE_FILES);
		return FALSE;
	}
	return TRUE;
}

// ---- downloaded content: folders under <root>$c\ with a ContentMeta.xbx ----
// (On the Xbox the meta file is signed and binary; here its name is a
// SaveMeta-style "Name=" line if it has one, else the folder's name.)
#define CONTENT_FIND_MAGIC 0x434E5446u

static BOOL NextContent(SaveFind *search, WIN32_FIND_DATAA &data, bool first, PXCONTENT_FIND_DATA out)
{
	for (;;)
	{
		if (!first && !FindNextFileA(search->find, &data))
			return FALSE;
		first = false;
		if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0] == '.')
			continue;
		std::string folder = search->root + data.cFileName + "\\";
		if (GetFileAttributesA(UxMapPath((folder + "ContentMeta.xbx").c_str()).c_str()) == INVALID_FILE_ATTRIBUTES)
			continue;
		memset(out, 0, sizeof(*out));
		out->wfd = data;
		out->qwOfferingId = _strtoui64(data.cFileName, NULL, 16);
		strncpy(out->szContentDirectory, folder.c_str(), MAX_PATH - 1);
		WCHAR name[MAX_CONTENT_DISPLAY_NAME];
		if (!ReadSaveName(folder + "ContentMeta.xbx", name, MAX_CONTENT_DISPLAY_NAME))
			MultiByteToWideChar(CP_ACP, 0, data.cFileName, -1, name, MAX_CONTENT_DISPLAY_NAME);
		name[MAX_CONTENT_DISPLAY_NAME - 1] = 0;
		memcpy(out->szDisplayName, name, sizeof(name));
		return TRUE;
	}
}

HANDLE WINAPI XFindFirstContent(LPCSTR root, DWORD, PXCONTENT_FIND_DATA data)
{
	SaveFind *search = new SaveFind;
	search->magic = CONTENT_FIND_MAGIC;
	search->root = WithSlash(root) + "$c\\";
	search->pcRoot = WithSlash(UxMapPath(search->root.c_str()).c_str());
	WIN32_FIND_DATAA found;
	search->find = FindFirstFileA((search->pcRoot + "*").c_str(), &found);
	if (search->find == INVALID_HANDLE_VALUE || !NextContent(search, found, true, data))
	{
		if (search->find != INVALID_HANDLE_VALUE)
			FindClose(search->find);
		delete search;
		SetLastError(ERROR_NO_MORE_FILES);
		return INVALID_HANDLE_VALUE;
	}
	return (HANDLE)search;
}

BOOL WINAPI XFindNextContent(HANDLE handle, PXCONTENT_FIND_DATA data)
{
	SaveFind *search = (SaveFind *)handle;
	if (search == NULL || handle == INVALID_HANDLE_VALUE || search->magic != CONTENT_FIND_MAGIC)
		return FALSE;
	WIN32_FIND_DATAA found;
	if (!NextContent(search, found, false, data))
	{
		SetLastError(ERROR_NO_MORE_FILES);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI XFindClose(HANDLE handle)
{
	SaveFind *search = (SaveFind *)handle;
	if (search == NULL || handle == INVALID_HANDLE_VALUE ||
	    (search->magic != SAVE_FIND_MAGIC && search->magic != CONTENT_FIND_MAGIC))
		return FALSE;
	FindClose(search->find);
	search->magic = 0;
	delete search;
	return TRUE;
}

// The size of a folder in 16 KB blocks, as the Xbox's dashboard counts.
static ULONGLONG FolderBlocks(const std::string &folder)
{
	ULONGLONG blocks = 1;
	WIN32_FIND_DATAA data;
	HANDLE find = FindFirstFileA((folder + "*").c_str(), &data);
	if (find == INVALID_HANDLE_VALUE)
		return blocks;
	do
	{
		// (Not "." and "..": this takes the PC's own FindFirstFileA, which
		// lists them, and walked round the disk without end.)
		if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
			continue;
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			blocks += FolderBlocks(folder + data.cFileName + "\\");
		else
			blocks += ((((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow) + 0x3FFF) / 0x4000;
	} while (FindNextFileA(find, &data));
	FindClose(find);
	return blocks;
}

DWORD WINAPI XGetDisplayBlocks(LPCSTR path)
{
	ULONGLONG blocks = FolderBlocks(WithSlash(UxMapPath(path).c_str()));
	return blocks > 50000 ? 50001 : (DWORD)blocks;
}

}   // extern "C"
