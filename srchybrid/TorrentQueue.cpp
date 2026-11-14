// Pegaso: glue between the application and the BitTorrent engine.
#include "stdafx.h"
#include "emule.h"
#include "TorrentQueue.h"
#include "Opcodes.h"
#include "Preferences.h"
#include "OtherFunctions.h"
#include "Log.h"
#include "NetGuard.h"
#include "WebDownload.h"
#include "PartFile.h"
#include "DownloadQueue.h"
#include "SharedFileList.h"
#include "HybridSwarm.h"
#include "StringConversion.h"
#include "emuleDlg.h"
#include "TransferDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CTorrentQueue theTorrents;

namespace
{
	int LimitFromPrefs(uint64 uBytesPerSec)
	{
		return (uBytesPerSec == UNLIMITED * 1024ull || uBytesPerSec > INT_MAX) ? 0 : (int)uBytesPerSec;
	}

	LPCTSTR StateName(ETorrentState state)
	{
		switch (state) {
		case TS_METADATA:
			return _T("getting metadata");
		case TS_CHECKING:
			return _T("checking");
		case TS_DOWNLOADING:
			return _T("downloading");
		case TS_SEEDING:
			return _T("seeding");
		case TS_PAUSED:
			return _T("paused");
		default:
			return _T("error");
		}
	}
}

CTorrentQueue::CTorrentQueue()
	: m_dwLastPoll()
	, m_dwLastReport()
	, m_uDatarate()
	, m_uUpDatarate()
	, m_bStartTried()
	, m_bNetPaused()
{
}

UINT CTorrentQueue::SeedRatioInPrefs()
{
	return (UINT)max(0, (int)theApp.GetProfileInt(_T("eMule"), _T("TorrentSeedRatio"), 200));
}

bool CTorrentQueue::IsEnabledInPrefs()
{
	return theApp.GetProfileInt(_T("eMule"), _T("TorrentEnabled"), 1) != 0;
}

UINT CTorrentQueue::PortInPrefs()
{
	const UINT uPort = theApp.GetProfileInt(_T("eMule"), _T("TorrentPort"), 6881);
	return (uPort >= 1024 && uPort <= 65535) ? uPort : 6881;
}

bool CTorrentQueue::RequireEncryptionInPrefs()
{
	return theApp.GetProfileInt(_T("eMule"), _T("TorrentRequireEncryption"), 0) != 0;
}

void CTorrentQueue::ApplyPrefs(bool bEnabled, UINT uPort, bool bRequireEncryption)
{
	const bool bChanged = bEnabled != IsEnabledInPrefs() || uPort != PortInPrefs() || bRequireEncryption != RequireEncryptionInPrefs();
	theApp.WriteProfileInt(_T("eMule"), _T("TorrentEnabled"), bEnabled ? 1 : 0);
	theApp.WriteProfileInt(_T("eMule"), _T("TorrentPort"), (int)uPort);
	theApp.WriteProfileInt(_T("eMule"), _T("TorrentRequireEncryption"), bRequireEncryption ? 1 : 0);
	if (bChanged || bEnabled != m_engine.IsRunning()) {
		Stop();
		Start();
	}
}

void CTorrentQueue::SetFilesWanted(const std::string &id, const std::vector<bool> &wanted)
{
	// 'wanted' follows the order of GetFiles()
	const std::vector<STorrentFile> files(m_engine.GetFiles(id));
	std::vector<std::pair<int, bool> > changes;
	for (size_t i = 0; i < files.size() && i < wanted.size(); ++i)
		if (files[i].wanted != wanted[i])
			changes.push_back(std::make_pair(files[i].index, (bool)wanted[i]));
	if (!changes.empty())
		m_engine.SetFilesWanted(id, changes);
}

void CTorrentQueue::Start()
{
	m_bStartTried = true;
	if (m_engine.IsRunning() || !IsEnabledInPrefs())
		return;

	STorrentSettings settings;
	settings.savePath = (LPCTSTR)thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);
	settings.stateDir = (LPCTSTR)(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("torrents\\"));
	settings.userAgent = CStringA(PEGASO_NAME _T("/") PEGASO_VERSION_NAME);
	settings.listenPort = (int)PortInPrefs();
	settings.requireEncryption = RequireEncryptionInPrefs();
	m_sBoundTo = theNetGuard.GetBindAddress();	// VPN protection
	settings.bindAddress = CStringA(m_sBoundTo);
	m_bNetPaused = false;
	settings.downloadLimit = LimitFromPrefs(thePrefs.GetMaxDownloadInBytesPerSec(false));
	settings.uploadLimit = LimitFromPrefs(thePrefs.GetMaxUpload() == UNLIMITED ? UNLIMITED * 1024ull : thePrefs.GetMaxUpload() * 1024ull);

	std::wstring error;
	if (m_engine.Start(settings, error))
		AddLogLine(false, _T("BitTorrent engine started"));
	else
		LogError(LOG_STATUSBAR, _T("BitTorrent engine failed to start: %s"), error.c_str());
}

