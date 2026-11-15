// Pegaso: IPv6 for eD2K and Kad sources, see PegasoIPv6.h
#include "stdafx.h"
#include <array>
#include <map>
#include <vector>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "emule.h"
#include "PegasoIPv6.h"
#include "AsyncSocketEx.h"
#include "ListenSocket.h"
#include "ClientList.h"
#include "UpDownClient.h"
#include "PartFile.h"
#include "DownloadQueue.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Log.h"
#include "Opcodes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	typedef std::array<uint8, 16> Addr;

	std::map<Addr, uint32>	s_index;		// address -> position in s_addrs, plus one
	std::vector<Addr>		s_addrs;

	bool		s_bHaveOwn;
	in6_addr	s_own;
	bool		s_bReachable;				// something proved that s_own takes connections
	DWORD		s_dwLastLook;
	bool		s_bLooked;
	uint32		s_uPendingKad;

	Addr ToArray(const in6_addr &addr)
	{
		Addr a;
		memcpy(a.data(), &addr, 16);
		return a;
	}

	bool LabMode()
	{
		return !thePrefs.FilterLANIPs();	// "this is a private network": every unicast address goes
	}

	bool SamePrefix64(const in6_addr &a, const in6_addr &b)
	{
		return memcmp(&a, &b, 8) == 0;
	}

	// The addresses of this computer: the first global one, a stable one rather than a temporary one.
	bool LocalAddress(in6_addr &rAddr)
	{
		ULONG uSize = 16 * 1024;
		std::vector<BYTE> buffer(uSize);
		ULONG uResult = ::GetAdaptersAddresses(AF_INET6, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER
			, NULL, (PIP_ADAPTER_ADDRESSES)buffer.data(), &uSize);
		if (uResult == ERROR_BUFFER_OVERFLOW) {
			buffer.resize(uSize);
			uResult = ::GetAdaptersAddresses(AF_INET6, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER
				, NULL, (PIP_ADAPTER_ADDRESSES)buffer.data(), &uSize);
		}
		if (uResult != NO_ERROR)
			return false;
		bool bFound = false;
		for (const IP_ADAPTER_ADDRESSES *pAdapter = (PIP_ADAPTER_ADDRESSES)buffer.data(); pAdapter != NULL; pAdapter = pAdapter->Next) {
			if (pAdapter->OperStatus != IfOperStatusUp || pAdapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || pAdapter->IfType == IF_TYPE_TUNNEL)
				continue;
			for (const IP_ADAPTER_UNICAST_ADDRESS *pUni = pAdapter->FirstUnicastAddress; pUni != NULL; pUni = pUni->Next) {
				if (pUni->Address.lpSockaddr == NULL || pUni->Address.lpSockaddr->sa_family != AF_INET6 || pUni->DadState != IpDadStatePreferred)
					continue;
				const in6_addr &addr = ((const sockaddr_in6*)pUni->Address.lpSockaddr)->sin6_addr;
				if (IN6_IS_ADDR_LINKLOCAL(&addr) || IN6_IS_ADDR_LOOPBACK(&addr) || !Pegaso6::IsAcceptable(addr))
					continue;
				const bool bStable = pUni->SuffixOrigin != IpSuffixOriginRandom;
				if (!bFound || bStable) {
					rAddr = addr;
					bFound = true;
					if (bStable)
						return true;
				}
			}
		}
		return bFound;
	}

	void Look()
	{
		s_bLooked = true;
		s_dwLastLook = ::GetTickCount();
		in6_addr addr = {};
		bool bHave = false;
		if (Pegaso6::Enabled()) {
			const CStringA sFixed(theApp.GetProfileString(_T("eMule"), _T("IPv6Address"), _T("")));
			if (!sFixed.IsEmpty())
				bHave = inet_pton(AF_INET6, sFixed, &addr) == 1;	// the user knows better (NAT66, tests)
			else
				bHave = LocalAddress(addr);
		}
		if (bHave != s_bHaveOwn || (bHave && memcmp(&addr, &s_own, sizeof addr) != 0)) {
			s_bHaveOwn = bHave;
			s_own = addr;
			s_bReachable = false;	// what was proved was about the other address
			if (bHave) {
				char sz[INET6_ADDRSTRLEN] = {};
				inet_ntop(AF_INET6, &addr, sz, sizeof sz);
				AddLogLine(false, _T("IPv6: this computer's address is %hs"), sz);
			} else
				AddDebugLogLine(false, _T("IPv6: no public address"));
		}
	}

	// ---- incoming connections over IPv6: a listener of its own next to eMule's IPv4 one
	class CListen6 : public CAsyncSocketEx
	{
	public:
		uint16 m_uPort = 0;

		bool Start(uint16 uPort)
		{
			if (!Create(uPort, SOCK_STREAM, FD_ACCEPT, CString(), AF_INET6))	// every IPv6 address; IPv6 only, as Windows sets it
				return false;
			if (!Listen()) {
				Close();
				return false;
			}
			m_uPort = uPort;
			return true;
		}

		bool m_bFailed = false;	// it never came to listen: nothing to close

		void Stop()
		{
			if (m_uPort != 0 && !m_bFailed)
				Close();
			m_uPort = 0;
		}

	protected:
		virtual void OnAccept(int nErrorCode)
		{
			if (nErrorCode != 0 || theApp.listensocket == NULL)
				return;
			CClientReqSocket *pSocket = new CClientReqSocket;
			SOCKADDR_IN6 from = {};
			int iLen = sizeof from;
			if (!Accept(*pSocket, (LPSOCKADDR)&from, &iLen)) {
				pSocket->Safe_Delete();
				return;
			}
			theApp.listensocket->AddConnection();
			const uint32 uPseudo = Pegaso6::PseudoOf(from.sin6_addr);
			if (uPseudo == 0 || !Pegaso6::Enabled() || theApp.listensocket->TooManySockets() || theApp.clientlist->IsBannedClient(uPseudo)) {
				pSocket->Safe_Delete();
				return;
			}
			pSocket->AsyncSelect(FD_WRITE | FD_READ | FD_CLOSE);
		}
	};
	CListen6 *s_pListen;
}

