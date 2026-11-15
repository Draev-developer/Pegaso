// Pegaso: NAT-PMP and PCP port mapping, see PegasoPortMap.h
#include "stdafx.h"
#include <atomic>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include <iphlpapi.h>
#include "emule.h"
#include "PegasoPortMap.h"
#include "PegasoStrings.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Log.h"
#include "Opcodes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CPegasoPortMap thePortMap;

bool PegasoIsNotPublicAddress(uint32 uIP)
{
	const uint32 a = uIP >> 24, b = (uIP >> 16) & 0xff;
	return uIP == 0 || a == 10 || a == 127 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168)
		|| (a == 100 && b >= 64 && b <= 127) || (a == 169 && b == 254) || a >= 224;
}

namespace
{
	const uint16 GATEWAY_PORT = 5351;
	const uint32 LIFETIME = 7200;	// seconds asked for; renewed at half of what was granted

	enum EState
	{
		ST_NONE,			// not asked, or the gateway speaks neither protocol
		ST_MAPPED,
		ST_WRONG_PORT,		// the gateway would only give another port
		ST_NO_PUBLIC		// the gateway has no public address
	};

	struct SResult
	{
		EState	state = ST_NONE;
		const char *pszProtocol = "";
		uint32	uExternalIP = 0;	// host order
		uint16	uOfferedPort = 0;	// ST_WRONG_PORT
		uint16	uWantedPort = 0;
		uint32	uLifetime = 0;
	};

	struct SJob
	{
		uint16	uTCP = 0;
		uint16	uUDP = 0;
		uint32	uGateway = 0;	// network order; 0 = find it
		uint32	uBind = 0;		// network order; 0 = any
		bool	bRenew = false;
	};

	std::mutex			s_lock;				// guards everything below
	SResult				s_result;
	bool				s_bReport;			// s_result is news for the log
	bool				s_bBusy;
	bool				s_bPCP;				// what the mapping was made with (for the release)
	SJob				s_job;				// of the mapping in force
	uint32				s_uGateway;			// of the mapping in force
	DWORD				s_dwMapped;
	CStringA			s_sNoPublic;		// to be reported
	bool				s_bNoPublicReported;
	struct SConflict { uint16 port; bool tcp; CStringA other; };
	std::vector<SConflict> s_conflicts;		// to be reported
	std::set<uint32>	s_conflictsReported;
	std::atomic<bool>	s_bAbort;

	uint32 FindGateway(uint32 uBind)
	{
		MIB_IPFORWARDROW row = {};
		if (::GetBestRoute(inet_addr("1.1.1.1"), uBind, &row) != NO_ERROR)
			return 0;
		return row.dwForwardNextHop;	// 0 if the destination is on the link itself
	}

	// Sends a request and waits for an answer, asking again with doubled patience.
	int Exchange(SOCKET s, const char *pRequest, int iRequest, char *pAnswer, int iCapacity, int iTries)
	{
		for (int i = 0; i < iTries && !s_bAbort; ++i) {
			if (send(s, pRequest, iRequest, 0) != iRequest)
				return 0;
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(s, &fds);
			timeval tv = {0, 250000 << i};
			tv.tv_sec = tv.tv_usec / 1000000;
			tv.tv_usec %= 1000000;
			if (select(0, &fds, NULL, NULL, &tv) > 0) {
				const int iRead = recv(s, pAnswer, iCapacity, 0);
				if (iRead > 0)
					return iRead;
				return 0;	// the port is closed (ICMP): nobody there
			}
		}
		return 0;
	}

	uint16 Get16(const char *p)	{ return (uint16)(((uchar)p[0] << 8) | (uchar)p[1]); }
	uint32 Get32(const char *p)	{ return ((uint32)Get16(p) << 16) | Get16(p + 2); }
	void Put16(char *p, uint16 v)	{ p[0] = (char)(v >> 8); p[1] = (char)v; }
	void Put32(char *p, uint32 v)	{ Put16(p, (uint16)(v >> 16)); Put16(p + 2, (uint16)v); }

