// UnleashX build shim, force-included into every UnrarXLib file: the types
// XBMC's build got elsewhere.
#pragma once
#include <xtl.h>
typedef __int64 int64_t;
#include <memory>

// RXDK's clang (Makefile.rxdk) is neither MSVC nor GCC to unrar's checks:
// give it the native 64-bit Int64 the other two get (int64.hpp) and the
// calling-convention macro its non-MSVC Win32 branch wants (os.hpp).
#if !defined(_MSC_VER) && !defined(__GNUC__) && !defined(__BORLANDC__)
#define NATIVE_INT64
typedef long long Int64;
#ifndef _USERENTRY
#define _USERENTRY __cdecl
#endif
#endif