bool Pegaso6::IsAcceptable(const in6_addr &addr)
{
	const uint8 *b = addr.u.Byte;
	if (IN6_IS_ADDR_UNSPECIFIED(&addr) || b[0] == 0xff)
		return false;
	if (LabMode())
		return true;
	static const uint8 mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
	static const uint8 nat64[12] = {0x00, 0x64, 0xff, 0x9b, 0, 0, 0, 0, 0, 0, 0, 0};
	static const uint8 discard[8] = {0x01, 0x00, 0, 0, 0, 0, 0, 0};
	if (IN6_IS_ADDR_LOOPBACK(&addr) || (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) || (b[0] & 0xfe) == 0xfc
		|| memcmp(b, mapped, 12) == 0 || memcmp(b, nat64, 12) == 0 || memcmp(b, discard, 8) == 0)
	{
		return false;
	}
	if (b[0] == 0x20 && b[1] == 0x01) {
		if ((b[2] == 0 && b[3] == 0)					// 2001::/32 Teredo
			|| (b[2] == 0 && (b[3] & 0xf0) == 0x20)		// 2001:20::/28
			|| (b[2] == 0x0d && b[3] == 0xb8))			// 2001:db8::/32 documentation
			return false;
	}
	if ((b[0] == 0x20 && b[1] == 0x02) || (b[0] == 0x5f && b[1] == 0x00))	// 6to4, reserved
		return false;
	return (b[0] & 0xe0) == 0x20;	// global unicast, 2000::/3
}

uint32 Pegaso6::PseudoOf(const in6_addr &addr)
{
	if (!IsAcceptable(addr))
		return 0;
	const Addr key(ToArray(addr));
	uint32 &ruIndex = s_index[key];
	if (ruIndex == 0) {
		if (s_addrs.size() >= 0xFFFFFE) {	// more addresses than low IDs: never in one session
			s_index.erase(key);
			return 0;
		}
		s_addrs.push_back(key);
		ruIndex = (uint32)s_addrs.size();
	}
	return 0xF0u | (ruIndex << 8);
}

bool Pegaso6::AddrOf(uint32 uPseudo, in6_addr &rAddr)
{
	const uint32 uIndex = uPseudo >> 8;
	if (!IsPseudo(uPseudo) || uIndex == 0 || uIndex > s_addrs.size())
		return false;
	memcpy(&rAddr, s_addrs[uIndex - 1].data(), 16);
	return true;
}

uint32 Pegaso6::PlaceholderID(uint32 uPseudo)
{
	return IsPseudo(uPseudo) ? (uPseudo >> 8) : 0;
}

CString Pegaso6::Text(uint32 uPseudo)
{
	in6_addr addr;
	if (!AddrOf(uPseudo, addr))
		return _T("?");
	char sz[INET6_ADDRSTRLEN] = {};
	inet_ntop(AF_INET6, &addr, sz, sizeof sz);
	return CString(sz);
}

