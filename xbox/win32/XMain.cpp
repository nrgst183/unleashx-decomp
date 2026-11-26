// The Windows build's entry point: sets up the Xbox's drives as folders
// (XFiles.cpp), puts UnleashX's own files where the Xbox would have them
// (E:\Apps\UnleashX, from the "disc" folder next to the program, without
// overwriting anything), maps D: there as a launch would, and runs the
// dash's main (src/app/Main.cpp, renamed by PlatformWin32.h).
#include <xtl.h>
#include <mmsystem.h>

#include <string>

#include "UxWin.h"
#include "XFiles.h"

extern "C" int UxXboxMain();

// Copies a folder's files and folders that aren't at the destination yet.
static void CopyMissing(const std::string &from, const std::string &to)
{
	// (The folder and the ones above it.)
	for (size_t i = 3; i <= to.size(); i++)
		if (i == to.size() || to[i] == '\\')
			CreateDirectoryA(to.substr(0, i).c_str(), NULL);
	WIN32_FIND_DATAA data;
	HANDLE find = FindFirstFileA((from + "\\*").c_str(), &data);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		std::string name = data.cFileName;
		if (name == "." || name == "..")
			continue;
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			CopyMissing(from + "\\" + name, to + "\\" + name);
		else
			CopyFileA((from + "\\" + name).c_str(), (to + "\\" + name).c_str(), TRUE);
	} while (FindNextFileA(find, &data));
	FindClose(find);
}

// ---- crashes: logged (where, and the return addresses on the stack) ----

static LPTOP_LEVEL_EXCEPTION_FILTER s_dashFilter;

static void LogAddress(const char *what, DWORD address)
{
	HMODULE module = NULL;
	char name[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       (LPCSTR)(ULONG_PTR)address, &module) &&
	    module != NULL)
	{
		GetModuleFileNameA(module, name, sizeof(name));
		const char *slash = strrchr(name, '\\');
		UxLog("%s %08lX (%s+%lX)", what, (unsigned long)address, slash != NULL ? slash + 1 : name,
		      (unsigned long)(address - (DWORD)(ULONG_PTR)module));
	}
	else
	{
		UxLog("%s %08lX", what, (unsigned long)address);
	}
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *exception)
{
	static LONG s_inside;
	if (InterlockedExchange(&s_inside, 1) == 0)
	{
		const EXCEPTION_RECORD *record = exception->ExceptionRecord;
		UxLog("Crash: exception %08lX", (unsigned long)record->ExceptionCode);
		LogAddress("  at", (DWORD)(ULONG_PTR)record->ExceptionAddress);
		// The return addresses in the program on the stack (no frame
		// pointers to follow in an optimised build).
		const DWORD *sp = (const DWORD *)(ULONG_PTR)exception->ContextRecord->Esp;
		HMODULE self = GetModuleHandleA(NULL);
		const IMAGE_NT_HEADERS *nt =
		    (const IMAGE_NT_HEADERS *)((const BYTE *)self + ((const IMAGE_DOS_HEADER *)self)->e_lfanew);
		DWORD start = (DWORD)(ULONG_PTR)self, end = start + nt->OptionalHeader.SizeOfImage;
		int found = 0;
		for (int i = 0; i < 2048 && found < 16; i++)
		{
			MEMORY_BASIC_INFORMATION info;
			if (VirtualQuery(sp + i, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT)
				break;
			if (sp[i] >= start && sp[i] < end)
			{
				LogAddress("  called from", sp[i]);
				found++;
			}
		}
	}
	return s_dashFilter != NULL ? s_dashFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

extern "C" LPTOP_LEVEL_EXCEPTION_FILTER WINAPI UxSetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter)
{
	LPTOP_LEVEL_EXCEPTION_FILTER previous = s_dashFilter;
	s_dashFilter = filter;
	SetUnhandledExceptionFilter(CrashFilter);
	return previous;
}

static std::string ProgramFolder()
{
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, sizeof(path));
	char *slash = strrchr(path, '\\');
	if (slash != NULL)
		*slash = 0;
	return path;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	SetProcessDPIAware();
	timeBeginPeriod(1);

	// The dash's own folder, D: as when it's launched, and the title's
	// data folders (T: and U:, as XAPI maps them for title 9e115330).
	UxLog("UnleashX for Windows: drives in %s", UxXboxRoot());
	std::string dash = UxMapPath(UX_DASH_FOLDER);
	CopyMissing(ProgramFolder() + "\\disc", dash);
	UxSetDriveDevice('D', "\\Device\\Harddisk0\\Partition1\\Apps\\UnleashX");
	UxSetDriveDevice('T', "\\Device\\Harddisk0\\Partition1\\TDATA\\9e115330");
	UxSetDriveDevice('U', "\\Device\\Harddisk0\\Partition1\\UDATA\\9e115330");

	int result = UxXboxMain();
	UxLog("main returned %d", result);
	ExitProcess(result);
	return result;
}
