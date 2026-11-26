// The Xbox kernel's exports the dash calls, for the Windows build: symbolic
// links (drive letters, XFiles.cpp), memory, time, the EEPROM settings (kept
// in a file next to the drives) and the hardware, which isn't there.
// (Windows' file functions here take PC paths.)
#define UX_NO_FILEMAP
#include <xtl.h>

#include <map>
#include <string>
#include <stdio.h>
#include <string.h>

#include "../../src/common/XboxKernel.h"
#include "UxWin.h"
#include "XFiles.h"

#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001)
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034)
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023)

// A symbolic link's handle carries its drive letter.
#define LINK_HANDLE_TAG 0x4C4E0000u
#define IS_LINK_HANDLE(h) ((((DWORD)(ULONG_PTR)(h)) & 0xFFFF0000u) == LINK_HANDLE_TAG)

static std::string AnsiString(const ANSI_STRING *s)
{
	if (s == NULL || s->Buffer == NULL)
		return std::string();
	return std::string(s->Buffer, s->Length);
}

// The drive letter of \??\X: (or X:).
static char LinkLetter(const std::string &name)
{
	std::string n = name;
	if (n.compare(0, 4, "\\??\\") == 0)
		n = n.substr(4);
	if (n.size() >= 2 && n[1] == ':' && isalpha((unsigned char)n[0]))
		return (char)toupper((unsigned char)n[0]);
	return 0;
}

static char s_imagePath[] = UX_DASH_DEVICE_PATH;

