// Pegaso: what protects the user of this computer. See PegasoPrivacy.h.
#include "stdafx.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <vector>
#include "emule.h"
#include "emuleDlg.h"
#include "PegasoPrivacy.h"
#include "PegasoStrings.h"
#include "PegasoBridge.h"
#include "PegasoIPv6.h"
#include "NetGuard.h"
#include "TorrentQueue.h"
#include "WebDownload.h"
#include "Preferences.h"
#include "Opcodes.h"
#include "OtherFunctions.h"
#include "StringConversion.h"
#include "KnownFileList.h"
#include "SearchDlg.h"
#include "ServerWnd.h"
#include "TransferDlg.h"
#include "DownloadListCtrl.h"
#include "Log.h"
#include "Version.h"

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "iphlpapi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const TCHAR PROTECTED_PREFIX[] = _T("dpapi:");
	const TCHAR UPDATE_URL[] = _T("https://api.github.com/repos/Draev-developer/Pegaso/releases/latest");
}

// ---------------------------------------------------------------- secrets

CString PegasoProtect(const CString &sSecret)
{
	if (sSecret.IsEmpty())
		return sSecret;
	const CStringA sUtf8(StrToUtf8(sSecret));
	DATA_BLOB in;
	in.pbData = (BYTE*)(LPCSTR)sUtf8;
	in.cbData = (DWORD)sUtf8.GetLength();
	DATA_BLOB out = {};
	if (!::CryptProtectData(&in, L"Pegaso", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return sSecret;		// better in clear than lost
	CString sResult(sSecret);
	DWORD dwChars = 0;
	if (::CryptBinaryToString(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &dwChars)) {
		CString sText;
		if (::CryptBinaryToString(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, sText.GetBuffer((int)dwChars), &dwChars)) {
			sText.ReleaseBuffer();
			sResult = PROTECTED_PREFIX + sText;
		} else
			sText.ReleaseBuffer(0);
	}
	::LocalFree(out.pbData);
	return sResult;
}

CString PegasoUnprotect(const CString &sStored)
{
	const int iPrefix = _countof(PROTECTED_PREFIX) - 1;
	if (sStored.Left(iPrefix) != PROTECTED_PREFIX)
		return sStored;
	const CString sText(sStored.Mid(iPrefix));
	DWORD dwSize = 0;
	if (!::CryptStringToBinary(sText, 0, CRYPT_STRING_BASE64, NULL, &dwSize, NULL, NULL) || dwSize == 0)
		return CString();
	std::vector<BYTE> data(dwSize);
	if (!::CryptStringToBinary(sText, 0, CRYPT_STRING_BASE64, data.data(), &dwSize, NULL, NULL))
		return CString();
	DATA_BLOB in;
	in.pbData = data.data();
	in.cbData = dwSize;
	DATA_BLOB out = {};
	if (!::CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return CString();	// another account, another computer
	const CString sResult(OptUtf8ToStr(CStringA(reinterpret_cast<LPCSTR>(out.pbData), (int)out.cbData)));
	::SecureZeroMemory(out.pbData, out.cbData);
	::LocalFree(out.pbData);
	return sResult;
}

void PegasoMigrateSecrets()
{
	const int iPrefix = _countof(PROTECTED_PREFIX) - 1;
	// kept as they were typed
	static const LPCTSTR s_apszPlain[] = {_T("ApiToken")};
	for (LPCTSTR pszKey : s_apszPlain) {
		const CString sValue(theApp.GetProfileString(_T("eMule"), pszKey, _T("")));
		if (!sValue.IsEmpty() && sValue.Left(iPrefix) != PROTECTED_PREFIX)
			theApp.WriteProfileString(_T("eMule"), pszKey, PegasoProtect(sValue));
	}
	// kept URL-encoded
	CStringArray asKeys;
	asKeys.Add(_T("RenameKeyTmdb"));
	asKeys.Add(_T("RenameKeyOmdb"));
	asKeys.Add(_T("RenameKeyTvdb"));
	const int iSites = min(64, (int)theApp.GetProfileInt(_T("eMule"), _T("TorznabCount"), 0));
	for (int i = 0; i < iSites; ++i) {
		CString sKey;
		sKey.Format(_T("Torznab%dKey"), i);
		asKeys.Add(sKey);
	}
	for (INT_PTR i = 0; i < asKeys.GetCount(); ++i) {
		const CString sValue(theApp.GetProfileString(_T("eMule"), asKeys[i], _T("")));
		if (!sValue.IsEmpty() && sValue.Left(iPrefix) != PROTECTED_PREFIX)
			theApp.WriteProfileString(_T("eMule"), asKeys[i], PegasoProtect(CString(OptUtf8ToStr(URLDecode(sValue)))));
	}
}

// ---------------------------------------------------------------- the way out, right now

namespace
{
	// The adapter the system would send a packet to the internet through. Nothing is sent.
	bool BestRoute(int iFamily, CString &sName, CString &sAddress)
	{
		SOCKADDR_STORAGE dest = {};
		if (iFamily == AF_INET) {
			sockaddr_in *p = reinterpret_cast<sockaddr_in*>(&dest);
			p->sin_family = AF_INET;
			::inet_pton(AF_INET, "1.1.1.1", &p->sin_addr);
		} else {
			sockaddr_in6 *p = reinterpret_cast<sockaddr_in6*>(&dest);
			p->sin6_family = AF_INET6;
			::inet_pton(AF_INET6, "2606:4700:4700::1111", &p->sin6_addr);
		}
		DWORD dwIndex = 0;
		if (::GetBestInterfaceEx(reinterpret_cast<sockaddr*>(&dest), &dwIndex) != NO_ERROR)
			return false;
		ULONG uSize = 32 * 1024;
		std::vector<BYTE> buffer(uSize);
		const ULONG uFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
		ULONG uResult = ::GetAdaptersAddresses(AF_UNSPEC, uFlags, NULL, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &uSize);
		if (uResult == ERROR_BUFFER_OVERFLOW) {
			buffer.resize(uSize);
			uResult = ::GetAdaptersAddresses(AF_UNSPEC, uFlags, NULL, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &uSize);
		}
		if (uResult != NO_ERROR)
			return false;
		for (const IP_ADAPTER_ADDRESSES *p = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); p != NULL; p = p->Next) {
			if ((iFamily == AF_INET ? p->IfIndex : p->Ipv6IfIndex) != dwIndex)
				continue;
			sName = p->FriendlyName;
			sAddress.Empty();
			for (const IP_ADAPTER_UNICAST_ADDRESS *pAddr = p->FirstUnicastAddress; pAddr != NULL; pAddr = pAddr->Next) {
				const sockaddr *sa = pAddr->Address.lpSockaddr;
				if (sa == NULL || sa->sa_family != iFamily)
					continue;
				TCHAR szText[INET6_ADDRSTRLEN] = {};
				if (iFamily == AF_INET)
					::InetNtop(AF_INET, const_cast<IN_ADDR*>(&reinterpret_cast<const sockaddr_in*>(sa)->sin_addr), szText, _countof(szText));
				else {
					const in6_addr &addr = reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr;
					if (IN6_IS_ADDR_LINKLOCAL(&addr))
						continue;
					::InetNtop(AF_INET6, const_cast<in6_addr*>(&addr), szText, _countof(szText));
				}
				sAddress = szText;
				break;
			}
			return true;
		}
		return false;
	}

	CString Formatted(EPegasoString id, LPCTSTR psz1, LPCTSTR psz2 = _T(""))
	{
		CString s;
		s.Format(GetPegasoString(id), psz1, psz2);
		return s;
	}
}

CString PegasoConnectionReport()
{
	const ProxySettings &proxy = thePrefs.GetProxySettings();
	const bool bBound = theNetGuard.IsActive() && theNetGuard.IsUp();
	CString sReport;

	// ---- what is set
	if (!theNetGuard.IsActive())
		sReport += GetPegasoString(PS_CC_VPN_OFF);
	else if (theNetGuard.IsUp())
		sReport += Formatted(PS_CC_VPN_ON, theNetGuard.GetAdapter(), theNetGuard.GetBindAddress());
	else
		sReport += Formatted(PS_CC_VPN_DOWN, theNetGuard.GetAdapter());
	sReport += _T("\r\n");
	if (theNetGuard.IsActive())
		sReport += GetPegasoString(theNetGuard.HasKillSwitch() ? PS_CC_KILL_ON : PS_CC_KILL_OFF) + CString(_T("\r\n"));
	CString sProxy;
	if (proxy.bUseProxy) {
		static const LPCTSTR s_apszType[] = {_T(""), _T("SOCKS4"), _T("SOCKS4a"), _T("SOCKS5"), _T("HTTP"), _T("HTTP")};
		sProxy.Format(_T("%s %s:%u"), proxy.type < _countof(s_apszType) ? s_apszType[proxy.type] : _T(""), (LPCTSTR)proxy.host, (unsigned)proxy.port);
		sReport += Formatted(PS_CC_PROXY, sProxy);
	} else
		sReport += GetPegasoString(PS_CC_PROXY_NONE);
	sReport += _T("\r\n");
	if (theNetGuard.IsBlocked())
		sReport += GetPegasoString(PS_CC_HELD) + CString(_T("\r\n"));
	sReport += _T("\r\n");

	// ---- the way of each protocol
	CString sRouteName, sRouteAddress;
	const bool bRoute = BestRoute(AF_INET, sRouteName, sRouteAddress);
	const CString sSystem(bRoute ? Formatted(PS_CC_VIA_SYSTEM, sRouteName, sRouteAddress) : CString(GetPegasoString(PS_CC_NOT_USED)));
	const CString sAdapter(bBound ? Formatted(PS_CC_VIA_ADAPTER, theNetGuard.GetAdapter(), theNetGuard.GetBindAddress()) : sSystem);
	const CString sByProxy(Formatted(PS_CC_VIA_PROXY, sProxy));
	const CString sNotUsed(GetPegasoString(PS_CC_NOT_USED));
	const CString sHeld(GetPegasoString(PS_CC_STOPPED));
	const bool bHeld = theNetGuard.IsBlocked();

	auto line = [&sReport](const CString &sWhat, const CString &sHow) {
		sReport += sWhat + _T(": ") + sHow + _T("\r\n");
	};
	line(_T("eD2K (TCP)"), bHeld ? sHeld : (proxy.bUseProxy ? sByProxy : sAdapter));
	line(_T("eD2K (UDP), Kad"), bHeld ? sHeld : (CNetGuard::UdpHeldByProxy() ? sNotUsed : sAdapter));
	if (!theTorrents.IsRunning())
		line(_T("BitTorrent"), sNotUsed);
	else {
		line(_T("BitTorrent"), bHeld ? sHeld : (proxy.bUseProxy ? sByProxy : sAdapter));
		line(_T("BitTorrent DHT"), bHeld ? sHeld : (!proxy.bUseProxy ? sAdapter : (proxy.type == PROXYTYPE_SOCKS5 ? sByProxy : sNotUsed)));
	}
	line(GetPegasoString(PS_CC_WEB), bHeld ? sHeld : (PegasoBridge::Port() < 0 ? sNotUsed : (proxy.bUseProxy ? sByProxy : sAdapter)));
	CString sRoute6Name, sRoute6Address;
	if (Pegaso6::Enabled() && !bHeld && BestRoute(AF_INET6, sRoute6Name, sRoute6Address))
		line(_T("IPv6"), Formatted(PS_CC_VIA_SYSTEM, sRoute6Name, sRoute6Address));
	else
		line(_T("IPv6"), sNotUsed);

	// ---- what it means
	if (!proxy.bUseProxy && !theNetGuard.IsActive())
		sReport += _T("\r\n") + CString(GetPegasoString(PS_CC_UNPROTECTED)) + _T("\r\n");
	else if (proxy.bUseProxy && !CNetGuard::UdpHeldByProxy())
		sReport += _T("\r\n") + CString(GetPegasoString(PS_CC_UDP_ROUND)) + _T("\r\n");
	return sReport;
}

// ---------------------------------------------------------------- CPegasoConnDlg

IMPLEMENT_DYNAMIC(CPegasoConnDlg, CDialog)

BEGIN_MESSAGE_MAP(CPegasoConnDlg, CDialog)
	ON_BN_CLICKED(IDC_CC_REFRESH, OnBnClickedRefresh)
	ON_BN_CLICKED(IDC_CC_COPY, OnBnClickedCopy)
	ON_WM_TIMER()
END_MESSAGE_MAP()

CPegasoConnDlg::CPegasoConnDlg(CWnd *pParent)
	: CDialog(CPegasoConnDlg::IDD, pParent)
{
}

BOOL CPegasoConnDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	SetWindowText(GetPegasoString(PS_CC_MENU));
	SetDlgItemText(IDC_CC_REFRESH, GetPegasoString(PS_CC_REFRESH));
	SetDlgItemText(IDC_CC_COPY, GetPegasoString(PS_CC_COPY));
	SetDlgItemText(IDCANCEL, GetPegasoString(PS_WS_CLOSE));
	OnBnClickedRefresh();
	SetTimer(1, 3000, NULL);	// a VPN which drops shows here without being asked
	GetDlgItem(IDCANCEL)->SetFocus();	// not the text: it would show up all selected
	return FALSE;
}

void CPegasoConnDlg::OnBnClickedRefresh()
{
	const CString sReport(PegasoConnectionReport());
	CString sShown;
	GetDlgItemText(IDC_CC_TEXT, sShown);
	if (sShown != sReport)
		SetDlgItemText(IDC_CC_TEXT, sReport);
}

void CPegasoConnDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1)
		OnBnClickedRefresh();
	else
		CDialog::OnTimer(nIDEvent);
}

