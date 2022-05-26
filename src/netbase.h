// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NETBASE_H
#define BITCOIN_NETBASE_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "compat.h"
#include "netaddress.h"
#include "serialize.h"
#include "util/sock.h"

#include <stdint.h>
#include <string>
#include <vector>

extern int nConnectTimeout;
extern bool fNameLookup;

//! -timeout default
static const int DEFAULT_CONNECT_TIMEOUT = 5000;
//! -dns default
static const int DEFAULT_NAME_LOOKUP = true;

class proxyType
{
public:
    proxyType(): randomize_credentials(false) {}
    proxyType(const CService &_proxy, bool _randomize_credentials=false): proxy(_proxy), randomize_credentials(_randomize_credentials) {}

    bool IsValid() const { return proxy.IsValid(); }

    CService proxy;
    bool randomize_credentials;
};

enum Network ParseNetwork(std::string net);
std::string GetNetworkName(enum Network net);
bool SetProxy(enum Network net, const proxyType &addrProxy);
bool GetProxy(enum Network net, proxyType &proxyInfoOut);
bool IsProxy(const CNetAddr &addr);
bool SetNameProxy(const proxyType &addrProxy);
bool HaveNameProxy();
bool LookupHost(const char *pszName, std::vector<CNetAddr>& vIP, unsigned int nMaxSolutions, bool fAllowLookup);
bool LookupHost(const char *pszName, CNetAddr& addr, bool fAllowLookup);
bool Lookup(const char *pszName, CService& addr, int portDefault, bool fAllowLookup);
bool Lookup(const char *pszName, std::vector<CService>& vAddr, int portDefault, bool fAllowLookup, unsigned int nMaxSolutions);
CService LookupNumeric(const char *pszName, int portDefault = 0);
bool LookupSubNet(const char *pszName, CSubNet& subnet);

/**
 * Create a TCP socket in the given address family.
 * @param[in] address_family The socket is created in the same address family as this address.
 * @return pointer to the created Sock object or unique_ptr that owns nothing in case of failure
 */
std::unique_ptr<Sock> CreateSockTCP(const CService& address_family);

/**
 * Socket factory. Defaults to `CreateSockTCP()`, but can be overridden by unit tests.
 */
extern std::function<std::unique_ptr<Sock>(const CService&)> CreateSock;

/**
 * Try to connect to the specified service on the specified socket.
 *
 * @param addrConnect The service to which to connect.
 * @param sock The socket on which to connect.
 * @param nTimeout Wait this many milliseconds for the connection to be
 *                 established.
 * @param manual_connection Whether or not the connection was manually requested
 *                          (e.g. through the addnode RPC)
 *
 * @returns Whether or not a connection was successfully made.
 */
bool ConnectSocketDirectly(const CService &addrConnect, const Sock& sock, int nTimeout);



bool ConnectSocket(const CService &addr, SOCKET& hSocketRet, int nTimeout, bool *outProxyConnectionFailed = 0);
bool ConnectSocketByName(CService &addr, SOCKET& hSocketRet, const char *pszDest, int portDefault, int nTimeout, bool *outProxyConnectionFailed = 0);

bool SetSocketNonBlocking(SOCKET& hSocket, bool fNonBlocking);
/** Set the TCP_NODELAY flag on a socket */
bool SetSocketNoDelay(const SOCKET& hSocket);

#endif // BITCOIN_NETBASE_H