	// ---- NAT-PMP. Returns the result code of the gateway, -1 without an answer.
	int NatPmpMap(SOCKET s, bool bTCP, uint16 uPort, uint16 uExternal, uint32 uLifetime, uint16 &ruGranted, uint32 &ruLifetime, int iTries)
	{
		char req[12] = {}, ans[32];
		req[1] = bTCP ? 2 : 1;
		Put16(req + 4, uPort);
		Put16(req + 6, uExternal);
		Put32(req + 8, uLifetime);
		const int iRead = Exchange(s, req, sizeof req, ans, sizeof ans, iTries);
		if (iRead < 16 || (uchar)ans[1] != 128 + req[1])
			return -1;
		ruGranted = Get16(ans + 10);
		ruLifetime = Get32(ans + 12);
		return Get16(ans + 2);
	}

	// ---- PCP. Returns the result code of the gateway, -1 without an answer.
	int PcpMap(SOCKET s, uint32 uClientIP, bool bTCP, uint16 uPort, uint16 uExternal, uint32 uLifetime, uint16 &ruGranted, uint32 &ruLifetime
		, uint32 &ruExternalIP, int iTries)
	{
		char req[60] = {}, ans[1100];
		req[0] = 2;		// version
		req[1] = 1;		// MAP
		Put32(req + 4, uLifetime);
		req[18] = req[19] = (char)0xff;				// the client address, as ::ffff:a.b.c.d
		memcpy(req + 20, &uClientIP, 4);
		for (int i = 0; i < 12; ++i)				// nonce: the same for a port for as long as Pegaso runs
			req[24 + i] = (char)(0x50 + i * 7 + (bTCP ? 1 : 0) + (uPort >> (i % 8)));
		req[36] = bTCP ? 6 : 17;
		Put16(req + 40, uPort);
		Put16(req + 42, uExternal);
		req[54] = req[55] = (char)0xff;				// suggested external address: none (::ffff:0.0.0.0)
		const int iRead = Exchange(s, req, sizeof req, ans, sizeof ans, iTries);
		if (iRead < 60 || ans[0] != 2 || (uchar)ans[1] != 0x81 || memcmp(ans + 24, req + 24, 12) != 0)
			return -1;
		ruLifetime = Get32(ans + 4);
		ruGranted = Get16(ans + 42);
		ruExternalIP = Get32(ans + 56);
		return (uchar)ans[3];
	}

	SOCKET OpenSocket(uint32 uGateway, uint32 uBind, uint32 &ruLocalIP)
	{
		SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (s == INVALID_SOCKET)
			return s;
		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = uBind;
		sockaddr_in gw = {};
		gw.sin_family = AF_INET;
		gw.sin_addr.s_addr = uGateway;
		gw.sin_port = htons(GATEWAY_PORT);
		int iLen = sizeof addr;
		if ((uBind != 0 && bind(s, (sockaddr*)&addr, sizeof addr) != 0) || connect(s, (sockaddr*)&gw, sizeof gw) != 0
			|| getsockname(s, (sockaddr*)&addr, &iLen) != 0)
		{
			closesocket(s);
			return INVALID_SOCKET;
		}
		ruLocalIP = addr.sin_addr.s_addr;
		return s;
	}

	void Release(SOCKET s, bool bPCP, uint32 uLocalIP, const SJob &job)
	{
		uint16 uGranted;
		uint32 uLifetime, uIP;
		for (int i = 0; i < 2; ++i) {
			const uint16 uPort = i == 0 ? job.uTCP : job.uUDP;
			if (uPort == 0)
				continue;
			if (bPCP)
				PcpMap(s, uLocalIP, i == 0, uPort, 0, 0, uGranted, uLifetime, uIP, 1);
			else
				NatPmpMap(s, i == 0, uPort, 0, 0, uGranted, uLifetime, 1);
		}
	}