void CPegasoConnDlg::OnBnClickedCopy()
{
	CString sText;
	GetDlgItemText(IDC_CC_TEXT, sText);
	::CopyTextToClipboard(sText);
}

// ---------------------------------------------------------------- traces

bool PegasoClearTracesOnExit()
{
	return theApp.GetProfileInt(_T("eMule"), _T("ClearTracesOnExit"), 0) != 0;
}

void PegasoSetClearTracesOnExit(bool bOn)
{
	theApp.WriteProfileInt(_T("eMule"), _T("ClearTracesOnExit"), bOn ? 1 : 0);
}

void PegasoClearTraces()
{
	CemuleDlg *pDlg = theApp.emuledlg;
	if (pDlg == NULL || pDlg->m_hWnd == NULL)
		return;
	// what was searched, and the addresses typed for the lists of servers
	if (pDlg->searchwnd != NULL && pDlg->searchwnd->m_hWnd != NULL) {
		pDlg->searchwnd->ResetHistory();
		pDlg->searchwnd->DeleteAllSearches();
	}
	if (pDlg->serverwnd != NULL && pDlg->serverwnd->m_hWnd != NULL)
		pDlg->serverwnd->ResetHistory();
	// what was downloaded: finished entries of the list, cancelled files, files no longer shared
	if (pDlg->transferwnd != NULL && pDlg->transferwnd->m_hWnd != NULL)
		pDlg->transferwnd->GetDownloadList().ClearCompleted();
	if (theApp.knownfiles != NULL)
		theApp.knownfiles->ClearHistory();
	// what was done
	pDlg->ResetLog();
	pDlg->ResetDebugLog();
	pDlg->ResetServerInfo();
	theLog.Close();
	theVerboseLog.Close();
	const CString sLogDir(thePrefs.GetMuleDirectory(EMULE_LOGDIR));
	CFileFind finder;
	for (BOOL bFound = finder.FindFile(sLogDir + _T("*.log")); bFound;) {
		bFound = finder.FindNextFile();
		if (!finder.IsDirectory())
			::DeleteFile(finder.GetFilePath());
	}
	finder.Close();
	if (!theApp.IsClosing()) {
		if (thePrefs.GetLog2Disk())
			theLog.Open();
		if (thePrefs.GetDebug2Disk())
			theVerboseLog.Open();
	}
}

