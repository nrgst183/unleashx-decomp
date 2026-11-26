// RXDK has no <process.h>; its libc has _beginthreadex (over CreateThread).
// _endthreadex is xbox/RxdkCompat.cpp's.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uintptr_t _beginthreadex(void *security, unsigned stackSize, unsigned(__stdcall *start)(void *), void *arg,
                         unsigned initFlags, unsigned *threadId);
void _endthreadex(unsigned exitCode);
#ifdef __cplusplus
}
#endif
