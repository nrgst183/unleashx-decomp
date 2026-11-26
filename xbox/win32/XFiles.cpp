// The Xbox's drives as folders on the PC, for the Windows build.
//
// Under the root (UNLEASHX_XBOX_ROOT, or "XboxHDD" next to the program)
// each drive is a folder: C, E, F, G, X, Y, Z (the hard disk's partitions)
// and DVD (the disc drive, D: unless a launch maps D: elsewhere). The kernel's
// device names map the same way (\Device\Harddisk0\Partition1 is E, ...). The
// file functions the dash calls with Xbox paths (PlatformWin32.h maps them
// to these) translate them and call Windows.
#include <xtl.h>

#include <map>
#include <string>
#include <vector>
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "UxWin.h"
#include "XFiles.h"

static CRITICAL_SECTION s_lock;
static bool s_lockReady;
// (Objects the dash's global constructors can reach are made on first use:
// the order of those constructors between files is the compiler's.)
static std::string &s_root_()
{
	static std::string value;
	return value;
}
#define s_root s_root_()
static std::map<char, std::string> &s_devices_()
{
	static std::map<char, std::string> value;
	return value;
}
#define s_devices s_devices_()  // drive letter -> kernel device

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

void UxLog(const char *format, ...)
{
	char text[1024];
	va_list args;
	va_start(args, format);
	_vsnprintf(text, sizeof(text) - 3, format, args);
	va_end(args);
	text[sizeof(text) - 3] = 0;
	strcat(text, "\r\n");
	OutputDebugStringA(text);
	static CRITICAL_SECTION s_logLock;
	static LONG s_ready;
	if (InterlockedCompareExchange(&s_ready, 1, 0) == 0)
	{
		InitializeCriticalSection(&s_logLock);
		InterlockedExchange(&s_ready, 2);
	}
	while (s_ready != 2)
		Sleep(0);
	EnterCriticalSection(&s_logLock);
	static FILE *s_file;
	if (s_file == NULL)
	{
		char path[MAX_PATH];
		GetModuleFileNameA(NULL, path, sizeof(path));
		char *dot = strrchr(path, '.');
		if (dot != NULL)
			strcpy(dot, ".log");
		// (Kept across restarts, as a reboot is one; started over past 1 MB.)
		s_file = ::fopen(path, "a");
		if (s_file != NULL)
			fseek(s_file, 0, SEEK_END);
		if (s_file != NULL && ftell(s_file) > 1024 * 1024)
		{
			fclose(s_file);
			s_file = ::fopen(path, "w");
		}
		if (s_file != NULL)
			fprintf(s_file, "----\n");
	}
	if (s_file != NULL)
	{
		SYSTEMTIME t;
		GetLocalTime(&t);
		fprintf(s_file, "%02d:%02d:%02d.%03d %s", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, text);
		fflush(s_file);
	}
	LeaveCriticalSection(&s_logLock);
}

// Partition number to drive letter, as the Xbox's dashboards map them.
static const char kPartitionLetters[] = {0, 'E', 'C', 'X', 'Y', 'Z', 'F', 'G'};

static void CreateTree(const std::string &path)
{
	std::string partial;
	for (size_t i = 0; i < path.size(); i++)
	{
		partial += path[i];
		if ((path[i] == '\\' || i + 1 == path.size()) && partial.size() > 3)
			CreateDirectoryA(partial.c_str(), NULL);
	}
}

const char *UxXboxRoot(void)
{
	Locker lock;
	if (s_root.empty())
	{
		char buffer[MAX_PATH * 2];
		DWORD n = GetEnvironmentVariableA("UNLEASHX_XBOX_ROOT", buffer, sizeof(buffer));
		if (n > 0 && n < sizeof(buffer))
		{
			s_root = buffer;
		}
		else
		{
			GetModuleFileNameA(NULL, buffer, sizeof(buffer));
			char *slash = strrchr(buffer, '\\');
			if (slash != NULL)
				*slash = 0;
			s_root = std::string(buffer) + "\\XboxHDD";
		}
		while (!s_root.empty() && (s_root[s_root.size() - 1] == '\\' || s_root[s_root.size() - 1] == '/'))
			s_root.erase(s_root.size() - 1);
		const char *drives[] = {"C", "E", "F", "G", "X", "Y", "Z", "DVD"};
		for (size_t i = 0; i < sizeof(drives) / sizeof(drives[0]); i++)
			CreateTree(s_root + "\\" + drives[i]);
		for (int p = 1; p <= 7; p++)
		{
			char device[64];
			sprintf(device, "\\Device\\Harddisk0\\Partition%d", p);
			s_devices[kPartitionLetters[p]] = device;
		}
		s_devices['D'] = "\\Device\\CdRom0";
	}
	return s_root.c_str();
}

