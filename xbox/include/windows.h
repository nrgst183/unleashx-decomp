// The third-party sources include <windows.h>; on the Xbox that's <xtl.h>,
// plus the few Win32 calls XAPI doesn't have that zip_utils uses.
#pragma once
#include <xtl.h>

// winnt.h names zip.cpp uses that RXDK's <xtl.h> doesn't bring (the XDK's did).
#ifndef Int32x32To64
#define Int32x32To64(a, b) ((LONGLONG)(LONG)(a) * (LONGLONG)(LONG)(b))
#endif
#ifndef IMAGE_DOS_SIGNATURE
#define IMAGE_DOS_SIGNATURE 0x5A4D      // MZ
#define IMAGE_OS2_SIGNATURE 0x454E      // NE
#define IMAGE_OS2_SIGNATURE_LE 0x454C   // LE
#define IMAGE_NT_SIGNATURE 0x00004550   // PE00
#endif

// zip.cpp seeds its encryption salt with this; there are no windows.
static void *GetDesktopWindow()
{
	return 0;
}

// No file mappings: zipping into memory fails (ZR_NOALLOC). UnleashX only
// zips to files.
#define FILE_MAP_ALL_ACCESS 0xF001F
static HANDLE CreateFileMapping(HANDLE, void *, DWORD, DWORD, DWORD, const char *)
{
	return NULL;
}
static void *MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, DWORD)
{
	return NULL;
}
static BOOL UnmapViewOfFile(const void *)
{
	return FALSE;
}
