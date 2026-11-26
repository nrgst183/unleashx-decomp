// RXDK has no <mbstring.h>. UnleashX's strings are single-byte (the XDK's
// multibyte functions ran in the "C" locale, where they're the plain byte
// functions), so these are the byte versions: xbox/RxdkCompat.cpp.
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int _mbsicmp(const unsigned char *a, const unsigned char *b);
int _mbsnbicmp(const unsigned char *a, const unsigned char *b, size_t count);
unsigned char *_mbschr(const unsigned char *s, unsigned int c);
unsigned char *_mbsrchr(const unsigned char *s, unsigned int c);
unsigned char *_mbslwr(unsigned char *s);
#ifdef __cplusplus
}
#endif