bool Pegaso6::FromHex(const CString &sHex, in6_addr &rAddr)
{
	if (sHex.GetLength() != 32)
		return false;
	for (int i = 0; i < 16; ++i) {
		int iByte = 0;
		for (int k = 0; k < 2; ++k) {
			const TCHAR c = sHex[i * 2 + k];
			int iDigit;
			if (c >= _T('0') && c <= _T('9'))
				iDigit = c - _T('0');
			else if (c >= _T('a') && c <= _T('f'))
				iDigit = c - _T('a') + 10;
			else if (c >= _T('A') && c <= _T('F'))
				iDigit = c - _T('A') + 10;
			else
				return false;
			iByte = iByte * 16 + iDigit;
		}
		rAddr.u.Byte[i] = (uint8)iByte;
	}
	return true;
}

CString Pegaso6::ToHex(const in6_addr &addr)
{
	CString s;
	for (int i = 0; i < 16; ++i)
		s.AppendFormat(_T("%02x"), addr.u.Byte[i]);
	return s;
}

bool Pegaso6::Enabled()
{
	if (theApp.GetProfileInt(_T("eMule"), _T("IPv6Enabled"), 1) == 0)
		return false;
	// bound to an adapter (VPN protection) or behind a proxy: IPv6 would go around them
	return (thePrefs.GetBindAddrA() == NULL || *thePrefs.GetBindAddrA() == '\0') && !thePrefs.GetProxySettings().bUseProxy;
}

bool Pegaso6::Own(in6_addr &rAddr)
{
	if (!s_bLooked)
		Look();
	if (!s_bHaveOwn)
		return false;
	rAddr = s_own;
	return true;
}

bool Pegaso6::PeerGateOpen()
{
	in6_addr addr;
	return Own(addr) && (s_bReachable || theApp.GetProfileInt(_T("eMule"), _T("IPv6AssumeReachable"), 0) != 0);
}

void Pegaso6::NoteInboundHello(uint32 uPeerPseudo)
{
	in6_addr peer;
	if (s_bReachable || !s_bHaveOwn || !AddrOf(uPeerPseudo, peer))
		return;
	// a peer of our own network never crossed the router: it proves nothing
	if (!LabMode() && SamePrefix64(peer, s_own))
		return;
	s_bReachable = true;
	AddLogLine(false, _T("IPv6: a connection got in, the address is reachable"));
}

void Pegaso6::NoteServerVerdict(uint8 uStatus)
{
	if (!s_bHaveOwn || (uStatus & IPV6ST_PROBED) == 0)
		return;	// only a real probe says something
	const bool bReachable = (uStatus & IPV6ST_REACHABLE) != 0;
	if (bReachable != s_bReachable)
		AddLogLine(false, bReachable ? _T("IPv6: the server reached this computer, the address is reachable")
			: _T("IPv6: the server could not reach this computer (router or firewall closed for IPv6)"));
	s_bReachable = bReachable;
}

void Pegaso6::Process()
{
	if (!theApp.IsRunning())
		return;
	if (!s_bLooked || ::GetTickCount() - s_dwLastLook >= SEC2MS(30))
		Look();
	const uint16 uPort = s_bHaveOwn && theApp.listensocket != NULL ? thePrefs.GetPort() : 0;
	if (s_pListen != NULL && s_pListen->m_uPort != uPort) {
		s_pListen->Stop();
		delete s_pListen;
		s_pListen = NULL;
	}
	if (s_pListen == NULL && uPort != 0) {
		s_pListen = new CListen6;
		if (s_pListen->Start(uPort))
			AddDebugLogLine(false, _T("IPv6: listening on port %u"), uPort);
		else {
			AddDebugLogLine(false, _T("IPv6: cannot listen on port %u"), uPort);
			s_pListen->m_bFailed = true;
			s_pListen->m_uPort = uPort;	// not again until something changes
		}
	}
}

void Pegaso6::Stop()
{
	if (s_pListen != NULL) {
		s_pListen->Stop();
		delete s_pListen;
		s_pListen = NULL;
	}
}

CUpDownClient* Pegaso6::NewSource(CPartFile *pFile, const in6_addr &addr, uint16 uPort)
{
	in6_addr own;
	if (uPort == 0 || !Enabled() || !Own(own) || (memcmp(&own, &addr, sizeof own) == 0 && uPort == thePrefs.GetPort()))
		return NULL;	// without an IPv6 of our own there is no way to it
	const uint32 uPseudo = PseudoOf(addr);
	if (uPseudo == 0 || theApp.clientlist->IsBannedClient(uPseudo))
		return NULL;
	CUpDownClient *pClient = new CUpDownClient(pFile, uPort, PlaceholderID(uPseudo), 0, 0, false);
	pClient->SetPegasoIP6(uPseudo);
	return pClient;
}

void Pegaso6::SetPendingKad(uint32 uPseudo)
{
	s_uPendingKad = uPseudo;
}

uint32 Pegaso6::PendingKad()
{
	in6_addr own;
	return s_uPendingKad != 0 && Enabled() && Own(own) ? s_uPendingKad : 0;
}
