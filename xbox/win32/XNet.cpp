// XNet for the Windows build: sockets are Winsock's already; this reports the
// PC's own address and link, and looks names up with getaddrinfo. The
// Xbox's network settings are kept (in memory) but Windows' are used.
// (Windows' file functions here take PC paths.)
#define UX_NO_FILEMAP
#include <xtl.h>

#include <string.h>
#include <stdio.h>

#include "../../src/net/XNet.h"

static XNetConfigParams s_config;
static bool s_configLoaded;

// The PC's IPv4 address: the one a packet to the internet would leave from
// (a UDP "connection" sends nothing), else the first of its name's that isn't
// the loopback.
static bool LocalAddress(IN_ADDR *address)
{
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s != INVALID_SOCKET)
	{
		struct sockaddr_in to;
		memset(&to, 0, sizeof(to));
		to.sin_family = AF_INET;
		to.sin_port = htons(53);
		to.sin_addr.s_addr = htonl(0x08080808);
		struct sockaddr_in local;
		int length = sizeof(local);
		bool found = connect(s, (struct sockaddr *)&to, sizeof(to)) == 0 &&
		             getsockname(s, (struct sockaddr *)&local, &length) == 0 && local.sin_addr.s_addr != 0;
		closesocket(s);
		if (found)
		{
			*address = local.sin_addr;
			return true;
		}
	}
	char name[256];
	if (gethostname(name, sizeof(name)) != 0)
		return false;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	struct addrinfo *list = NULL;
	if (getaddrinfo(name, NULL, &hints, &list) != 0)
		return false;
	bool found = false;
	for (struct addrinfo *it = list; it != NULL && !found; it = it->ai_next)
	{
		IN_ADDR a = ((struct sockaddr_in *)it->ai_addr)->sin_addr;
		if ((ntohl(a.s_addr) >> 24) != 127)
		{
			*address = a;
			found = true;
		}
	}
	freeaddrinfo(list);
	return found;
}

extern "C" {

INT WSAAPI XNetStartup(const XNetStartupParams *)
{
	return 0;
}

INT WSAAPI XNetCleanup()
{
	return 0;
}

INT WSAAPI XNetRandom(BYTE *buffer, UINT size)
{
	static unsigned int s_seed;
	if (s_seed == 0)
		s_seed = GetTickCount() ^ (GetCurrentProcessId() << 16);
	for (UINT i = 0; i < size; i++)
	{
		s_seed = s_seed * 1103515245u + 12345u;
		buffer[i] = (BYTE)(s_seed >> 16);
	}
	return 0;
}

INT WSAAPI XNetInAddrToString(const IN_ADDR ina, char *text, INT size)
{
	const BYTE *b = (const BYTE *)&ina.s_addr;
	_snprintf(text, size, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
	if (size > 0)
		text[size - 1] = 0;
	return 0;
}

// Looked up right away (the Xbox does it in the background and signals).
INT WSAAPI XNetDnsLookup(const char *host, WSAEVENT event, XNDNS **result)
{
	XNDNS *dns = new XNDNS;
	memset(dns, 0, sizeof(*dns));
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	struct addrinfo *list = NULL;
	if (getaddrinfo(host, NULL, &hints, &list) != 0)
	{
		dns->iStatus = WSAHOST_NOT_FOUND;
	}
	else
	{
		for (struct addrinfo *it = list; it != NULL && dns->cina < 8; it = it->ai_next)
			dns->aina[dns->cina++] = ((struct sockaddr_in *)it->ai_addr)->sin_addr;
		freeaddrinfo(list);
		dns->iStatus = dns->cina > 0 ? 0 : WSAHOST_NOT_FOUND;
	}
	*result = dns;
	if (event != NULL)
		SetEvent(event);
	return 0;
}

INT WSAAPI XNetDnsRelease(XNDNS *dns)
{
	delete dns;
	return 0;
}

DWORD WSAAPI XNetGetTitleXnAddr(XNADDR *address)
{
	memset(address, 0, sizeof(*address));
	if (!LocalAddress(&address->ina))
		return XNET_GET_XNADDR_ETHERNET;
	return XNET_GET_XNADDR_DHCP | XNET_GET_XNADDR_GATEWAY | XNET_GET_XNADDR_DNS;
}

DWORD WSAAPI XNetGetEthernetLinkStatus()
{
	return XNET_ETHERNET_LINK_ACTIVE | XNET_ETHERNET_LINK_100MBPS | XNET_ETHERNET_LINK_FULL_DUPLEX;
}

DWORD WSAAPI XNetGetBroadcastVersionStatus(BOOL)
{
	return 0;
}

int __stdcall XNetLoadConfigParams(XNetConfigParams *params)
{
	if (!s_configLoaded)
	{
		memset(&s_config, 0, sizeof(s_config));
		s_configLoaded = true;
	}
	*params = s_config;
	return 0;
}

int __stdcall XNetSaveConfigParams(const XNetConfigParams *params)
{
	s_config = *params;
	s_configLoaded = true;
	return 0;
}

}   // extern "C"
