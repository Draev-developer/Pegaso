// Pegaso: built-in ed2k servers and Kad bootstrap. See PegasoDefaults.h
#include "stdafx.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "emule.h"
#include "emuleDlg.h"
#include "PegasoDefaults.h"
#include "Preferences.h"
#include "Opcodes.h"
#include "Server.h"
#include "ServerList.h"
#include "ServerWnd.h"
#include "ServerListCtrl.h"
#include "WebDownload.h"
#include "NetGuard.h"
#include "Log.h"
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/routing/RoutingZone.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	struct SDefaultServer
	{
		LPCTSTR	address;
		uint16	port;
		LPCTSTR	name;
	};

	// Present in both reference lists (upd.emule-security.org and emuling.gitlab.io), November 2026.
	const SDefaultServer s_aServers[] =
	{
		{_T("176.123.5.89"), 4725, _T("eMule Sunrise")},
		{_T("85.17.116.222"), 6082, _T("ed2k-rust")},
		{_T("91.208.162.182"), 4232, _T("MO-Server")},
		{_T("77.42.68.79"), 4232, _T("Nordic-Server")},
		{_T("91.208.162.87"), 4232, _T("!! Sharing-Devils No.4 !!")},
		{_T("85.121.5.137"), 4232, _T("!! Sharing-Devils No.2 !!")},
		{_T("212.95.35.240"), 4232, _T("eMule Cosmic")},
		{_T("91.208.162.55"), 4235, _T("!! Sharing-Devils No.3 !!")},
		{_T("146.19.248.158"), 4325, _T("!! Sharing-Devils No.1 !!")}
	};

	LPCTSTR const s_apszServerMetUrls[] =
	{
		_T("https://upd.emule-security.org/server.met"),
		_T("https://emuling.gitlab.io/server.met")
	};

	LPCTSTR const s_pszNodesUrl = _T("https://upd.emule-security.org/nodes.dat");

	struct SNodesJob
	{
		std::mutex	lock;
		bool		done = false;
		std::vector<BYTE> data;
		CString		error;
	};

	std::shared_ptr<SNodesJob> s_job;	// main thread only
	DWORD s_dwLastFetch;				// main thread only

	void NodesWorker(std::shared_ptr<SNodesJob> job)
	{
		std::vector<BYTE> data;
		CString sError;
		if (!PegasoHttpGet(s_pszNodesUrl, data, sError, 1024 * 1024))
			data.clear();
		std::lock_guard<std::mutex> guard(job->lock);
		job->data.swap(data);
		job->error = sError;
		job->done = true;
	}
}

int PegasoAddDefaultServers()
{
	if (theApp.emuledlg == NULL || theApp.emuledlg->serverwnd == NULL)
		return 0;
	int iAdded = 0;
	for (const SDefaultServer &def : s_aServers) {
		CServer *pServer = new CServer(def.port, def.address);
		pServer->SetListName(def.name);
		if (theApp.emuledlg->serverwnd->serverlistctrl.AddServer(pServer, true))
			++iAdded;
		else
			delete pServer;
	}
	if (iAdded > 0)
		AddLogLine(false, _T("Server list was empty: %d built-in servers added"), iAdded);
	return iAdded;
}

void PegasoDefaultServerMetUrls(CStringList &list)
{
	for (LPCTSTR pszUrl : s_apszServerMetUrls)
		list.AddTail(pszUrl);
}

void PegasoBootstrapKad()
{
	if (s_job || theNetGuard.IsBlocked() || !Kademlia::CKademlia::IsRunning())
		return;
	const DWORD dwNow = ::GetTickCount();
	if (s_dwLastFetch != 0 && dwNow - s_dwLastFetch < MIN2MS(5))
		return;
	Kademlia::CRoutingZone *pZone = Kademlia::CKademlia::GetRoutingZone();
	if (pZone == NULL || pZone->GetNumContacts() > 0 || !Kademlia::CKademlia::s_liBootstrapList.IsEmpty())
		return;
	s_dwLastFetch = dwNow ? dwNow : 1;
	s_job = std::make_shared<SNodesJob>();
	AddLogLine(false, _T("Kad has no contacts: fetching a contact list from %s"), s_pszNodesUrl);
	std::thread(NodesWorker, s_job).detach();
}

void PegasoDefaultsProcess()
{
	if (!s_job)
		return;
	std::vector<BYTE> data;
	CString sError;
	{
		std::lock_guard<std::mutex> guard(s_job->lock);
		if (!s_job->done)
			return;
		data.swap(s_job->data);
		sError = s_job->error;
	}
	s_job.reset();

	if (data.empty()) {
		LogWarning(LOG_STATUSBAR, _T("Kad contact list could not be fetched: %s"), (LPCTSTR)sError);
		return;
	}
	if (!Kademlia::CKademlia::IsRunning())
		return;
	CString sFile(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	sFile.AppendFormat(_T("temp-%lu-nodes.dat"), ::GetTickCount());
	bool bWritten = false;
	try {
		CFile file;
		if (file.Open(sFile, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite)) {
			file.Write(data.data(), (UINT)data.size());
			file.Close();
			bWritten = true;
		}
	} catch (CFileException *ex) {
		ex->Delete();
	}
	if (bWritten) {
		Kademlia::CKademlia::GetRoutingZone()->ReadFile(sFile);
		(void)_tremove(sFile);
		AddLogLine(false, _T("Kad contact list fetched (%u bytes)"), (unsigned)data.size());
	}
}
