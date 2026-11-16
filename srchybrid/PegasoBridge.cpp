// Pegaso: the way out of the web requests. See PegasoBridge.h.
#include "stdafx.h"
#include <ws2tcpip.h>
#include <windns.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "emule.h"
#include "PegasoBridge.h"
#include "Preferences.h"
#include "NetGuard.h"
#include "Opcodes.h"
#include "StringConversion.h"

#pragma comment(lib, "dnsapi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	enum EMode
	{
		MODE_DIRECT,	// from the address of the adapter
		MODE_HTTP,
		MODE_SOCKS4,	// no names: not usable for the web
		MODE_SOCKS4A,
		MODE_SOCKS5
	};

	struct SConfig
	{
		EMode		mode = MODE_DIRECT;
		std::string	host;
		int			port = 0;
		std::string	user;
		std::string	password;
		bool		auth = false;
		std::string	bind;		// IPv4 address of the adapter, or empty
		unsigned long ifIndex = 0;	// that adapter, if it has DNS servers of its own: names are asked of them only
		bool		blocked = false;
	};

	const size_t	MAX_HEAD = 64 * 1024;
	const int		CONNECT_SECONDS = 20;
	const int		HANDSHAKE_SECONDS = 30;
	const int		IDLE_SECONDS = 900;

	std::mutex			s_lock;
	SConfig				s_config;			// under s_lock
	std::atomic<int>	s_iPort{0};			// what Port() answers
	int					s_iListenPort = 0;	// main thread; 0 = not listening yet, -1 = failed

	// ---------------------------------------------------------------- sockets

	bool SendAll(SOCKET s, const char *p, size_t n)
	{
		while (n > 0) {
			const int iSent = ::send(s, p, (int)min(n, (size_t)(64 * 1024)), 0);
			if (iSent <= 0)
				return false;
			p += iSent;
			n -= (size_t)iSent;
		}
		return true;
	}

	bool SendAll(SOCKET s, const std::string &data)
	{
		return SendAll(s, data.data(), data.size());
	}

	bool RecvAll(SOCKET s, unsigned char *p, size_t n)
	{
		while (n > 0) {
			const int iRead = ::recv(s, reinterpret_cast<char*>(p), (int)n, 0);
			if (iRead <= 0)
				return false;
			p += iRead;
			n -= (size_t)iRead;
		}
		return true;
	}

	void SetTimeouts(SOCKET s, int iSeconds)
	{
		const DWORD dwTimeout = (DWORD)iSeconds * 1000;
		::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&dwTimeout), sizeof dwTimeout);
		::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&dwTimeout), sizeof dwTimeout);
	}

	// Reads up to the empty line which ends a header; what came after it is left in "rest".
	bool ReadHead(SOCKET s, std::string &head, std::string &rest)
	{
		head.clear();
		char buffer[4096];
		for (;;) {
			const size_t uEnd = head.find("\r\n\r\n");
			if (uEnd != std::string::npos) {
				rest = head.substr(uEnd + 4);
				head.resize(uEnd + 4);
				return true;
			}
			if (head.size() > MAX_HEAD)
				return false;
			const int iRead = ::recv(s, buffer, sizeof buffer, 0);
			if (iRead <= 0)
				return false;
			head.append(buffer, (size_t)iRead);
		}
	}

	bool IsLoopback(const sockaddr *addr)
	{
		if (addr->sa_family == AF_INET)
			return (ntohl(reinterpret_cast<const sockaddr_in*>(addr)->sin_addr.s_addr) >> 24) == 127;
		if (addr->sa_family == AF_INET6)
			return IN6_IS_ADDR_LOOPBACK(&reinterpret_cast<const sockaddr_in6*>(addr)->sin6_addr) != 0;
		return false;
	}

	// One attempt, with a time limit. With an adapter address, from that address - but for a
	// destination on this computer itself, which no adapter carries.
	SOCKET ConnectTo(const sockaddr *addr, int len, const std::string &bind)
	{
		const SOCKET s = ::socket(addr->sa_family, SOCK_STREAM, IPPROTO_TCP);
		if (s == INVALID_SOCKET)
			return INVALID_SOCKET;
		bool bOk = true;
		if (!bind.empty() && !IsLoopback(addr)) {
			sockaddr_in local = {};
			local.sin_family = AF_INET;
			bOk = addr->sa_family == AF_INET && ::inet_pton(AF_INET, bind.c_str(), &local.sin_addr) == 1
				&& ::bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof local) == 0;
		}
		if (bOk) {
			u_long uNonBlocking = 1;
			::ioctlsocket(s, FIONBIO, &uNonBlocking);
			bOk = ::connect(s, addr, len) == 0;
			if (!bOk && ::WSAGetLastError() == WSAEWOULDBLOCK) {
				fd_set setWrite, setError;
				FD_ZERO(&setWrite);
				FD_ZERO(&setError);
				FD_SET(s, &setWrite);
				FD_SET(s, &setError);
				timeval tv = {CONNECT_SECONDS, 0};
				bOk = ::select(0, NULL, &setWrite, &setError, &tv) > 0 && FD_ISSET(s, &setWrite);
			}
			uNonBlocking = 0;
			::ioctlsocket(s, FIONBIO, &uNonBlocking);
		}
		if (!bOk) {
			::closesocket(s);
			return INVALID_SOCKET;
		}
		SetTimeouts(s, HANDSHAKE_SECONDS);
		return s;
	}

	// The IPv4 addresses of a name, asked of the DNS servers of one adapter and of no other:
	// the question does not go to the servers of the connection the VPN is there to hide from.
	bool ResolveOn(unsigned long ifIndex, const std::string &host, std::vector<in_addr> &found)
	{
		const CStringW sName(host.c_str());
		DNS_QUERY_REQUEST request = {};
		request.Version = DNS_QUERY_REQUEST_VERSION1;
		request.QueryName = sName;
		request.QueryType = DNS_TYPE_A;
		request.QueryOptions = DNS_QUERY_STANDARD;
		request.InterfaceIndex = ifIndex;
		DNS_QUERY_RESULT result = {};
		result.Version = DNS_QUERY_RESULTS_VERSION1;
		if (::DnsQueryEx(&request, &result, NULL) != ERROR_SUCCESS)
			return false;
		for (const DNS_RECORD *p = result.pQueryRecords; p != NULL; p = p->pNext)
			if (p->wType == DNS_TYPE_A) {
				in_addr addr;
				addr.s_addr = p->Data.A.IpAddress;
				found.push_back(addr);
			}
		if (result.pQueryRecords != NULL)
			::DnsRecordListFree(result.pQueryRecords, DnsFreeRecordList);
		return !found.empty();
	}

	// A TCP connection to host:port. With an adapter address, only over IPv4 and from it; with
	// the number of the adapter as well, its name is asked of the DNS servers of that adapter.
	SOCKET Connect(const std::string &host, int port, const std::string &bind, unsigned long ifIndex)
	{
		in_addr literal;
		if (!bind.empty() && ifIndex != 0 && ::inet_pton(AF_INET, host.c_str(), &literal) != 1) {
			std::vector<in_addr> found;
			if (!ResolveOn(ifIndex, host, found))
				return INVALID_SOCKET;	// not asked of anyone else
			for (const in_addr &addr : found) {
				sockaddr_in to = {};
				to.sin_family = AF_INET;
				to.sin_addr = addr;
				to.sin_port = htons((u_short)port);
				const SOCKET s = ConnectTo(reinterpret_cast<const sockaddr*>(&to), sizeof to, bind);
				if (s != INVALID_SOCKET)
					return s;
			}
			return INVALID_SOCKET;
		}
		addrinfo hints = {};
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_family = bind.empty() ? AF_UNSPEC : AF_INET;
		addrinfo *pList = NULL;
		char szPort[16];
		sprintf_s(szPort, "%d", port);
		if (::getaddrinfo(host.c_str(), szPort, &hints, &pList) != 0 || pList == NULL)
			return INVALID_SOCKET;
		SOCKET s = INVALID_SOCKET;
		for (const addrinfo *p = pList; p != NULL && s == INVALID_SOCKET; p = p->ai_next)
			s = ConnectTo(p->ai_addr, (int)p->ai_addrlen, bind);
		::freeaddrinfo(pList);
		return s;
	}

	// ---------------------------------------------------------------- SOCKS

	bool Socks5(SOCKET s, const SConfig &cfg, const std::string &host, int port)
	{
		const bool bAuth = cfg.auth && cfg.user.size() < 256 && cfg.password.size() < 256;
		std::string out("\x05", 1);
		out += bAuth ? std::string("\x02\x00\x02", 3) : std::string("\x01\x00", 2);
		unsigned char in[262];
		if (!SendAll(s, out) || !RecvAll(s, in, 2) || in[0] != 5)
			return false;
		if (in[1] == 2) {
			if (!bAuth)
				return false;
			out.assign(1, '\x01');
			out += (char)cfg.user.size();
			out += cfg.user;
			out += (char)cfg.password.size();
			out += cfg.password;
			if (!SendAll(s, out) || !RecvAll(s, in, 2) || in[1] != 0)
				return false;
		} else if (in[1] != 0)
			return false;

		// the name goes to the proxy as it is: the proxy looks it up, not this computer
		out.assign("\x05\x01\x00", 3);
		in_addr addr4;
		in6_addr addr6;
		if (::inet_pton(AF_INET, host.c_str(), &addr4) == 1) {
			out += '\x01';
			out.append(reinterpret_cast<const char*>(&addr4), 4);
		} else if (::inet_pton(AF_INET6, host.c_str(), &addr6) == 1) {
			out += '\x04';
			out.append(reinterpret_cast<const char*>(&addr6), 16);
		} else {
			if (host.empty() || host.size() > 255)
				return false;
			out += '\x03';
			out += (char)host.size();
			out += host;
		}
		out += (char)(port >> 8);
		out += (char)(port & 0xFF);
		if (!SendAll(s, out) || !RecvAll(s, in, 4) || in[0] != 5 || in[1] != 0)
			return false;
		size_t uLeft;
		switch (in[3]) {
		case 1:
			uLeft = 4 + 2;
			break;
		case 4:
			uLeft = 16 + 2;
			break;
		case 3:
			if (!RecvAll(s, in, 1))
				return false;
			uLeft = (size_t)in[0] + 2;
			break;
		default:
			return false;
		}
		return RecvAll(s, in, uLeft);
	}

	bool Socks4a(SOCKET s, const SConfig &cfg, const std::string &host, int port)
	{
		std::string out("\x04\x01", 2);
		out += (char)(port >> 8);
		out += (char)(port & 0xFF);
		in_addr addr4;
		const bool bLiteral = ::inet_pton(AF_INET, host.c_str(), &addr4) == 1;
		if (bLiteral)
			out.append(reinterpret_cast<const char*>(&addr4), 4);
		else
			out.append("\x00\x00\x00\x01", 4);	// "the name follows"
		if (cfg.auth)
			out += cfg.user;
		out += '\0';
		if (!bLiteral) {
			out += host;
			out += '\0';
		}
		unsigned char in[8];
		return SendAll(s, out) && RecvAll(s, in, 8) && in[1] == 0x5A;
	}

	// ---------------------------------------------------------------- HTTP

	std::string Base64(const std::string &in)
	{
		static const char s_acDigit[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		for (size_t i = 0; i < in.size(); i += 3) {
			const unsigned u = ((unsigned char)in[i] << 16)
				| (i + 1 < in.size() ? (unsigned char)in[i + 1] << 8 : 0)
				| (i + 2 < in.size() ? (unsigned char)in[i + 2] : 0);
			out += s_acDigit[(u >> 18) & 63];
			out += s_acDigit[(u >> 12) & 63];
			out += i + 1 < in.size() ? s_acDigit[(u >> 6) & 63] : '=';
			out += i + 2 < in.size() ? s_acDigit[u & 63] : '=';
		}
		return out;
	}

	bool StartsWithNoCase(const std::string &s, const char *pszPrefix)
	{
		const size_t n = strlen(pszPrefix);
		return s.size() >= n && _strnicmp(s.c_str(), pszPrefix, n) == 0;
	}

	// "host:port", "[v6]:port" or just the host
	bool SplitAuthority(std::string authority, int iDefaultPort, std::string &host, int &port)
	{
		const size_t uUser = authority.rfind('@');
		if (uUser != std::string::npos)
			authority.erase(0, uUser + 1);
		port = iDefaultPort;
		size_t uPort = std::string::npos;
		if (!authority.empty() && authority[0] == '[') {
			const size_t uClose = authority.find(']');
			if (uClose == std::string::npos)
				return false;
			host = authority.substr(1, uClose - 1);
			if (uClose + 1 < authority.size() && authority[uClose + 1] == ':')
				uPort = uClose + 2;
		} else {
			const size_t uColon = authority.rfind(':');
			host = authority.substr(0, uColon);
			if (uColon != std::string::npos)
				uPort = uColon + 1;
		}
		if (uPort != std::string::npos)
			port = atoi(authority.c_str() + uPort);
		return !host.empty() && port > 0 && port < 65536;
	}

	// The header of a request, with another first line and one connection per request: the
	// bridge carries bytes after the first header and could not tell the next request apart.
	std::string Rewrite(const std::string &head, const std::string &firstLine, const std::string &extra)
	{
		std::string out(firstLine + "\r\n");
		size_t uPos = head.find("\r\n");
		while (uPos != std::string::npos) {
			uPos += 2;
			const size_t uEnd = head.find("\r\n", uPos);
			if (uEnd == std::string::npos || uEnd == uPos)
				break;
			const std::string line(head.substr(uPos, uEnd - uPos));
			if (!StartsWithNoCase(line, "connection:") && !StartsWithNoCase(line, "proxy-connection:") && !StartsWithNoCase(line, "proxy-authorization:"))
				out += line + "\r\n";
			uPos = uEnd;
		}
		return out + extra + "Connection: close\r\n\r\n";
	}

	void Answer(SOCKET s, const char *pszStatus)
	{
		SendAll(s, std::string("HTTP/1.1 ") + pszStatus + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
	}

	void Relay(SOCKET a, SOCKET b)
	{
		std::vector<char> buffer(64 * 1024);
		for (;;) {
			fd_set setRead;
			FD_ZERO(&setRead);
			FD_SET(a, &setRead);
			FD_SET(b, &setRead);
			timeval tv = {IDLE_SECONDS, 0};
			if (::select(0, &setRead, NULL, NULL, &tv) <= 0)
				return;
			for (int i = 0; i < 2; ++i) {
				const SOCKET from = i == 0 ? a : b;
				if (!FD_ISSET(from, &setRead))
					continue;
				const int iRead = ::recv(from, buffer.data(), (int)buffer.size(), 0);
				if (iRead <= 0 || !SendAll(i == 0 ? b : a, buffer.data(), (size_t)iRead))
					return;
			}
		}
	}

	// One connection of Windows to the bridge. Gives back the socket to the other end, with
	// everything said so that only bytes are left to be carried; INVALID_SOCKET if it failed
	// (the answer to Windows is given here).
	SOCKET Open(SOCKET client, const SConfig &cfg)
	{
		std::string head, rest;
		if (!ReadHead(client, head, rest))
			return INVALID_SOCKET;
		if (cfg.blocked) {
			Answer(client, "503 Held by Pegaso");
			return INVALID_SOCKET;
		}
		const std::string first(head.substr(0, head.find("\r\n")));
		const size_t uSpace1 = first.find(' ');
		const size_t uSpace2 = first.rfind(' ');
		if (uSpace1 == std::string::npos || uSpace2 <= uSpace1) {
			Answer(client, "400 Bad Request");
			return INVALID_SOCKET;
		}
		const std::string method(first.substr(0, uSpace1));
		const std::string target(first.substr(uSpace1 + 1, uSpace2 - uSpace1 - 1));
		const std::string version(first.substr(uSpace2 + 1));
		const bool bTunnel = method == "CONNECT";

		std::string host, path;
		int port = 0;
		if (bTunnel) {
			if (!SplitAuthority(target, 443, host, port)) {
				Answer(client, "400 Bad Request");
				return INVALID_SOCKET;
			}
		} else {
			if (!StartsWithNoCase(target, "http://")) {
				Answer(client, "400 Bad Request");
				return INVALID_SOCKET;
			}
			const size_t uPath = target.find_first_of("/?", 7);
			path = uPath == std::string::npos ? std::string("/") : target.substr(uPath);
			if (path[0] != '/')
				path.insert(0, 1, '/');
			if (!SplitAuthority(target.substr(7, uPath == std::string::npos ? std::string::npos : uPath - 7), 80, host, port)) {
				Answer(client, "400 Bad Request");
				return INVALID_SOCKET;
			}
		}

		const std::string credentials(cfg.auth ? "Proxy-Authorization: Basic " + Base64(cfg.user + ":" + cfg.password) + "\r\n" : std::string());
		SOCKET up;
		bool bOk = true;
		if (cfg.mode == MODE_DIRECT)
			up = Connect(host, port, cfg.bind, cfg.ifIndex);
		else {
			up = Connect(cfg.host, cfg.port, cfg.bind, cfg.ifIndex);
			if (up != INVALID_SOCKET)
				switch (cfg.mode) {
				case MODE_SOCKS5:
					bOk = Socks5(up, cfg, host, port);
					break;
				case MODE_SOCKS4A:
					bOk = Socks4a(up, cfg, host, port);
					break;
				case MODE_HTTP:
					if (bTunnel) {
						std::string answer, more;
						bOk = SendAll(up, "CONNECT " + target + " HTTP/1.1\r\nHost: " + target + "\r\n" + credentials + "\r\n")
							&& ReadHead(up, answer, more) && answer.size() > 12 && answer.compare(9, 3, "200") == 0;
						rest.clear();
						if (bOk && !more.empty())
							bOk = SendAll(client, "HTTP/1.1 200 Connection established\r\n\r\n") && SendAll(client, more);
						else if (bOk)
							bOk = SendAll(client, "HTTP/1.1 200 Connection established\r\n\r\n");
						if (bOk)
							return up;
					}
					break;
				default:
					bOk = false;	// SOCKS4: it cannot be given a name
				}
		}
		if (up == INVALID_SOCKET || !bOk) {
			if (up != INVALID_SOCKET)
				::closesocket(up);
			Answer(client, "502 Bad Gateway");
			return INVALID_SOCKET;
		}

		if (bTunnel)
			bOk = SendAll(client, "HTTP/1.1 200 Connection established\r\n\r\n");
		else if (cfg.mode == MODE_HTTP)	// the proxy wants the whole address
			bOk = SendAll(up, Rewrite(head, first, credentials + "Proxy-Connection: close\r\n"));
		else
			bOk = SendAll(up, Rewrite(head, method + " " + path + " " + version, std::string()));
		if (bOk && !rest.empty())
			bOk = SendAll(up, rest);
		if (!bOk) {
			::closesocket(up);
			return INVALID_SOCKET;
		}
		return up;
	}

	void Serve(SOCKET client)
	{
		SConfig cfg;
		{
			std::lock_guard<std::mutex> lock(s_lock);
			cfg = s_config;
		}
		SetTimeouts(client, HANDSHAKE_SECONDS);
		const SOCKET up = Open(client, cfg);
		if (up != INVALID_SOCKET) {
			SetTimeouts(client, IDLE_SECONDS);
			SetTimeouts(up, IDLE_SECONDS);
			Relay(client, up);
			::closesocket(up);
		}
		::closesocket(client);
	}

	void AcceptLoop(SOCKET listener)
	{
		for (;;) {
			const SOCKET client = ::accept(listener, NULL, NULL);
			if (client == INVALID_SOCKET) {
				::Sleep(100);
				continue;
			}
			try {
				std::thread(Serve, client).detach();
			} catch (...) {
				::closesocket(client);
			}
		}
	}

	// Main thread. The port, or -1.
	int Listen()
	{
		WSADATA wsa;
		if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
			return -1;
		const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listener == INVALID_SOCKET)
			return -1;
		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		int iLen = sizeof addr;
		if (::bind(listener, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0 || ::listen(listener, SOMAXCONN) != 0
			|| ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &iLen) != 0)
		{
			::closesocket(listener);
			return -1;
		}
		try {
			std::thread(AcceptLoop, listener).detach();
		} catch (...) {
			::closesocket(listener);
			return -1;
		}
		return ntohs(addr.sin_port);
	}
}