extern "C" {

ANSI_STRING XeImageFileName = {(USHORT)(sizeof(s_imagePath) - 1), (USHORT)sizeof(s_imagePath), s_imagePath};
STRING HalDiskSerialNumber = {16, 17, (PCHAR) "UNLEASHXWIN00001"};
STRING HalDiskModelNumber = {16, 17, (PCHAR) "UnleashX Windows"};
XBOX_KRNL_VERSION XboxKrnlVersion = {1, 0, 5838, 1};
XBOX_HARDWARE_INFO XboxHardwareInfo = {0x00000020, 0xD2, 0xD2, {0, 0}};
BYTE XboxHDKey[16];

// ---- strings and critical sections ----

void __stdcall RtlInitAnsiString(PANSI_STRING destination, const char *source)
{
	destination->Buffer = (PCHAR)source;
	destination->Length = (USHORT)(source != NULL ? strlen(source) : 0);
	destination->MaximumLength = (USHORT)(source != NULL ? destination->Length + 1 : 0);
}

// The dash's two kernel critical sections are statics set up by global
// constructors, which another file's constructors can run before (the
// order between files is the compiler's). An all-zero one, though, looks
// held to Windows, and waiting on it crashes (no debug info to count the
// contention in). So one that hasn't been set up yet is, when first used,
// and setting up one twice does nothing. (Windows always gives one debug
// info, or -1 for none: zero means untouched.)
static void EnsureInitialized(RTL_CRITICAL_SECTION *section)
{
	static LONG s_busy;
	if (section->DebugInfo != NULL)
		return;
	while (InterlockedExchange(&s_busy, 1) != 0)
		Sleep(0);
	if (section->DebugInfo == NULL)
		InitializeCriticalSection((CRITICAL_SECTION *)section);
	InterlockedExchange(&s_busy, 0);
}

void WINAPI RtlInitializeCriticalSection(RTL_CRITICAL_SECTION *section)
{
	EnsureInitialized(section);
}

void WINAPI RtlEnterCriticalSection(RTL_CRITICAL_SECTION *section)
{
	EnsureInitialized(section);
	EnterCriticalSection((CRITICAL_SECTION *)section);
}

void WINAPI RtlLeaveCriticalSection(RTL_CRITICAL_SECTION *section)
{
	LeaveCriticalSection((CRITICAL_SECTION *)section);
}

// ---- symbolic links: the drive letters ----

NTSTATUS __stdcall IoCreateSymbolicLink(PSTRING link, PSTRING device)
{
	char letter = LinkLetter(AnsiString(link));
	if (letter == 0)
		return STATUS_UNSUCCESSFUL;
	std::string target = AnsiString(device);
	if (!UxDeviceExists(target.c_str()))
		return STATUS_OBJECT_NAME_NOT_FOUND;   // (another DVD drive, a memory unit...)
	UxLog("%c: is %s", letter, target.c_str());
	UxSetDriveDevice(letter, target.c_str());
	return STATUS_SUCCESS;
}

NTSTATUS __stdcall IoDeleteSymbolicLink(PSTRING link)
{
	char letter = LinkLetter(AnsiString(link));
	if (letter == 0 || UxDriveDevice(letter).empty())
		return STATUS_OBJECT_NAME_NOT_FOUND;
	UxSetDriveDevice(letter, NULL);
	return STATUS_SUCCESS;
}

NTSTATUS __stdcall NtOpenSymbolicLinkObject(PHANDLE handle, POBJECT_ATTRIBUTES attributes)
{
	*handle = NULL;
	char letter = attributes != NULL ? LinkLetter(AnsiString(attributes->ObjectName)) : 0;
	if (letter == 0 || UxDriveDevice(letter).empty())
		return STATUS_OBJECT_NAME_NOT_FOUND;
	*handle = (HANDLE)(ULONG_PTR)(LINK_HANDLE_TAG | (BYTE)letter);
	return STATUS_SUCCESS;
}

NTSTATUS __stdcall NtQuerySymbolicLinkObject(HANDLE handle, PSTRING target, PULONG returnedLength)
{
	if (!IS_LINK_HANDLE(handle) || target == NULL)
		return STATUS_UNSUCCESSFUL;
	std::string device = UxDriveDevice((char)((ULONG_PTR)handle & 0xFF));
	if (returnedLength != NULL)
		*returnedLength = (ULONG)device.size();
	if (target->Buffer == NULL || target->MaximumLength < device.size())
		return STATUS_BUFFER_TOO_SMALL;
	memcpy(target->Buffer, device.data(), device.size());
	target->Length = (USHORT)device.size();
	if (target->MaximumLength > device.size())
		target->Buffer[device.size()] = 0;
	return STATUS_SUCCESS;
}

NTSTATUS __stdcall NtClose(HANDLE handle)
{
	if (handle == NULL || IS_LINK_HANDLE(handle))
		return STATUS_SUCCESS;
	return CloseHandle(handle) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

// The raw devices (disks, the DVD drive, memory units) aren't there.
NTSTATUS __stdcall NtCreateFile(HANDLE *handle, ULONG, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK status,
                                LARGE_INTEGER *, ULONG, ULONG, ULONG, ULONG)
{
	*handle = NULL;
	if (status != NULL)
	{
		status->Status = STATUS_OBJECT_NAME_NOT_FOUND;
		status->Information = 0;
	}
	return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS __stdcall NtOpenFile(HANDLE *handle, ULONG, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK status, ULONG, ULONG)
{
	*handle = NULL;
	if (status != NULL)
	{
		status->Status = STATUS_OBJECT_NAME_NOT_FOUND;
		status->Information = 0;
	}
	return STATUS_OBJECT_NAME_NOT_FOUND;
}

// ---- memory ----

LONG __stdcall NtAllocateVirtualMemory(void **baseAddress, ULONG, DWORD *regionSize, DWORD allocationType,
                                       DWORD protect)
{
	void *p = VirtualAlloc(*baseAddress, *regionSize, allocationType & ~MEM_NOZERO, protect);
	if (p == NULL)
		return (LONG)0xC0000017;   // STATUS_NO_MEMORY
	*baseAddress = p;
	return STATUS_SUCCESS;
}

LONG __stdcall NtFreeVirtualMemory(void **baseAddress, DWORD *regionSize, DWORD freeType)
{
	BOOL ok = (freeType & MEM_RELEASE) ? VirtualFree(*baseAddress, 0, MEM_RELEASE)
	                                   : VirtualFree(*baseAddress, *regionSize, freeType);
	return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

ULONG __stdcall MmQueryAddressProtect(void *address)
{
	MEMORY_BASIC_INFORMATION info;
	if (VirtualQuery(address, &info, sizeof(info)) == 0)
		return 0;
	return info.Protect;
}

void __stdcall MmSetAddressProtect(void *address, ULONG bytes, ULONG protect)
{
	DWORD old;
	VirtualProtect(address, bytes, protect, &old);
}

// ---- time ----

void __stdcall KeQuerySystemTime(LARGE_INTEGER *time)
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	time->LowPart = ft.dwLowDateTime;
	time->HighPart = (LONG)ft.dwHighDateTime;
}

// The PC's clock is Windows' to set.
NTSTATUS __stdcall NtSetSystemTime(LARGE_INTEGER *, LARGE_INTEGER *previous)
{
	if (previous != NULL)
		KeQuerySystemTime(previous);
	return STATUS_SUCCESS;
}

// ---- hardware ----

NTSTATUS __stdcall HalReadSMBusValue(UCHAR address, UCHAR command, BOOLEAN, ULONG *value)
{
	*value = 0;
	if (address != 0x20)
		return STATUS_UNSUCCESSFUL;   // (no video encoder, EEPROM...)
	// The SMC: its version ("P01", a 1.0, a character a read), and the CPU
	// and board temperatures.
	static int s_versionIndex;
	switch (command)
	{
	case 0x01: *value = (ULONG)(unsigned char)"P01"[s_versionIndex++ % 3]; break;
	case 0x09: *value = 45; break;
	case 0x0A: *value = 35; break;
	}
	return STATUS_SUCCESS;
}

static void Restart();

NTSTATUS __stdcall HalWriteSMBusValue(UCHAR address, UCHAR command, BOOLEAN, ULONG value)
{
	// The SMC's power register: reset and power cycle start the program
	// again, power off quits.
	if (address == 0x20 && command == 0x02)
	{
		UxLog("SMC power 0x%02lX", (unsigned long)value);
		if (value == 0x01 || value == 0x40)
			Restart();
		if (value == 0x01 || value == 0x40 || value == 0x80)
			ExitProcess(0);
	}
	return STATUS_SUCCESS;
}

// The tray: closed, no disc.
NTSTATUS __stdcall HalReadSMCTrayState(ULONG *state, ULONG *changeCount)
{
	if (state != NULL)
		*state = 0x40;
	if (changeCount != NULL)
		*changeCount = 0;
	return STATUS_SUCCESS;
}

// Rebooting starts the program again; halting quits.
static void Restart()
{
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, sizeof(path));
	STARTUPINFOA startup;
	memset(&startup, 0, sizeof(startup));
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process;
	char commandLine[MAX_PATH + 2];
	sprintf(commandLine, "\"%s\"", path);
	if (CreateProcessA(path, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process))
	{
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
	}
}

void __stdcall HalReturnToFirmware(FIRMWARE_REENTRY routine)
{
	UxLog("HalReturnToFirmware(%d)", (int)routine);
	if (routine != ReturnFirmwareHalt)
		Restart();
	ExitProcess(0);
}

void __stdcall HalInitiateShutdown()
{
	UxLog("HalInitiateShutdown");
	ExitProcess(0);
}

// ---- the EEPROM settings: XboxHDD\eeprom.txt, "index type value" lines ----

struct Setting
{
	ULONG type;
	std::string value;
};

static CRITICAL_SECTION s_settingsLock;
static bool s_settingsLoaded;
// (Objects the dash's global constructors can reach are made on first use:
// the order of those constructors between files is the compiler's.)
static std::map<ULONG, Setting> &s_settings_()
{
	static std::map<ULONG, Setting> value;
	return value;
}
#define s_settings s_settings_()

static std::string SettingsPath()
{
	return std::string(UxXboxRoot()) + "\\eeprom.txt";
}

static void LoadSettings()
{
	if (s_settingsLoaded)
		return;
	s_settingsLoaded = true;
	// The defaults: English, no video modes, stereo, NTSC, North America.
	Setting dw;
	dw.type = REG_DWORD;
	DWORD defaults[][2] = {{7, 1}, {8, 0}, {9, 0}, {0x103, 0x00400100}, {0x104, 1}};
	for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
	{
		dw.value.assign((const char *)&defaults[i][1], 4);
		s_settings[defaults[i][0]] = dw;
	}
	FILE *f = ::fopen(SettingsPath().c_str(), "r");
	if (f == NULL)
		return;
	char line[1024];
	while (fgets(line, sizeof(line), f) != NULL)
	{
		unsigned long index, type;
		char hex[900];
		if (sscanf(line, "%lx %lx %899s", &index, &type, hex) != 3)
			continue;
		Setting s;
		s.type = type;
		for (size_t i = 0; hex[i] != 0 && hex[i + 1] != 0; i += 2)
		{
			unsigned int byte;
			sscanf(hex + i, "%2x", &byte);
			s.value += (char)byte;
		}
		s_settings[index] = s;
	}
	fclose(f);
}

static void SaveSettings()
{
	FILE *f = ::fopen(SettingsPath().c_str(), "w");
	if (f == NULL)
		return;
	for (std::map<ULONG, Setting>::iterator it = s_settings.begin(); it != s_settings.end(); ++it)
	{
		fprintf(f, "%lx %lx ", (unsigned long)it->first, (unsigned long)it->second.type);
		for (size_t i = 0; i < it->second.value.size(); i++)
			fprintf(f, "%02x", (unsigned char)it->second.value[i]);
		fprintf(f, "%s\n", it->second.value.empty() ? "00" : "");
	}
	fclose(f);
}

static void LockSettings()
{
	static LONG s_ready;
	if (InterlockedCompareExchange(&s_ready, 1, 0) == 0)
	{
		InitializeCriticalSection(&s_settingsLock);
		InterlockedExchange(&s_ready, 2);
	}
	while (s_ready != 2)
		Sleep(0);
	EnterCriticalSection(&s_settingsLock);
	LoadSettings();
}

NTSTATUS __stdcall ExQueryNonVolatileSetting(ULONG index, ULONG *type, void *value, ULONG length, ULONG *resultLength)
{
	LockSettings();
	NTSTATUS status = STATUS_OBJECT_NAME_NOT_FOUND;
	std::map<ULONG, Setting>::iterator it = s_settings.find(index);
	if (it != s_settings.end())
	{
		if (type != NULL)
			*type = it->second.type;
		ULONG n = (ULONG)it->second.value.size();
		if (resultLength != NULL)
			*resultLength = n;
		if (n > length)
		{
			status = STATUS_BUFFER_TOO_SMALL;
		}
		else
		{
			memcpy(value, it->second.value.data(), n);
			status = STATUS_SUCCESS;
		}
	}
	else if (value != NULL && length > 0)
	{
		// Anything else reads as zero.
		memset(value, 0, length);
		if (type != NULL)
			*type = REG_DWORD;
		if (resultLength != NULL)
			*resultLength = length;
		status = STATUS_SUCCESS;
	}
	LeaveCriticalSection(&s_settingsLock);
	return status;
}

NTSTATUS __stdcall ExSaveNonVolatileSetting(ULONG index, ULONG type, void *value, ULONG length)
{
	LockSettings();
	Setting s;
	s.type = type;
	s.value.assign((const char *)value, length);
	s_settings[index] = s;
	SaveSettings();
	LeaveCriticalSection(&s_settingsLock);
	return STATUS_SUCCESS;
}

}   // extern "C"

// For XApi.cpp: a DWORD setting.
DWORD UxQueryDwordSetting(ULONG index, DWORD fallback)
{
	DWORD value = fallback, type = 0;
	ULONG length = 0;
	if (ExQueryNonVolatileSetting(index, &type, &value, sizeof(value), &length) < 0)
		return fallback;
	return value;
}