void CTorrentQueue::Stop()
{
	if (m_engine.IsRunning()) {
		m_engine.Stop();
		AddLogLine(false, _T("BitTorrent engine stopped"));
	}
	m_status.clear();
	m_uDatarate = 0;
	// the stand-ins belong to the download queue, which deletes them when the application closes
	if (!theApp.IsClosing())
		RemoveProxies();
	m_proxies.clear();
}

void CTorrentQueue::RemoveProxies()
{
	for (const auto &entry : m_proxies) {
		theApp.downloadqueue->RemoveFile(entry.second);
		theApp.emuledlg->transferwnd->GetDownloadList().RemoveFile(entry.second);
		delete entry.second;
	}
	m_proxies.clear();
}

// Keeps one row per torrent in the download list.
void CTorrentQueue::SyncDownloadList()
{
	if (theApp.IsClosing() || theApp.downloadqueue == NULL || theApp.emuledlg == NULL || theApp.emuledlg->transferwnd == NULL)
		return;
	std::map<std::string, CPartFile*> gone(m_proxies);
	for (const STorrentStatus &st : m_status) {
		if (theHybrid.IsHelper(st.id))	// a source of another download: no row of its own
			continue;
		const auto it = m_proxies.find(st.id);
		if (it != m_proxies.end()) {
			it->second->UpdateFromTorrent(st);
			gone.erase(st.id);
		} else {
			CPartFile *pFile = CPartFile::CreateTorrentProxy(st);
			m_proxies[st.id] = pFile;
			theApp.downloadqueue->AddDownload(pFile, false);
		}
	}
	for (const auto &entry : gone) {
		theApp.downloadqueue->RemoveFile(entry.second);
		theApp.emuledlg->transferwnd->GetDownloadList().RemoveFile(entry.second);
		delete entry.second;
		m_proxies.erase(entry.first);
	}
}

bool CTorrentQueue::AddWebSeed(const std::string &id, const CString &sUrl)
{
	CString s(sUrl);
	s.Trim();
	if (!CWebDownloadQueue::IsWebUrl(s)) {
		LogError(LOG_STATUSBAR, _T("Not a web address: %s"), (LPCTSTR)s);
		return false;
	}
	if (!m_engine.AddWebSeed(id, std::string(CStringA(EncodeUrlUtf8(s)))))
		return false;
	AddLogLine(true, _T("Web source added to the torrent: %s"), (LPCTSTR)s);
	return true;
}

bool CTorrentQueue::IsTorrentLink(const CString &sLink)
{
	CString s(sLink);
	s.Trim(_T(" \t\r\n\""));
	if (_tcsnicmp(s, _T("magnet:?"), 8) == 0)
		return CTorrentEngine::IsTorrentMagnet(std::string(CStringA(s)));
	return s.GetLength() > 8 && s.Right(8).CompareNoCase(_T(".torrent")) == 0 && s.Find(_T("://")) < 0;
}

bool CTorrentQueue::AddLink(const CString &sLink)
{
	if (!IsTorrentLink(sLink))
		return false;
	CString s(sLink);
	s.Trim(_T(" \t\r\n\""));
	if (!m_bStartTried)
		Start();

	std::wstring error;
	bool bOk;
	if (_tcsnicmp(s, _T("magnet:?"), 8) == 0)
		bOk = m_engine.AddMagnet(std::string(CStringA(s)), error);
	else
		bOk = m_engine.AddTorrentFile(std::wstring((LPCTSTR)s), error);
	if (!bOk)
		LogError(LOG_STATUSBAR, _T("Torrent not added: %s"), error.c_str());
	return true;
}

