// Pegaso: glue between the application and the BitTorrent engine (TorrentEngine.h).
// Main thread only.
#pragma once
#include <map>
#include "TorrentEngine.h"

class CTorrentQueue
{
public:
	CTorrentQueue();

	// preferences.ini, section [eMule]:
	//   TorrentEnabled (1), TorrentPort (6881), TorrentRequireEncryption (0)
	void	Start();
	void	Stop();
	bool	IsRunning() const							{ return m_engine.IsRunning(); }

	// Magnet link with a BitTorrent hash, or the path of a .torrent file.
	static bool IsTorrentLink(const CString &sLink);
	// Returns false if the link is not a torrent at all; errors are logged.
	bool	AddLink(const CString &sLink);

	void	Process();									// called from CDownloadQueue::Process()
	uint32	GetDatarate() const							{ return m_uDatarate; }	// download, bytes per second
	const std::vector<STorrentStatus>& GetStatus() const { return m_status; }

	void	Pause(const std::string &id)				{ m_engine.Pause(id); }
	void	Resume(const std::string &id)				{ m_engine.Resume(id); }
	void	Remove(const std::string &id, bool bDeleteFiles) { m_engine.Remove(id, bDeleteFiles); }

private:
	void	SyncDownloadList();
	void	RemoveProxies();

	CTorrentEngine m_engine;
	std::map<std::string, class CPartFile*> m_proxies;	// the rows of the download list
	std::vector<STorrentStatus> m_status;	// refreshed once per second
	DWORD	m_dwLastPoll;
	DWORD	m_dwLastReport;
	uint32	m_uDatarate;
	bool	m_bStartTried;
};

extern CTorrentQueue theTorrents;