	void Work(SJob job)
	{
		SResult res;
		uint32 uGateway = job.uGateway ? job.uGateway : FindGateway(job.uBind);
		bool bPCP = false;
		uint32 uLocalIP = 0;
		SOCKET s = uGateway ? OpenSocket(uGateway, job.uBind, uLocalIP) : INVALID_SOCKET;
		if (s != INVALID_SOCKET) {
			// the public address, in NAT-PMP; a gateway which only speaks PCP says so
			char req[2] = {}, ans[32];
			const int iRead = Exchange(s, req, sizeof req, ans, sizeof ans, 3);
			bool bSpeaks = false;
			if (iRead >= 8 && (uchar)ans[1] == 128) {
				const int iCode = Get16(ans + 2);
				if (ans[0] == 0 && iCode == 0 && iRead >= 12) {
					res.uExternalIP = Get32(ans + 8);
					bSpeaks = true;
				} else if (iCode == 1 || ans[0] != 0)
					bSpeaks = bPCP = true;		// unsupported version
				else if (iCode == 3) {			// "network failure": the gateway has no address itself
					res.state = ST_NO_PUBLIC;
					res.pszProtocol = "NAT-PMP";
				}
			} else if (iRead >= 24 && ans[0] == 2)
				bSpeaks = bPCP = true;
			if (bSpeaks) {
				res.pszProtocol = bPCP ? "PCP" : "NAT-PMP";
				if (!bPCP && PegasoIsNotPublicAddress(res.uExternalIP))
					res.state = ST_NO_PUBLIC;
				else {
					bool bOK = true;
					uint32 uGrantedLife = LIFETIME;
					for (int i = 0; i < 2 && bOK && !s_bAbort; ++i) {
						const uint16 uPort = i == 0 ? job.uTCP : job.uUDP;
						if (uPort == 0)
							continue;
						uint16 uGranted = 0;
						uint32 uLifetime = 0, uIP = res.uExternalIP;
						const int iCode = bPCP ? PcpMap(s, uLocalIP, i == 0, uPort, uPort, LIFETIME, uGranted, uLifetime, uIP, 3)
							: NatPmpMap(s, i == 0, uPort, uPort, LIFETIME, uGranted, uLifetime, 3);
						if (iCode != 0) {
							bOK = false;
							theApp.QueueDebugLogLine(false, _T("%hs: mapping %s port %u refused, result %d"), res.pszProtocol, i == 0 ? _T("TCP") : _T("UDP"), uPort, iCode);
						} else if (uGranted != uPort) {
							bOK = false;
							res.state = ST_WRONG_PORT;
							res.uOfferedPort = uGranted;
							res.uWantedPort = uPort;
						} else {
							res.uExternalIP = uIP;
							uGrantedLife = min(uGrantedLife, max(uLifetime, 120u));
						}
					}
					if (bOK && !s_bAbort && PegasoIsNotPublicAddress(res.uExternalIP)) {
						bOK = false;
						res.state = ST_NO_PUBLIC;
					}
					if (bOK && !s_bAbort) {
						res.state = ST_MAPPED;
						res.uLifetime = uGrantedLife;
					} else
						Release(s, bPCP, uLocalIP, job);	// what was given (another port, half of the pair) goes back
				}
			}
			closesocket(s);
		}
		res.uWantedPort = res.uWantedPort ? res.uWantedPort : job.uTCP;

		std::lock_guard<std::mutex> guard(s_lock);
		s_bBusy = false;
		if (s_bAbort)
			return;
		const bool bWasMapped = s_result.state == ST_MAPPED;
		s_result = res;
		if (res.state == ST_MAPPED) {
			s_bPCP = bPCP;
			s_job = job;
			s_uGateway = uGateway;
			s_dwMapped = ::GetTickCount();
			s_bReport = !job.bRenew || !bWasMapped;
		} else if (res.state == ST_NO_PUBLIC) {
			if (s_sNoPublic.IsEmpty())
				s_sNoPublic = CStringA(ipstr(htonl(res.uExternalIP)));
		} else
			s_bReport = res.state == ST_WRONG_PORT;
	}

	void Launch(const SJob &job)
	{
		{
			std::lock_guard<std::mutex> guard(s_lock);
			if (s_bBusy)
				return;
			s_bBusy = true;
		}
		std::thread(Work, job).detach();
	}
}

void CPegasoPortMap::Start(uint16 uTCPPort, uint16 uUDPPort)
{
	SJob job;
	job.uTCP = uTCPPort;
	job.uUDP = uUDPPort;
	const CString sGateway(theApp.GetProfileString(_T("eMule"), _T("PortMapGateway"), _T("")));
	if (!sGateway.IsEmpty())
		job.uGateway = inet_addr(CStringA(sGateway));
	if (thePrefs.GetBindAddrA() != NULL && *thePrefs.GetBindAddrA() != '\0')
		job.uBind = inet_addr(thePrefs.GetBindAddrA());
	if (job.uGateway == INADDR_NONE || job.uBind == INADDR_NONE || uTCPPort == 0)
		return;
	Launch(job);
}

bool CPegasoPortMap::HasFixedGateway()
{
	return !theApp.GetProfileString(_T("eMule"), _T("PortMapGateway"), _T("")).IsEmpty();
}