void CTorrentQueue::Process()
{
	if (!m_bStartTried)
		Start();
	if (!m_engine.IsRunning())
		return;
	// VPN protection: the adapter address changed (connected, reconnected, dropped) -> bind again
	if (theNetGuard.GetBindAddress() != m_sBoundTo) {
		Stop();
		Start();
		if (!m_engine.IsRunning())
			return;
	}
	if (theNetGuard.IsBlocked() != m_bNetPaused) {
		m_bNetPaused = theNetGuard.IsBlocked();
		m_engine.PauseSession(m_bNetPaused);
	}
	const DWORD dwNow = ::GetTickCount();
	if (dwNow - m_dwLastPoll < 1000)
		return;
	m_dwLastPoll = dwNow;

	// The download limit covers ed2k, web and torrents together: the torrents get what the
	// others leave, but never less than a tenth.
	int iDownLimit = LimitFromPrefs(thePrefs.GetMaxDownloadInBytesPerSec(true));
	if (iDownLimit > 0 && theApp.downloadqueue != NULL) {
		const uint32 uTotal = theApp.downloadqueue->GetDatarate();
		const int iOther = (int)min((uint32)INT_MAX / 2, uTotal > m_uDatarate ? uTotal - m_uDatarate : 0);
		const int iFloor = max(iDownLimit / 10, 1024);
		iDownLimit = (iOther + iFloor < iDownLimit) ? iDownLimit - iOther : iFloor;
	}
	m_engine.SetLimits(iDownLimit
		, LimitFromPrefs(thePrefs.GetMaxUpload() == UNLIMITED ? UNLIMITED * 1024ull : thePrefs.GetMaxUpload() * 1024ull));

	std::vector<STorrentEvent> events;
	m_engine.Poll(events);
	for (const STorrentEvent &ev : events) {
		switch (ev.kind) {
		case TE_ADDED:
			AddLogLine(true, _T("Torrent added: %s"), ev.name.empty() ? CString(CStringA(ev.id.c_str())).GetString() : ev.name.c_str());
			break;
		case TE_METADATA:
			AddLogLine(false, _T("Torrent metadata received: %s"), ev.name.c_str());
			break;
		case TE_FINISHED:
			if (theHybrid.IsHelper(ev.id))	// not a download of its own, see HybridSwarm.h
				break;
			AddLogLine(true, _T("Torrent finished: %s"), ev.name.c_str());
			// mark what was downloaded as coming from the Internet, like any other download
			for (const STorrentFile &file : m_engine.GetFiles(ev.id))
				if (file.wanted)
					PegasoMarkFromInternet(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR) + file.path.c_str());
			// what landed in the incoming folder is offered on the ed2k and Kad networks as well
			if (theApp.sharedfiles != NULL)
				theApp.sharedfiles->Reload();
			break;
		case TE_REMOVED:
			AddLogLine(false, _T("Torrent removed: %s"), ev.name.c_str());
			break;
		case TE_ERROR:
			LogError(LOG_STATUSBAR, _T("Torrent error: %s%s%s"), ev.name.c_str(), ev.name.empty() ? _T("") : _T(" - "), ev.text.c_str());
			break;
		case TE_INFO:
			AddDebugLogLine(false, _T("BitTorrent: %s"), ev.text.c_str());
		}
	}

	m_status = m_engine.GetStatus();
	theHybrid.Process(m_engine, m_status);
	SyncDownloadList();
	uint32 uRate = 0;
	uint32 uUpRate = 0;
	const UINT uSeedRatio = SeedRatioInPrefs();
	for (const STorrentStatus &st : m_status) {
		uRate += (uint32)max(0, st.downloadRate);
		uUpRate += (uint32)max(0, st.uploadRate);
		// enough given back: stop seeding (once; the user may resume it)
		if (uSeedRatio > 0 && st.state == TS_SEEDING && st.size > 0 && !theHybrid.IsHelper(st.id)
			&& (double)st.uploaded * 100.0 >= (double)st.size * uSeedRatio && m_ratioStopped.insert(st.id).second)
		{
			m_engine.Pause(st.id);
			AddLogLine(false, _T("Torrent \"%s\": shared %u%% of its size, seeding stopped"), st.name.c_str(), uSeedRatio);
		}
	}
	m_uDatarate = uRate;
	m_uUpDatarate = uUpRate;
	if (uRate > 0)
		theApp.UpdateReceivedBytes(uRate);	// about one second worth of data: keeps the session statistics going

	// until the torrents have their place in the transfer window, the verbose log tells how they are doing
	if (thePrefs.GetVerbose() && dwNow - m_dwLastReport >= 15000) {
		m_dwLastReport = dwNow;
		for (const STorrentStatus &st : m_status)
			if (st.state != TS_SEEDING && st.state != TS_PAUSED)
				AddDebugLogLine(false, _T("Torrent \"%s\": %s, %s of %s, %s/s, %d peers (%d seeds)%s%s")
					, st.name.c_str(), StateName(st.state)
					, (LPCTSTR)CastItoXBytes((uint64)st.done), (LPCTSTR)CastItoXBytes((uint64)st.size)
					, (LPCTSTR)CastItoXBytes((uint64)max(0, st.downloadRate)), st.peers, st.seeds
					, st.error.empty() ? _T("") : _T(" - "), st.error.c_str());
	}
}