// The folder of a kernel device: a partition, the disc, or a folder path
// (a launch maps D: to the XBE's folder, \Device\Harddisk0\Partition1\Apps\...).
static std::string DeviceFolder(const std::string &device)
{
	std::string lower = device;
	for (size_t i = 0; i < lower.size(); i++)
		lower[i] = (char)tolower((unsigned char)lower[i]);
	const char *kHarddisk = "\\device\\harddisk0\\partition";
	if (lower.compare(0, strlen(kHarddisk), kHarddisk) == 0)
	{
		size_t p = strlen(kHarddisk);
		int partition = 0;
		while (p < lower.size() && isdigit((unsigned char)lower[p]))
			partition = partition * 10 + (lower[p++] - '0');
		char letter = partition >= 1 && partition <= 7 ? kPartitionLetters[partition] : 'E';
		std::string rest = device.substr(p);
		return std::string(UxXboxRoot()) + "\\" + letter + rest;
	}
	const char *kCdRom = "\\device\\cdrom0";
	if (lower.compare(0, strlen(kCdRom), kCdRom) == 0)
		return std::string(UxXboxRoot()) + "\\DVD" + device.substr(strlen(kCdRom));
	// Anything else (another drive, a memory unit) isn't there.
	return std::string(UxXboxRoot()) + "\\.missing" + device;
}

// Only the hard disk's partitions and the DVD drive are there (folders).
bool UxDeviceExists(const char *device)
{
	std::string lower = device != NULL ? device : "";
	for (size_t i = 0; i < lower.size(); i++)
		lower[i] = (char)tolower((unsigned char)lower[i]);
	const char *kHarddisk = "\\device\\harddisk0\\partition";
	if (lower.compare(0, strlen(kHarddisk), kHarddisk) == 0)
	{
		int partition = atoi(lower.c_str() + strlen(kHarddisk));
		return partition >= 1 && partition <= 7;
	}
	const char *kCdRom = "\\device\\cdrom0";
	return lower.compare(0, strlen(kCdRom), kCdRom) == 0 &&
	       (lower.size() == strlen(kCdRom) || lower[strlen(kCdRom)] == '\\');
}

std::string UxDriveFolder(char letter)
{
	UxXboxRoot();
	letter = (char)toupper((unsigned char)letter);
	Locker lock;
	std::map<char, std::string>::iterator it = s_devices.find(letter);
	if (it != s_devices.end())
		return DeviceFolder(it->second);
	return std::string(UxXboxRoot()) + "\\" + letter;
}

void UxSetDriveDevice(char letter, const char *device)
{
	UxXboxRoot();
	Locker lock;
	letter = (char)toupper((unsigned char)letter);
	if (device == NULL || device[0] == 0)
	{
		s_devices.erase(letter);
		return;
	}
	s_devices[letter] = device;
	if (UxDeviceExists(device))
		CreateTree(DeviceFolder(device));
}

std::string UxDriveDevice(char letter)
{
	UxXboxRoot();
	Locker lock;
	letter = (char)toupper((unsigned char)letter);
	std::map<char, std::string>::iterator it = s_devices.find(letter);
	return it != s_devices.end() ? it->second : std::string();
}

std::string UxMapPath(const char *xboxPath)
{
	if (xboxPath == NULL)
		return std::string();
	std::string path = xboxPath;
	for (size_t i = 0; i < path.size(); i++)
		if (path[i] == '/')
			path[i] = '\\';
	// \??\E:\... and \\.\E:
	if (path.compare(0, 4, "\\??\\") == 0 || path.compare(0, 4, "\\\\.\\") == 0)
		path = path.substr(4);
	if (path.size() >= 2 && path[1] == ':' && isalpha((unsigned char)path[0]))
	{
		std::string rest = path.substr(2);
		if (!rest.empty() && rest[0] != '\\')
			rest = "\\" + rest;
		return UxDriveFolder(path[0]) + rest;
	}
	if (path.compare(0, 8, "\\Device\\") == 0 || path.compare(0, 8, "\\device\\") == 0)
		return DeviceFolder(path);
	// Relative: from D:, as XAPI does.
	if (!path.empty() && path[0] == '\\')
		return UxDriveFolder('D') + path;
	return UxDriveFolder('D') + "\\" + path;
}

bool UxReadFile(const char *xboxPath, std::vector<unsigned char> &data)
{
	data.clear();
	std::string path = UxMapPath(xboxPath);
	FILE *f = ::fopen(path.c_str(), "rb");
	if (f == NULL)
		return false;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size > 0)
	{
		data.resize(size);
		if (fread(&data[0], 1, size, f) != (size_t)size)
			data.clear();
	}
	fclose(f);
	return size == 0 || !data.empty();
}

// ---- the file functions, with Xbox paths ----

extern "C" {

HANDLE WINAPI UxCreateFileA(LPCSTR path, DWORD access, DWORD share, LPVOID security, DWORD disposition, DWORD flags,
                            HANDLE templateFile)
{
	std::string pc = UxMapPath(path);
	// (The Xbox has no backup semantics; folders open for their times.)
	return CreateFileA(pc.c_str(), access, share | FILE_SHARE_READ, (LPSECURITY_ATTRIBUTES)security, disposition,
	                   flags | FILE_FLAG_BACKUP_SEMANTICS, templateFile);
}

BOOL WINAPI UxCopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists)
{
	return CopyFileA(UxMapPath(from).c_str(), UxMapPath(to).c_str(), failIfExists);
}