void PegasoBridge::Sync()
{
	SConfig cfg;
	const ProxySettings &proxy = thePrefs.GetProxySettings();
	if (proxy.bUseProxy) {
		switch (proxy.type) {
		case PROXYTYPE_SOCKS5:
			cfg.mode = MODE_SOCKS5;
			break;
		case PROXYTYPE_SOCKS4A:
			cfg.mode = MODE_SOCKS4A;
			break;
		case PROXYTYPE_SOCKS4:
			cfg.mode = MODE_SOCKS4;
			break;
		default:
			cfg.mode = MODE_HTTP;
		}
		cfg.host = (LPCSTR)CStringA(proxy.host);
		cfg.port = proxy.port;
		cfg.auth = proxy.bEnablePassword;
		cfg.user = (LPCSTR)StrToUtf8(proxy.user);
		cfg.password = (LPCSTR)StrToUtf8(proxy.password);
	}
	cfg.blocked = theNetGuard.IsBlocked();
	if (!cfg.blocked) {
		cfg.bind = (LPCSTR)CStringA(theNetGuard.GetBindAddress());
		if (!cfg.bind.empty() && theNetGuard.HasAdapterDns())
			cfg.ifIndex = theNetGuard.GetIfIndex();
	}

	const bool bNeeded = cfg.mode != MODE_DIRECT || !cfg.bind.empty() || cfg.blocked;
	if (bNeeded && s_iListenPort == 0)
		s_iListenPort = Listen();
	{
		std::lock_guard<std::mutex> lock(s_lock);
		s_config = cfg;
	}
	s_iPort = !bNeeded ? 0 : (cfg.mode == MODE_SOCKS4 ? -1 : s_iListenPort);
}

int PegasoBridge::Port()
{
	return s_iPort;
}