void PegasoClearTracesAsk(CWnd *pParent)
{
	const HWND hParent = pParent->GetSafeHwnd();
	if (::MessageBox(hParent, GetPegasoString(PS_CT_ASK), GetPegasoString(PS_CT_MENU), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;
	PegasoClearTraces();
	::MessageBox(hParent, GetPegasoString(PS_CT_DONE), GetPegasoString(PS_CT_MENU), MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------- new version

namespace
{
	// The body of one answer, up to 256 KB. Goes the way of every web request of Pegaso.
	bool HttpGet(const CString &sUrl, std::string &body)
	{
		URL_COMPONENTS uc = {};
		uc.dwStructSize = sizeof uc;
		TCHAR szHost[256];
		TCHAR szPath[2048];
		uc.lpszHostName = szHost;
		uc.dwHostNameLength = _countof(szHost);
		uc.lpszUrlPath = szPath;
		uc.dwUrlPathLength = _countof(szPath);
		if (!::WinHttpCrackUrl(sUrl, 0, 0, &uc))
			return false;
		bool bOk = false;
		HINTERNET hSession = PegasoHttpSession();
		HINTERNET hConnect = NULL;
		HINTERNET hRequest = NULL;
		if (hSession != NULL) {
			::WinHttpSetTimeouts(hSession, 15000, 15000, 30000, 30000);
			hConnect = ::WinHttpConnect(hSession, szHost, uc.nPort, 0);
		}
		if (hConnect != NULL)
			hRequest = ::WinHttpOpenRequest(hConnect, L"GET", szPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES
				, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
		if (hRequest != NULL
			&& ::WinHttpSendRequest(hRequest, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
			&& ::WinHttpReceiveResponse(hRequest, NULL))
		{
			DWORD dwStatus = 0;
			DWORD dwSize = sizeof dwStatus;
			::WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatus, &dwSize, WINHTTP_NO_HEADER_INDEX);
			char buffer[8192];
			DWORD dwRead = 0;
			while (dwStatus == 200 && body.size() < 256 * 1024 && ::WinHttpReadData(hRequest, buffer, sizeof buffer, &dwRead) && dwRead > 0)
				body.append(buffer, dwRead);
			bOk = dwStatus == 200;
		}
		if (hRequest != NULL)
			::WinHttpCloseHandle(hRequest);
		if (hConnect != NULL)
			::WinHttpCloseHandle(hConnect);
		if (hSession != NULL)
			::WinHttpCloseHandle(hSession);
		return bOk;
	}

	void UpdateThread(CString sUrl)
	{
		std::string body;
		if (!HttpGet(sUrl, body))
			return;
		// "tag_name": "v0.6.2"
		size_t uPos = body.find("\"tag_name\"");
		if (uPos == std::string::npos)
			return;
		uPos = body.find('"', body.find(':', uPos));
		if (uPos == std::string::npos)
			return;
		const size_t uEnd = body.find('"', uPos + 1);
		if (uEnd == std::string::npos || uEnd - uPos > 32)
			return;
		const std::string tag(body.substr(uPos + 1, uEnd - uPos - 1));
		int aiTheirs[3] = {};
		int iPart = 0;
		bool bDigits = false;
		for (const char c : tag) {
			if (c >= '0' && c <= '9') {
				aiTheirs[iPart] = aiTheirs[iPart] * 10 + (c - '0');
				bDigits = true;
			} else if (c == '.' && bDigits && iPart < 2)
				++iPart;
			else if (bDigits)
				break;
		}
		const int aiOurs[3] = {PEGASO_VERSION_MJR, PEGASO_VERSION_MIN, PEGASO_VERSION_UPD};
		int iDiff = 0;
		for (int i = 0; i < 3 && iDiff == 0; ++i)
			iDiff = aiTheirs[i] - aiOurs[i];
		if (iDiff > 0 && !theApp.IsClosing())
			theApp.QueueLogLineEx(LOG_STATUSBAR | LOG_WARNING, _T("A new version of Pegaso is available: %d.%d.%d (this one is %s) - %s/releases")
				, aiTheirs[0], aiTheirs[1], aiTheirs[2], PEGASO_VERSION_NAME, PEGASO_URL);
	}
}

void PegasoUpdateCheck()
{
	if (theNetGuard.IsBlocked())
		return;		// asked again at the next start
	if (thePrefs.GetLastVC() != 0) {
		struct tm tmTemp;
		const time_t tNow = safe_mktime(CTime::GetCurrentTime().GetLocalTm(&tmTemp));
		if (difftime(tNow, thePrefs.GetLastVC()) / DAY2S(1) < thePrefs.GetUpdateDays())
			return;
	}
	thePrefs.UpdateLastVC();
	try {
		std::thread(UpdateThread, theApp.GetProfileString(_T("eMule"), _T("UpdateCheckUrl"), UPDATE_URL)).detach();
	} catch (...) {
	}
}
