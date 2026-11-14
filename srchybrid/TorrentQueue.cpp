// Pegaso: glue between the application and the BitTorrent engine.
#include "stdafx.h"
#include "emule.h"
#include "TorrentQueue.h"
#include "Opcodes.h"
#include "Preferences.h"
#include "OtherFunctions.h"
#include "Log.h"
#include "PartFile.h"
#include "DownloadQueue.h"
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
	, m_bStartTried()
{
}

void CTorrentQueue::Start()
{
	m_bStartTried = true;
	if (m_engine.IsRunning() || theApp.GetProfileInt(_T("eMule"), _T("TorrentEnabled"), 1) == 0)
		return;

	STorrentSettings settings;
	settings.savePath = (LPCTSTR)thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);
	settings.stateDir = (LPCTSTR)(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("torrents\\"));
	settings.userAgent = CStringA(PEGASO_NAME _T("/") PEGASO_VERSION_NAME);
	settings.listenPort = (int)theApp.GetProfileInt(_T("eMule"), _T("TorrentPort"), 6881);
	settings.requireEncryption = theApp.GetProfileInt(_T("eMule"), _T("TorrentRequireEncryption"), 0) != 0;
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
	const DWORD dwNow = ::GetTickCount();
	if (dwNow - m_dwLastPoll < 1000)
		return;
	m_dwLastPoll = dwNow;

	m_engine.SetLimits(LimitFromPrefs(thePrefs.GetMaxDownloadInBytesPerSec(true))
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
			AddLogLine(true, _T("Torrent finished: %s"), ev.name.c_str());
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
	SyncDownloadList();
	uint32 uRate = 0;
	for (const STorrentStatus &st : m_status)
		uRate += (uint32)max(0, st.downloadRate);
	m_uDatarate = uRate;
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
