// XNet (the Xbox network library's additions to Winsock) for the Windows
// build: the XNet part of RXDK's winsockx.h (GPL-3.0-or-later, Team
// Resurgent; portions (c) Microsoft), over the real Winsock 2.
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <pshpack4.h>
#ifdef __cplusplus
extern "C" {
#endif

// XBox Secure Network Library -----------------------------------------------------------

#include <pshpack1.h>

//
// XNetStartup is called to load the XBox Secure Network Library.  It takes an optional
// pointer to a parameter structure.  To initialize the library with the default set
// of parameters, simply pass NULL for this argument.  To initialize the library with
// a different set of parameters, place an XNetStartupParams on the stack, zero it out,
// set the cfgSizeOfStruct to sizeof(XNetStartupParams), set any of the parameters you
// want to configure (leaving the remaining ones zeroed), and pass a pointer to this
// structure to XNetStartup.
//
// By default the XBox Secure Network Library operates in secure mode, which means that
// communication to untrusted hosts (such as a PC) is disallowed.  However, the devkit
// version of the XBox Secure Network Library (xnet.lib and xnetd.lib) allow you to 
// bypass this security by specifying the XNET_STARTUP_BYPASS_SECURITY flag in the 
// cfgFlags parameter.  Here is an example of how to do this:
// 
//      XNetStartupParams xnsp;
//      memset(&xnsp, 0, sizeof(xnsp));
//      xnsp.cfgSizeOfStruct = sizeof(XNetStartupParams);
//      xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
//      INT err = XNetStartup(&xnsp);
//
// Attempting to bypass security when linking with xnets.lib or xnetsd.lib does not
// return an error, it is simply ignored.  Attempts to send or receive packets from
// untrusted hosts will fail.
//

typedef struct {

    BYTE        cfgSizeOfStruct;

        // Must be set to sizeof(XNetStartupParams).  There is no default.

    BYTE        cfgFlags;

        // One or more of the following flags OR'd together:

        #define XNET_STARTUP_BYPASS_SECURITY            0x01
        #define XNET_STARTUP_ALLOW_AUTOIP                0x04
            // This devkit-only flag tells the XNet stack to allow insecure
            // communication to untrusted hosts (such as a PC).  This flag
            // is silently ignored by the secure versions of the library.

        #define XNET_STARTUP_BYPASS_DHCP                0x02
            // This devkit-only flag tells the XNet stack to skip searching for
            // for a DHCP server and use auto-ip only to acquire an IP address.
            // This will save several seconds when starting up if you know
            // that there is no DHCP server configured.  This flag is silently
            // ignored by the secure versions of the library.

        // The default is 0 (no flags specified).

    BYTE        cfgPrivatePoolSizeInPages;

        // Specifies the size of the pre-allocated private memory pool used by
        // XNet for the following situations:
        //
        //      - Responding to ARP/DHCP/ICMP messages
        //      - Responding to certain TCP control messages
        //      - Allocating incoming TCP connection request sockets
        //      - Buffering outgoing data until it is transmitted (UDP) or
        //        until it is acknowledged (TCP)
        //      - Buffering incoming data on a socket that does not have a
        //        sufficiently large overlapped read pending
        //
        // The reason for using a private pool instead of the normal system
        // pool is because we want to have completely deterministic memory 
        // behavior.  That is, all memory allocation occurs only when an API
        // is called.  No system memory allocation happens asynchronously in
        // response to an incoming packet.
        //
        // Note that this parameter is in units of pages (4096 bytes per page). 
        //
        // The default is 12 pages (48K).

    BYTE        cfgEnetReceiveQueueLength;
        
        // The length of the Ethernet receive queue in number of packets.  Each 
        // packet takes 2KB of physically contiguous memory.
        //
        // The default is 8 packets (16K).

    BYTE        cfgIpFragMaxSimultaneous;

        // The maximum number of IP datagrams that can be in the process of reassembly
        // at the same time.
        //
        // The default is 4 packets.

    BYTE        cfgIpFragMaxPacketDiv256;

        // The maximum size of an IP datagram (including header) that can be reassembled.
        // Be careful when setting this parameter to a large value as it opens up 
        // a potential denial-of-service attack by consuming large amounts of memory
        // in the fixed-size private pool.
        //
        // Note that this parameter is in units of 256-bytes each.
        //
        // The default is 8 units (2048 bytes).

    BYTE        cfgSockMaxSockets;

        // The maximum number of sockets that can be opened at once, including those 
        // sockets created as a result of incoming connection requests.  Remember
        // that a TCP socket may not be closed immediately after closesocket is
        // called depending on the linger options in place (by default a TCP socket
        // will linger).
        //
        // The default is 64 sockets.
        
    BYTE        cfgSockDefaultRecvBufsizeInK;

        // The default receive buffer size for a socket, in units of K (1024 bytes).
        //
        // The default is 16 units (16K).

    BYTE        cfgSockDefaultSendBufsizeInK;

        // The default send buffer size for a socket, in units of K (1024 bytes).
        //
        // The default is 16 units (16K).

    BYTE        cfgKeyRegMax;

        // The maximum number of XNKID / XNKEY pairs that can be registered at the 
        // same time by calling XNetRegisterKey.
        //
        // The default is 4 key pair registrations.

    BYTE        cfgSecRegMax;

        // The maximum number of security associations that can be registered at the
        // same time.  Security associations are created for each unique XNADDR / XNKID
        // pair passed to XNetXnAddrToInAddr.  Security associations are also implicitly
        // created for each secure host that establishes an incoming connection
        // with this host on a given registered XNKID.  Note that there will only be
        // one security association between a pair of hosts on a given XNKID no matter
        // how many sockets are actively communicating on that secure connection.
        //
        // The default is 32 security associations.

     BYTE       cfgQosDataLimitDiv4;

        // The maximum amount of Qos data that can be supplied to a call to XNetQosListen
        // or returned in the result set of a call to XNetQosXnAddr or XNetQosServer.
        //
        // The default is 128 (512 bytes).

} XNetStartupParams;

typedef struct {
    IN_ADDR     ina;                            // IP address (zero if not static/DHCP)
    IN_ADDR     inaOnline;                      // Online IP address (zero if not online)
    WORD        wPortOnline;                    // Online port
    BYTE        abEnet[6];                      // Ethernet MAC address
    BYTE        abOnline[20];                   // Online identification
} XNADDR;

typedef struct {
    BYTE        ab[8];                          // xbox to xbox key identifier
} XNKID;

#define XNET_XNKID_MASK             0xF0        // Mask of flag bits in first byte of XNKID
#define XNET_XNKID_SYSTEM_LINK      0x00        // Peer to peer system link session
#define XNET_XNKID_ONLINE_PEER      0x80        // Peer to peer online session
#define XNET_XNKID_ONLINE_TITLESERVER   0xE0    // Client to title server online session
#define XNET_XNKID_ONLINE_SERVER    0xC0        // Client to server online session

#define XNetXnKidIsSystemLink(pxnkid)           (((pxnkid)->ab[0] & 0xC0) == XNET_XNKID_SYSTEM_LINK)
#define XNetXnKidIsOnlinePeer(pxnkid)           (((pxnkid)->ab[0] & 0xC0) == XNET_XNKID_ONLINE_PEER)
#define XNetXnKidIsOnlineServer(pxnkid)         (((pxnkid)->ab[0] & 0xC0) == XNET_XNKID_ONLINE_SERVER)
#define XNetXnKidIsOnlineTitleServer(pxnkid)    (((pxnkid)->ab[0] & 0xE0) == XNET_XNKID_ONLINE_TITLESERVER)

typedef struct {
    BYTE        ab[16];                         // xbox to xbox key exchange key
} XNKEY;


typedef struct {
    INT         iStatus;                        // WSAEINPROGRESS if pending; 0 if success; error if failed
    UINT        cina;                           // Count of IP addresses for the given host
    IN_ADDR     aina[8];                        // Vector of IP addresses for the given host
} XNDNS;

// RXDK 5849 uplift: XDK-5849 XNQOSINFO layout (the leak's bDone/cXmit/dwPingTime shape differs).
typedef struct {
    BYTE        bFlags;                         // See XNET_XNQOSINFO_* below
    BYTE        bReserved;                      // Reserved
    WORD        cProbesXmit;                    // Count of Qos probes transmitted
    WORD        cProbesRecv;                    // Count of Qos probes successfully received
    WORD        cbData;                         // Size of Qos data supplied by target (may be zero)
    BYTE *      pbData;                         // Qos data supplied by target (may be NULL)
    WORD        wRttMinInMsecs;                 // Minimum round-trip time in milliseconds
    WORD        wRttMedInMsecs;                 // Median round-trip time in milliseconds
    DWORD       dwUpBitsPerSec;                 // Upstream bandwidth in bits per second
    DWORD       dwDnBitsPerSec;                 // Downstream bandwidth in bits per second
} XNQOSINFO;

typedef XNADDR TSADDR;                          // 5849: Title-Server address (== XNADDR)

#define XNET_XNQOSINFO_COMPLETE         0x01
#define XNET_XNQOSINFO_TARGET_CONTACTED 0x02
#define XNET_XNQOSINFO_TARGET_DISABLED  0x04
#define XNET_XNQOSINFO_DATA_RECEIVED    0x08
#define XNET_XNQOSINFO_PARTIAL_COMPLETE 0x10

typedef struct {
    UINT        cxnqos;                         // Count of items in axnqosinfo[] array
    UINT        cxnqosPending;                  // Count of items still pending
    XNQOSINFO   axnqosinfo[1];                  // Vector of Qos results
} XNQOS;

#include <poppack.h>

INT   WSAAPI XNetStartup(const XNetStartupParams * pxnsp);
INT   WSAAPI XNetCleanup();

INT   WSAAPI XNetRandom(BYTE * pb, UINT cb);

INT   WSAAPI XNetCreateKey(XNKID * pxnkid, XNKEY * pxnkey);
INT   WSAAPI XNetRegisterKey(const XNKID * pxnkid, const XNKEY * pxnkey);
INT   WSAAPI XNetUnregisterKey(const XNKID * pxnkid);

INT   WSAAPI XNetXnAddrToInAddr(const XNADDR * pxna, const XNKID * pxnkid, IN_ADDR * pina);
INT   WSAAPI XNetServerToInAddr(const IN_ADDR ina, DWORD dwServiceId, IN_ADDR * pina);
INT   WSAAPI XNetInAddrToXnAddr(const IN_ADDR ina, XNADDR * pxna, XNKID * pxnkid);
INT   WSAAPI XNetInAddrToString(const IN_ADDR ina, char * pchBuf, INT cchBuf);
INT   WSAAPI XNetUnregisterInAddr(const IN_ADDR ina);

INT   WSAAPI XNetDnsLookup(const char * pszHost, WSAEVENT hEvent, XNDNS ** ppxndns);
INT   WSAAPI XNetDnsRelease(XNDNS * pxndns);

INT   WSAAPI XNetQosListen(const XNKID * pxnkid, const BYTE * pb, UINT cb, DWORD dwBitsPerSec, DWORD dwFlags);
INT   WSAAPI XNetQosXnAddr(UINT cxnqos, const XNADDR * apxna[], const XNKID * apxnkid[], const XNKEY * apxnkey[], DWORD dwFlags, WSAEVENT hEvent, XNQOS ** ppxnqos);
INT   WSAAPI XNetQosServer(UINT cxnqos, const IN_ADDR aina[], const DWORD adwServiceId[], DWORD dwFlags, WSAEVENT hEvent, XNQOS ** ppxnqos);
INT   WSAAPI XNetQosRelease(XNQOS * pxnqos);

// 5849 renumbered these to proper bit flags. The leak's ENABLE=0x00 is unusable
// with the `dwFlags & XNET_QOS_LISTEN_ENABLE` bit test libxnet's ipqos.cpp does
// (always false), and the leak numbering made a 5849 title's SET_BITSPERSEC (0x08)
// fall outside the validation mask and get rejected. libxnet uses these by name,
// so adopting the 5849 values fixes both without touching the logic.
#define XNET_QOS_LISTEN_ENABLE          0x01    // Responds to queries on the given XNKID
#define XNET_QOS_LISTEN_DISABLE         0x02    // Rejects queries on the given XNKID
#define XNET_QOS_LISTEN_SET_DATA        0x04    // Sets the block of data to send back to queriers
#define XNET_QOS_LISTEN_SET_BITSPERSEC  0x08    // Sets max bandwidth that query reponses may consume
#define XNET_QOS_XNADDR_RESERVED        0x00    // No flags currently defined
#define XNET_QOS_SERVER_RESERVED        0x00    // No flags currently defined

// RXDK 5849 uplift: the XDK-5849 connect + QoS-lookup API (absent from the leak public header).
INT   WSAAPI XNetTsAddrToInAddr(const TSADDR * ptsa, DWORD dwServiceId, const XNKID * pxnkid, IN_ADDR * pina);
INT   WSAAPI XNetXnAddrToMachineId(const XNADDR * pxnaddr, ULONGLONG * pqwMachineId);
INT   WSAAPI XNetInAddrToServer(const IN_ADDR ina, IN_ADDR * pina);

INT   WSAAPI XNetConnect(const IN_ADDR ina);
DWORD WSAAPI XNetGetConnectStatus(const IN_ADDR ina);
#define XNET_CONNECT_STATUS_IDLE            0x0000
#define XNET_CONNECT_STATUS_PENDING         0x0001
#define XNET_CONNECT_STATUS_CONNECTED       0x0002
#define XNET_CONNECT_STATUS_LOST            0x0003

INT   WSAAPI XNetQosLookup(UINT cxna, const XNADDR * apxna[], const XNKID * apxnkid[], const XNKEY * apxnkey[], UINT cina, const IN_ADDR aina[], const DWORD adwServiceId[], UINT cProbes, DWORD dwBitsPerSec, DWORD dwFlags, WSAEVENT hEvent, XNQOS ** ppxnqos);
INT   WSAAPI XNetQosServiceLookup(DWORD dwFlags, WSAEVENT hEvent, XNQOS ** ppxnqos);
#define XNET_QOS_LISTEN_RELEASE             0x10
#define XNET_QOS_LOOKUP_RESERVED            0x00
#define XNET_QOS_SERVICE_LOOKUP_RESERVED    0x00

DWORD WSAAPI XNetGetTitleXnAddr(XNADDR * pxna);
DWORD WSAAPI XNetGetDebugXnAddr(XNADDR * pxna);

#define XNET_GET_XNADDR_PENDING         0x00    // Address acquisition is not yet complete
#define XNET_GET_XNADDR_NONE            0x01    // XNet is uninitialized or no debugger found
#define XNET_GET_XNADDR_ETHERNET        0x02    // Host has ethernet address (no IP address)
#define XNET_GET_XNADDR_PPPOE           0x0010  // Host has PPPoE assigned IP address
#define XNET_GET_XNADDR_STATIC          0x04    // Host has static IP address
#define XNET_GET_XNADDR_DHCP            0x08    // Host has dynamic IP address via DHCP
#define XNET_GET_XNADDR_AUTO            0x10    // Host has auto IP address in 169.254/16
#define XNET_GET_XNADDR_GATEWAY         0x20    // Host has one or more gateways configured
#define XNET_GET_XNADDR_DNS             0x40    // Host has one or more DNS servers configured
#define XNET_GET_XNADDR_TROUBLESHOOT    0x8000  // Network configuration requires troubleshooting
#define XNET_GET_XNADDR_ONLINE          0x80    // Host is currently connected to online service

DWORD WSAAPI XNetGetEthernetLinkStatus();

#define XNET_ETHERNET_LINK_ACTIVE       0x01    // Ethernet cable is connected and active
#define XNET_ETHERNET_LINK_100MBPS      0x02    // Ethernet link is set to 100 Mbps
#define XNET_ETHERNET_LINK_10MBPS       0x04    // Ethernet link is set to 10 Mbps
#define XNET_ETHERNET_LINK_FULL_DUPLEX  0x08    // Ethernet link is in full duplex mode
#define XNET_ETHERNET_LINK_HALF_DUPLEX  0x10    // Ethernet link is in half duplex mode

// RXDK: XDK-5849 broadcast title-version mismatch detection.
DWORD WSAAPI XNetGetBroadcastVersionStatus(BOOL fReset);

#define XNET_BROADCAST_VERSION_OLDER        0x0001  // Got broadcast packet(s) from incompatible older version of title
#define XNET_BROADCAST_VERSION_NEWER        0x0002  // Got broadcast packet(s) from incompatible newer version of title

//
// Since our socket handles are not file handles, apps can NOT call CancelIO API to cancel
#ifdef __cplusplus
}
#endif
#include <poppack.h>
