// <xtl.h> for the Windows build of UnleashX (xbox/win32): the real Windows
// headers, then the Xbox APIs on top of them (XAPI and XInput, Direct3D 8
// and D3DX 8 as on the Xbox, XNet, XGraphics). The functions behind them are
// in xbox/win32/*.cpp. The kernel calls are declared by
// src/common/XboxKernel.h, as on the XDK build.
#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmreg.h>

#include "xbox.h"
#include "xkbd.h"
#include "d3d8.h"
#include "d3dx8.h"
#include "winsockx.h"
#include "xgraphics.h"