BOOL WINAPI UxCopyFileExA(LPCSTR from, LPCSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, LPBOOL cancel, DWORD flags)
{
	return CopyFileExA(UxMapPath(from).c_str(), UxMapPath(to).c_str(), progress, data, cancel, flags);
}

BOOL WINAPI UxCreateDirectoryA(LPCSTR path, LPVOID security)
{
	return CreateDirectoryA(UxMapPath(path).c_str(), (LPSECURITY_ATTRIBUTES)security);
}

BOOL WINAPI UxDeleteFileA(LPCSTR path)
{
	return DeleteFileA(UxMapPath(path).c_str());
}

HANDLE WINAPI UxFindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data)
{
	std::string pc = UxMapPath(pattern);
	// (The Xbox's FindFirstFile on a drive root lists it.)
	if (!pc.empty() && pc[pc.size() - 1] == '\\')
		pc += "*";
	HANDLE find = FindFirstFileA(pc.c_str(), data);
	// FATX has no . and ..: skip them.
	while (find != INVALID_HANDLE_VALUE && (strcmp(data->cFileName, ".") == 0 || strcmp(data->cFileName, "..") == 0))
	{
		if (!FindNextFileA(find, data))
		{
			FindClose(find);
			SetLastError(ERROR_FILE_NOT_FOUND);
			return INVALID_HANDLE_VALUE;
		}
	}
	return find;
}

BOOL WINAPI UxGetDiskFreeSpaceExA(LPCSTR path, PULARGE_INTEGER freeToCaller, PULARGE_INTEGER total,
                                  PULARGE_INTEGER totalFree)
{
	std::string pc = UxMapPath(path);
	if (!pc.empty() && pc[pc.size() - 1] != '\\')
		pc += "\\";
	return GetDiskFreeSpaceExA(pc.c_str(), freeToCaller, total, totalFree);
}

DWORD WINAPI UxGetFileAttributesA(LPCSTR path)
{
	return GetFileAttributesA(UxMapPath(path).c_str());
}

BOOL WINAPI UxGetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
	return GetFileAttributesExA(UxMapPath(path).c_str(), level, info);
}

BOOL WINAPI UxGetVolumeInformationA(LPCSTR root, LPSTR volumeName, DWORD volumeNameSize, LPDWORD serial,
                                    LPDWORD maxComponentLength, LPDWORD flags, LPSTR fileSystemName,
                                    DWORD fileSystemNameSize)
{
	char letter = root != NULL && root[0] != 0 ? (char)toupper((unsigned char)root[0]) : 'D';
	if (GetFileAttributesA(UxDriveFolder(letter).c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		SetLastError(ERROR_PATH_NOT_FOUND);
		return FALSE;
	}
	if (volumeName != NULL && volumeNameSize > 0)
		volumeName[0] = 0;
	if (serial != NULL)
		*serial = 0x55580000 | (BYTE)letter;
	if (maxComponentLength != NULL)
		*maxComponentLength = 42;
	if (flags != NULL)
		*flags = 0;
	if (fileSystemName != NULL && fileSystemNameSize > 0)
		lstrcpynA(fileSystemName, letter == 'D' ? "UDF" : "FATX", fileSystemNameSize);
	return TRUE;
}

BOOL WINAPI UxMoveFileA(LPCSTR from, LPCSTR to)
{
	return MoveFileExA(UxMapPath(from).c_str(), UxMapPath(to).c_str(), MOVEFILE_COPY_ALLOWED);
}

BOOL WINAPI UxMoveFileExA(LPCSTR from, LPCSTR to, DWORD flags)
{
	return MoveFileExA(UxMapPath(from).c_str(), to != NULL ? UxMapPath(to).c_str() : NULL,
	                   flags | MOVEFILE_COPY_ALLOWED);
}

BOOL WINAPI UxMoveFileWithProgressA(LPCSTR from, LPCSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, DWORD flags)
{
	return MoveFileWithProgressA(UxMapPath(from).c_str(), to != NULL ? UxMapPath(to).c_str() : NULL, progress, data,
	                             flags | MOVEFILE_COPY_ALLOWED);
}

BOOL WINAPI UxRemoveDirectoryA(LPCSTR path)
{
	return RemoveDirectoryA(UxMapPath(path).c_str());
}

BOOL WINAPI UxSetFileAttributesA(LPCSTR path, DWORD attributes)
{
	return SetFileAttributesA(UxMapPath(path).c_str(), attributes);
}

FILE *UxFopen(const char *path, const char *mode)
{
	return ::fopen(UxMapPath(path).c_str(), mode);
}

int UxAccess(const char *path, int mode)
{
	return _access(UxMapPath(path).c_str(), mode);
}

int UxStat(const char *path, struct stat *st)
{
	return ::stat(UxMapPath(path).c_str(), st);
}

int UxRename(const char *from, const char *to)
{
	return ::rename(UxMapPath(from).c_str(), UxMapPath(to).c_str());
}

}   // extern "C"
