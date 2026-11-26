// The RXDK build's stand-ins for the MSVC 7.1 CRT functions UnleashX calls that
// RXDK's libc doesn't have (src/common/PlatformRxdk.h, xbox/rxdk/*.h). The XDK
// build gets them from libcmt.lib, so this file is empty there.
#ifdef UX_RXDK
#include <xtl.h>
#include <ctype.h>
#include <process.h>
#include <mbstring.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

// The XDK's CRT runs in the "C" locale with a single-byte code page, where the
// _mbs functions are the byte functions: case folds ASCII only, to lower case
// (like _stricmp).
static inline unsigned FoldByte(unsigned c)
{
	return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

extern "C" int _mbsnbicmp(const unsigned char *a, const unsigned char *b, size_t count)
{
	for (; count != 0; count--, a++, b++)
	{
		unsigned ca = FoldByte(*a), cb = FoldByte(*b);
		if (ca != cb)
			return ca < cb ? -1 : 1;
		if (ca == 0)
			break;
	}
	return 0;
}

extern "C" int _mbsicmp(const unsigned char *a, const unsigned char *b)
{
	return _mbsnbicmp(a, b, (size_t)-1);
}

extern "C" unsigned char *_mbschr(const unsigned char *s, unsigned int c)
{
	return (unsigned char *)strchr((const char *)s, (int)c);
}

extern "C" unsigned char *_mbsrchr(const unsigned char *s, unsigned int c)
{
	return (unsigned char *)strrchr((const char *)s, (int)c);
}

extern "C" unsigned char *_mbslwr(unsigned char *s)
{
	for (unsigned char *p = s; *p != 0; p++)
		*p = (unsigned char)FoldByte(*p);
	return s;
}

// Collation in the "C" locale is code-point order, so this is _wcsicmp: both
// strings folded to lower case.
extern "C" int _wcsicoll(const wchar_t *a, const wchar_t *b)
{
	for (;; a++, b++)
	{
		wint_t ca = towlower((wint_t)*a), cb = towlower((wint_t)*b);
		if (ca != cb)
			return ca < cb ? -1 : 1;
		if (ca == 0)
			return 0;
	}
}

// RXDK's _beginthreadex is CreateThread with no CRT state to tear down.
extern "C" void _endthreadex(unsigned exitCode)
{
	ExitThread(exitCode);
}

// MSVC 7.1's operator new returns NULL when memory runs out (a throwing one
// needed thrownew.obj, which the XDK build didn't link), and UnleashX checks
// for that everywhere; libc++'s throws std::bad_alloc, which nothing catches,
// so a failed allocation would end the dashboard instead of failing the one
// thing that asked. These replace the whole set RXDK's libcpp defines in one
// object (operator_new_delete.cpp, Itanium and MSVC-mangled names alike), so
// that object is never pulled in beside them. Built with -fcheck-new, so the
// compiler keeps the NULL checks after a new-expression.
#include <new>
#include <stdlib.h>

void *operator new(size_t size)
{
	return malloc(size != 0 ? size : 1);
}
void *operator new[](size_t size)
{
	return malloc(size != 0 ? size : 1);
}
void operator delete(void *p) noexcept
{
	free(p);
}
void operator delete[](void *p) noexcept
{
	free(p);
}
void operator delete(void *p, size_t) noexcept
{
	free(p);
}
void operator delete[](void *p, size_t) noexcept
{
	free(p);
}
extern "C++" {
void *UxMsvcNew(size_t size) asm("??2@YAPAXI@Z");
void *UxMsvcNew(size_t size)
{
	return malloc(size != 0 ? size : 1);
}
void *UxMsvcNewNothrow(size_t size, const void *) asm("??2@YAPAXIABUnothrow_t@std@@@Z");
void *UxMsvcNewNothrow(size_t size, const void *)
{
	return malloc(size != 0 ? size : 1);
}
void *UxMsvcNewArray(size_t size) asm("??_U@YAPAXI@Z");
void *UxMsvcNewArray(size_t size)
{
	return malloc(size != 0 ? size : 1);
}
void UxMsvcDelete(void *p) noexcept asm("??3@YAXPAX@Z");
void UxMsvcDelete(void *p) noexcept
{
	free(p);
}
void UxMsvcDeleteArray(void *p) noexcept asm("??_V@YAXPAX@Z");
void UxMsvcDeleteArray(void *p) noexcept
{
	free(p);
}
}
#endif
