// The file functions the Xbox code calls, taking Xbox paths (C:\..., E:\...,
// \Device\...) to folders on the PC: xbox/win32/XFiles.cpp. Included by
// src/common/PlatformWin32.h, and forced into the third-party code that
// opens files (zip_utils, unrar), after <windows.h>.
#pragma once

#include <windows.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif
HANDLE WINAPI UxCreateFileA(LPCSTR path, DWORD access, DWORD share, LPVOID security, DWORD disposition,
                            DWORD flags, HANDLE templateFile);
BOOL WINAPI UxCopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists);
BOOL WINAPI UxCopyFileExA(LPCSTR from, LPCSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, LPBOOL cancel,
                          DWORD flags);
BOOL WINAPI UxCreateDirectoryA(LPCSTR path, LPVOID security);
BOOL WINAPI UxDeleteFileA(LPCSTR path);
HANDLE WINAPI UxFindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data);
BOOL WINAPI UxGetDiskFreeSpaceExA(LPCSTR path, PULARGE_INTEGER freeToCaller, PULARGE_INTEGER total,
                                  PULARGE_INTEGER totalFree);
DWORD WINAPI UxGetFileAttributesA(LPCSTR path);
BOOL WINAPI UxGetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID info);
BOOL WINAPI UxGetVolumeInformationA(LPCSTR root, LPSTR volumeName, DWORD volumeNameSize, LPDWORD serial,
                                    LPDWORD maxComponentLength, LPDWORD flags, LPSTR fileSystemName,
                                    DWORD fileSystemNameSize);
BOOL WINAPI UxMoveFileA(LPCSTR from, LPCSTR to);
BOOL WINAPI UxMoveFileExA(LPCSTR from, LPCSTR to, DWORD flags);
BOOL WINAPI UxMoveFileWithProgressA(LPCSTR from, LPCSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, DWORD flags);
BOOL WINAPI UxRemoveDirectoryA(LPCSTR path);
BOOL WINAPI UxSetFileAttributesA(LPCSTR path, DWORD attributes);
FILE *UxFopen(const char *path, const char *mode);
// GetTickCount to the millisecond (xbox/win32/WinGfx.cpp).
DWORD WINAPI UxGetTickCount(void);
// The window's new size when it was resized: the dash then draws at it
// (CUnleashX::ChangeResolution). 0 when there's nothing to do.
int UxGfxTakeResize(unsigned *width, unsigned *height);
int UxRename(const char *from, const char *to);
#ifdef __cplusplus
}
#endif

// (xbox/win32's own files that see this header through the dash's take PC
// paths: they define UX_NO_FILEMAP.)
#ifndef UX_NO_FILEMAP
#define CreateFileA UxCreateFileA
#define CopyFileA UxCopyFileA
#define CopyFileExA UxCopyFileExA
#define CreateDirectoryA UxCreateDirectoryA
#define DeleteFileA UxDeleteFileA
#define FindFirstFileA UxFindFirstFileA
#define GetDiskFreeSpaceExA UxGetDiskFreeSpaceExA
#define GetFileAttributesA UxGetFileAttributesA
#define GetFileAttributesExA UxGetFileAttributesExA
#define GetVolumeInformationA UxGetVolumeInformationA
#define MoveFileA UxMoveFileA
#define MoveFileExA UxMoveFileExA
#define MoveFileWithProgressA UxMoveFileWithProgressA
#define RemoveDirectoryA UxRemoveDirectoryA
#define SetFileAttributesA UxSetFileAttributesA
#define fopen(path, mode) UxFopen(path, mode)
#define rename(from, to) UxRename(from, to)
#define GetTickCount UxGetTickCount
#endif

// unrar's POSIX calls (UX_FILEMAP_POSIX: not for the dash's code, where
// these names are other things too).
#ifdef UX_FILEMAP_POSIX
#include <sys/stat.h>
#ifdef __cplusplus
extern "C" {
#endif
int UxAccess(const char *path, int mode);
int UxStat(const char *path, struct stat *st);
#ifdef __cplusplus
}
#endif
#define access(path, mode) UxAccess(path, mode)
#define stat(path, st) UxStat(path, st)
#endif
