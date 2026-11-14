// Pegaso: VPN protection. See NetGuard.h.
#include "stdafx.h"
#include <iphlpapi.h>
#include "emule.h"
#include "NetGuard.h"
#include "Preferences.h"
#include "emuleDlg.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "StringConversion.h"
#include "ServerConnect.h"
#include "kademlia/kademlia/Kademlia.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CNetGuard theNetGuard;

namespace
{
	// Sockets bound to the loopback address cannot reach the network: used while the
	// adapter is down and the kill switch is on.
	LPCTSTR const BLOCKED_ADDRESS = _T("127.0.0.1");
}

CNetGuard::CNetGuard()
	: m_dwLastCheck()
	, m_bKillSwitch(true)
	, m_bBlocked()
	, m_bUp()
{
}

std::vector<SNetAdapter> CNetGuard::ListAdapters()
{
	std::vector<SNetAdapter> out;
	ULONG uSize = 16 * 1024;
	std::vector<BYTE> buffer;
	DWORD dwResult = ERROR_BUFFER_OVERFLOW;
	for (int i = 0; i < 4 && dwResult == ERROR_BUFFER_OVERFLOW; ++i) {
		buffer.resize(uSize);
		dwResult = ::GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER
			, NULL, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &uSize);
	}
	if (dwResult != ERROR_SUCCESS)
		return out;
	for (PIP_ADAPTER_ADDRESSES p = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()); p != NULL; p = p->Next) {
		if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
			continue;
		SNetAdapter adapter;
		adapter.name = p->FriendlyName;
		adapter.description = p->Description;
		adapter.up = p->OperStatus == IfOperStatusUp;
		for (PIP_ADAPTER_UNICAST_ADDRESS pAddr = p->FirstUnicastAddress; pAddr != NULL; pAddr = pAddr->Next)
			if (pAddr->Address.lpSockaddr != NULL && pAddr->Address.lpSockaddr->sa_family == AF_INET) {
				const sockaddr_in *sin = reinterpret_cast<const sockaddr_in*>(pAddr->Address.lpSockaddr);
				TCHAR szAddr[INET_ADDRSTRLEN] = {};
				if (::InetNtop(AF_INET, const_cast<IN_ADDR*>(&sin->sin_addr), szAddr, _countof(szAddr)) != NULL) {
					adapter.ipv4 = szAddr;
					break;
				}
			}
		out.push_back(adapter);
	}
	return out;
}

CString CNetGuard::AdapterInPrefs()
{
	// stored URL-encoded (UTF-8): adapter names may contain characters outside of the ANSI code page of the ini file
	return CString(OptUtf8ToStr(URLDecode(theApp.GetProfileString(_T("eMule"), _T("PegasoBindAdapter"), _T("")))));
}

bool CNetGuard::KillSwitchInPrefs()
{
	return theApp.GetProfileInt(_T("eMule"), _T("PegasoKillSwitch"), 1) != 0;
}

void CNetGuard::Init()
{
	m_sAdapter = AdapterInPrefs().Trim();
	m_bKillSwitch = KillSwitchInPrefs();
	Refresh(true);
}

void CNetGuard::ApplyPrefs(const CString &sAdapter, bool bKillSwitch)
{
	CString sName(sAdapter);
	sName.Trim();
	theApp.WriteProfileString(_T("eMule"), _T("PegasoBindAdapter"), EncodeUrlUtf8(sName));
	theApp.WriteProfileInt(_T("eMule"), _T("PegasoKillSwitch"), bKillSwitch ? 1 : 0);
	const bool bWasActive = IsActive();
	m_sAdapter = sName;
	m_bKillSwitch = bKillSwitch;
	if (!IsActive() && bWasActive)
		thePrefs.SetBindAddrOverride(CString());	// back to "any adapter" for new connections
	Refresh(true);
}

void CNetGuard::Process()
{
	const DWORD dwNow = ::GetTickCount();
	if (dwNow - m_dwLastCheck < 2000)
		return;
	m_dwLastCheck = dwNow;
	if (IsActive())
		Refresh(false);
}

void CNetGuard::Refresh(bool bLog)
{
	if (!IsActive()) {
		m_bBlocked = false;
		m_bUp = false;
		m_sBindAddress.Empty();
		return;
	}

	bool bUp = false;
	CString sAddress;
	for (const SNetAdapter &adapter : ListAdapters())
		if (adapter.name.CompareNoCase(m_sAdapter) == 0) {
			bUp = adapter.up && !adapter.ipv4.IsEmpty();
			sAddress = adapter.ipv4;
			break;
		}

	const bool bBlocked = !bUp && m_bKillSwitch;
	// Down without kill switch: no binding, the traffic takes whatever route the system has.
	const CString sBind(bUp ? sAddress : (m_bKillSwitch ? CString(BLOCKED_ADDRESS) : CString()));
	const bool bChanged = bUp != m_bUp || bBlocked != m_bBlocked || sBind != m_sBindAddress;
	m_bUp = bUp;
	m_bBlocked = bBlocked;
	if (sBind != m_sBindAddress) {
		m_sBindAddress = sBind;
		// ed2k/Kad: sockets opened from now on use the new address. The listening sockets
		// keep the one they were created with until Pegaso is restarted.
		thePrefs.SetBindAddrOverride(sBind);
	}

	if (bChanged || bLog) {
		if (bUp)
			AddLogLine(bChanged, _T("VPN protection: adapter \"%s\" is connected (%s); transfers use only this adapter"), (LPCTSTR)m_sAdapter, (LPCTSTR)sAddress);
		else if (bBlocked)
			LogError(LOG_STATUSBAR, _T("VPN protection: adapter \"%s\" is not connected - all transfers are stopped"), (LPCTSTR)m_sAdapter);
		else
			LogWarning(LOG_STATUSBAR, _T("VPN protection: adapter \"%s\" is not connected and the kill switch is off - transfers are NOT protected"), (LPCTSTR)m_sAdapter);
	}
	// the ed2k server and Kad connections would only keep failing: drop them
	if (bChanged && bBlocked && theApp.emuledlg != NULL && theApp.emuledlg->m_hWnd != NULL && theApp.serverconnect != NULL && !theApp.IsClosing()) {
		theApp.serverconnect->StopConnectionTry();
		theApp.serverconnect->Disconnect();
		Kademlia::CKademlia::Stop();
		theApp.emuledlg->ShowConnectionState();
	}
}
