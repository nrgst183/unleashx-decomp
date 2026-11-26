// What the XDK (MSVC 7.1's CRT and XAPI) had that the RXDK SDK spells
// differently or lacks. Included by Platform.h on the RXDK build only, after
// <xtl.h>; the definitions are in xbox/RxdkCompat.cpp unless RXDK's libc has
// them already (it has most of MSVC's CRT names, without declaring them).
#pragma once

// The XDK's <xtl.h> brought XNet (Winsock) and XGraphics along; RXDK's leaves
// them to the title.
#include <winsockx.h>
#include <xgraphics.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif
// In RXDK's libc (ms_crt_compat.c) but not in its headers.
char *_strdup(const char *s);
int _wtoi(const wchar_t *s);
double _wtof(const wchar_t *s);
long long _atoi64(const char *s);
// Not in RXDK: xbox/RxdkCompat.cpp.
int _wcsicoll(const wchar_t *a, const wchar_t *b);
#ifdef __cplusplus
}
#endif

// winnt.h (which RXDK's <xtl.h> skips: the kernel headers stand in for it).
#ifndef Int32x32To64
#define Int32x32To64(a, b) ((LONGLONG)(LONG)(a) * (LONGLONG)(LONG)(b))
#endif