bool CPegasoPortMap::IsMapped() const
{
	std::lock_guard<std::mutex> guard(s_lock);
	return s_result.state == ST_MAPPED;
}

void CPegasoPortMap::NoteNoPublicAddress(const CStringA &sWanIP)
{
	std::lock_guard<std::mutex> guard(s_lock);
	if (s_sNoPublic.IsEmpty())
		s_sNoPublic = sWanIP.IsEmpty() ? CStringA("?") : sWanIP;
}

void CPegasoPortMap::NoteConflict(uint16 uPort, bool bTCP, const CStringA &sOtherIP)
{
	std::lock_guard<std::mutex> guard(s_lock);
	s_conflicts.push_back(SConflict{uPort, bTCP, sOtherIP});
}

void CPegasoPortMap::Process()
{
	SResult res;
	bool bReport;
	CStringA sNoPublic;
	std::vector<SConflict> conflicts;
	SJob renew, current;
	{
		std::lock_guard<std::mutex> guard(s_lock);
		res = s_result;
		current = s_job;
		bReport = s_bReport;
		s_bReport = false;
		if (!s_sNoPublic.IsEmpty() && !s_bNoPublicReported) {
			s_bNoPublicReported = true;
			sNoPublic = s_sNoPublic;
		}
		for (const SConflict &c : s_conflicts)
			if (s_conflictsReported.insert(((uint32)c.tcp << 16) | c.port).second)
				conflicts.push_back(c);
		s_conflicts.clear();
		if (res.state == ST_MAPPED && !s_bBusy && ::GetTickCount() - s_dwMapped >= SEC2MS(res.uLifetime / 2)) {
			renew = s_job;
			renew.bRenew = true;
			renew.uGateway = s_uGateway;
		}
	}
	if (bReport) {
		if (res.state == ST_MAPPED) {
			CString sText;
			sText.Format(GetPegasoString(PS_PM_OK), (LPCTSTR)CString(res.pszProtocol), (UINT)current.uTCP, (UINT)current.uUDP, (LPCTSTR)ipstr(htonl(res.uExternalIP)));
			AddLogLine(false, _T("%s"), (LPCTSTR)sText);
		} else if (res.state == ST_WRONG_PORT) {
			CString sText;
			sText.Format(GetPegasoString(PS_PM_WRONG_PORT), (LPCTSTR)CString(res.pszProtocol), (UINT)res.uOfferedPort, (UINT)res.uWantedPort, (UINT)res.uWantedPort);
			LogWarning(LOG_STATUSBAR, _T("%s"), (LPCTSTR)sText);
		}
	}
	if (!sNoPublic.IsEmpty()) {
		CString sText;
		sText.Format(GetPegasoString(PS_PM_NO_PUBLIC), (LPCTSTR)CString(sNoPublic));
		LogWarning(LOG_STATUSBAR, _T("%s"), (LPCTSTR)sText);
	}
	for (const SConflict &c : conflicts) {
		CString sText;
		sText.Format(GetPegasoString(PS_PM_CONFLICT), (UINT)c.port, c.tcp ? _T("TCP") : _T("UDP"), (LPCTSTR)CString(c.other));
		LogWarning(LOG_STATUSBAR, _T("%s"), (LPCTSTR)sText);
	}
	if (renew.uTCP != 0)
		Launch(renew);
}

void CPegasoPortMap::Stop()
{
	s_bAbort = true;
	for (int i = 0; i < 30; ++i) {	// a question to the gateway may be on its way: up to 3 seconds
		{
			std::lock_guard<std::mutex> guard(s_lock);
			if (!s_bBusy)
				break;
		}
		::Sleep(100);
	}
	SJob job;
	bool bPCP;
	uint32 uGateway;
	{
		std::lock_guard<std::mutex> guard(s_lock);
		if (s_result.state != ST_MAPPED || s_bBusy)
			return;
		s_result.state = ST_NONE;
		job = s_job;
		bPCP = s_bPCP;
		uGateway = s_uGateway;
	}
	s_bAbort = false;	// the release itself is not to be cut short
	uint32 uLocalIP = 0;
	const SOCKET s = OpenSocket(uGateway, job.uBind, uLocalIP);
	if (s != INVALID_SOCKET) {
		Release(s, bPCP, uLocalIP, job);
		closesocket(s);
	}
	s_bAbort = true;
}
