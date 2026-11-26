// The Windows build (UX_WIN32): what the Xbox code expects of the platform
// that Windows does differently. Included by Platform.h after <xtl.h>
// (xbox/win32/include). The definitions are in xbox/win32.
#pragma once

#include <stdio.h>
#include <wchar.h>

// What MSVC 7.1's CRT had.
#ifndef Int32x32To64
#define Int32x32To64(a, b) ((LONGLONG)(LONG)(a) * (LONGLONG)(LONG)(b))
#endif

// The Xbox's paths go to folders on the PC (xbox/win32/UxFileMap.h).
#include "UxFileMap.h"

// The Xbox program's entry point (src/app/Main.cpp), which WinMain calls.
#ifdef __cplusplus
extern "C" int UxXboxMain();
#else
int UxXboxMain();
#endif
#define main UxXboxMain

// The dash's crash handler, with the crash written to UnleashX.log first
// (xbox/win32/XMain.cpp).
#ifdef __cplusplus
extern "C"
#endif
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI UxSetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter);
#define SetUnhandledExceptionFilter UxSetUnhandledExceptionFilter

// Names the code uses that windows.h already has (the XDK's didn't).
#undef GetFreeSpace
#define CancelShutdown UxCancelShutdown

// The Xbox kernel's critical sections (Windows has the same structure).
#ifdef __cplusplus
extern "C" {
#endif
void WINAPI RtlInitializeCriticalSection(RTL_CRITICAL_SECTION *section);
void WINAPI RtlEnterCriticalSection(RTL_CRITICAL_SECTION *section);
void WINAPI RtlLeaveCriticalSection(RTL_CRITICAL_SECTION *section);
#ifdef __cplusplus
}
#endif

#ifndef MEM_NOZERO
#define MEM_NOZERO 0x800000   // (Xbox: don't clear the pages)
#endif
