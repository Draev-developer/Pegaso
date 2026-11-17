// Pegaso: hybrid swarm. See HybridSwarm.h
#include "stdafx.h"
#include "emule.h"
#include "HybridSwarm.h"
#include "TorrentQueue.h"
#include "PartFile.h"
#include "DownloadQueue.h"
#include "Preferences.h"
#include "OtherFunctions.h"
#include "Opcodes.h"
#include "Log.h"
#include "PegasoStrings.h"
#include <set>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CHybridSwarm theHybrid;

namespace
{
	bool IsActive(const CPartFile *pFile)
	{
		return inSet(pFile->GetStatus(), PS_READY, PS_EMPTY) && !pFile->IsStopped();
	}

	// The download a link belongs to, while there still is something to download.
	CPartFile* DownloadOf(const uchar *hash)
	{
		if (theApp.downloadqueue == NULL)
			return NULL;
		CPartFile *pFile = theApp.downloadqueue->GetFileByID(hash);
		if (pFile == NULL || pFile->IsTorrent() || !pFile->IsPartFile() || inSet(pFile->GetStatus(), PS_COMPLETING, PS_COMPLETE, PS_ERROR))
			return NULL;
		return pFile;
	}
}

CHybridSwarm::CHybridSwarm()
	: m_bLoaded()
{
}

CString CHybridSwarm::IniPath()
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("hybrid.ini");
}

void CHybridSwarm::Load()
{
	if (m_bLoaded)
		return;
	m_bLoaded = true;
	const CString sPath(IniPath());
	const UINT uCount = ::GetPrivateProfileInt(_T("Hybrid"), _T("Count"), 0, sPath);
	for (UINT i = 0; i < uCount && i < 1000; ++i) {
		CString sKey;
		TCHAR szTorrent[128], szHash[64];
		sKey.Format(_T("Torrent%u"), i);
		::GetPrivateProfileString(_T("Hybrid"), sKey, _T(""), szTorrent, _countof(szTorrent), sPath);
		sKey.Format(_T("File%u"), i);
		::GetPrivateProfileString(_T("Hybrid"), sKey, _T(""), szHash, _countof(szHash), sPath);
		SLink link = {};
		link.fileIndex = -1;
		link.corruptionAtLink = _UI64_MAX;	// taken from the download when it is first seen
		link.dwAdded = ::GetTickCount();
		sKey.Format(_T("Own%u"), i);
		link.own = ::GetPrivateProfileInt(_T("Hybrid"), sKey, 0, sPath) != 0;
		if (szTorrent[0] != 0 && _tcslen(szHash) == 32 && strmd4(CStringA(szHash), link.hash))
			m_links[std::string(CStringA(szTorrent))] = link;
	}
}

void CHybridSwarm::Save() const
{
	const CString sPath(IniPath());
	::WritePrivateProfileString(_T("Hybrid"), NULL, NULL, sPath);	// start over
	CString sValue;
	sValue.Format(_T("%u"), (unsigned)m_links.size());
	::WritePrivateProfileString(_T("Hybrid"), _T("Count"), sValue, sPath);
	UINT i = 0;
	for (const auto &entry : m_links) {
		CString sKey;
		sKey.Format(_T("Torrent%u"), i);
		::WritePrivateProfileString(_T("Hybrid"), sKey, CString(entry.first.c_str()), sPath);
		sKey.Format(_T("File%u"), i);
		::WritePrivateProfileString(_T("Hybrid"), sKey, md4str(entry.second.hash), sPath);
		sKey.Format(_T("Own%u"), i);
		::WritePrivateProfileString(_T("Hybrid"), sKey, entry.second.own ? _T("1") : _T("0"), sPath);
		++i;
	}
}

bool CHybridSwarm::IsHelper(const std::string &id)
{
	Load();
	const std::map<std::string, SLink>::const_iterator it = m_links.find(id);
	return (it != m_links.end() && !it->second.own) || m_leaving.count(id) != 0;
}

bool CHybridSwarm::Link(CPartFile *pFile, const CString &sTorrent)
{
	Load();
	if (pFile == NULL || pFile->IsTorrent() || !pFile->IsPartFile())
		return false;
	for (const auto &entry : m_links)
		if (md4equ(entry.second.hash, pFile->GetFileHash())) {
			LogWarning(LOG_STATUSBAR, _T("\"%s\" already has a torrent as a source"), (LPCTSTR)pFile->GetFileName());
			return false;
		}
	CString s(sTorrent);
	s.Trim(_T(" \t\r\n\""));
	if (!CTorrentQueue::IsTorrentLink(s)) {
		LogError(LOG_STATUSBAR, _T("Not a magnet link nor a .torrent file: %s"), (LPCTSTR)s);
		return false;
	}
	if (!theTorrents.IsRunning())
		theTorrents.Start();
	if (!theTorrents.IsRunning()) {
		LogError(LOG_STATUSBAR, _T("BitTorrent is switched off in the options"));
		return false;
	}
	CString sDir(thePrefs.GetTempDir());
	sDir.TrimRight(_T("\\/"));	// the engine wants a clean path: no doubled separators
	sDir += _T("\\hybrid\\");
	::CreateDirectory(sDir, NULL);

	std::wstring error;
	std::string id;
	CTorrentEngine &engine = theTorrents.Engine();
	const bool bOk = (_tcsnicmp(s, _T("magnet:?"), 8) == 0)
		? engine.AddMagnet(std::string(CStringA(s)), error, std::wstring((LPCTSTR)sDir), &id)
		: engine.AddTorrentFile(std::wstring((LPCTSTR)s), error, std::wstring((LPCTSTR)sDir), &id);
	if (!bOk || id.empty()) {
		LogError(LOG_STATUSBAR, _T("Torrent not added as a source: %s"), error.c_str());
		return false;
	}
	SLink link = {};
	md4cpy(link.hash, pFile->GetFileHash());
	link.fileIndex = -1;
	link.corruptionAtLink = pFile->GetCorruptionLoss();
	link.dwAdded = ::GetTickCount();
	m_links[id] = link;
	engine.DeliverPieces(id, true);
	Save();
	AddLogLine(true, _T("Hybrid download: a torrent was added as a source of \"%s\""), (LPCTSTR)pFile->GetFileName());
	return true;
}

bool CHybridSwarm::LinkListed(CPartFile *pFile, const std::string &id)
{
	Load();
	if (pFile == NULL || pFile->IsTorrent() || !pFile->IsPartFile() || !theTorrents.IsRunning())
		return false;
	for (const auto &entry : m_links)
		if (md4equ(entry.second.hash, pFile->GetFileHash())) {
			LogWarning(LOG_STATUSBAR, _T("\"%s\" already has a torrent as a source"), (LPCTSTR)pFile->GetFileName());
			return false;
		}
	if (m_links.find(id) != m_links.end()) {
		LogWarning(LOG_STATUSBAR, _T("That torrent is the source of another download already"));
		return false;
	}
	SLink link = {};
	md4cpy(link.hash, pFile->GetFileHash());
	link.fileIndex = -1;
	link.corruptionAtLink = pFile->GetCorruptionLoss();
	link.dwAdded = ::GetTickCount();
	link.own = true;
	m_links[id] = link;
	theTorrents.Engine().DeliverPieces(id, true);
	Save();
	AddLogLine(true, _T("Hybrid download: a torrent of the list was added as a source of \"%s\""), (LPCTSTR)pFile->GetFileName());
	return true;
}

std::vector<CHybridSwarm::SCandidate> CHybridSwarm::Candidates(const CPartFile *pFile)
{
	Load();
	std::vector<SCandidate> out;
	if (pFile == NULL || !theTorrents.IsRunning())
		return out;
	const int64_t iSize = (int64_t)(uint64)pFile->GetFileSize();
	std::set<std::string> known;

	// the torrents of the list
	for (const STorrentStatus &st : theTorrents.GetStatus()) {
		known.insert(st.id);
		if (m_links.find(st.id) != m_links.end() || m_leaving.count(st.id) != 0)
			continue;
		bool bHasIt = false;
		for (const STorrentFile &file : theTorrents.Engine().GetFiles(st.id))
			bHasIt |= (file.size == iSize);
		if (!bHasIt)
			continue;
		SCandidate c;
		c.listed = true;
		c.id = st.id;
		c.text.Format(_T("%s  -  %s"), st.name.c_str(), GetPegasoString(PS_AT_INLIST));
		if (!st.finished && st.size > 0)
			c.text.AppendFormat(_T(" (%d %%)"), (int)(st.done * 100 / st.size));
		out.push_back(c);
	}

	// the .torrent files of the download folders
	CStringArray asFolders;
	asFolders.Add(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i)
		if (!thePrefs.GetCatPath(i).IsEmpty())
			asFolders.Add(thePrefs.GetCatPath(i));
	std::set<CString> seen;
	for (INT_PTR i = 0; i < asFolders.GetCount(); ++i) {
		CString sFolder(asFolders[i]);
		if (sFolder.Right(1) != _T("\\"))
			sFolder += _T('\\');
		CString sKey(sFolder);
		if (!seen.insert(sKey.MakeLower()).second)
			continue;
		CFileFind finder;
		int iLooked = 0;
		for (BOOL bFound = finder.FindFile(sFolder + _T("*.torrent")); bFound && iLooked < 500; ++iLooked) {
			bFound = finder.FindNextFile();
			if (finder.IsDirectory())
				continue;
			std::string id;
			std::wstring name;
			std::vector<int64_t> sizes;
			if (!CTorrentEngine::DescribeTorrentFile(std::wstring((LPCTSTR)finder.GetFilePath()), id, name, sizes) || !known.insert(id).second)
				continue;	// not a torrent, or one which is in the list (or listed here) already
			bool bHasIt = false;
			for (const int64_t size : sizes)
				bHasIt |= (size == iSize);
			if (!bHasIt)
				continue;
			SCandidate c;
			c.listed = false;
			c.path = finder.GetFilePath();
			c.text.Format(_T("%s  -  %s"), (LPCTSTR)finder.GetFileName(), GetPegasoString(PS_AT_FILE));
			out.push_back(c);
		}
	}
	return out;
}

void CHybridSwarm::Process(CTorrentEngine &engine, const std::vector<STorrentStatus> &status)
{
	Load();
	// The engine is polled from the first moments of the program, before the downloads have
	// been read from disk: a link whose download "is not there" then is not a finished one.
	if (!theApp.IsRunning())
		return;
	for (auto it = m_leaving.begin(); it != m_leaving.end();) {
		bool bThere = false;
		for (const STorrentStatus &st : status)
			bThere |= (st.id == *it);
		if (bThere)
			++it;
		else
			it = m_leaving.erase(it);
	}
	if (m_links.empty())
		return;
	const DWORD dwNow = ::GetTickCount();
	bool bChanged = false;

	for (auto it = m_links.begin(); it != m_links.end();) {
		const std::string &id = it->first;
		SLink &link = it->second;
		const STorrentStatus *pStatus = NULL;
		for (const STorrentStatus &st : status)
			if (st.id == id)
				pStatus = &st;
		CPartFile *pFile = DownloadOf(link.hash);

		if (pFile != NULL && link.corruptionAtLink == _UI64_MAX)
			link.corruptionAtLink = pFile->GetCorruptionLoss();

		LPCTSTR pszDrop = NULL;
		if (pFile == NULL) {
			// finished, cancelled or broken: the helper has done its job
			pszDrop = _T("");
		} else if (pStatus == NULL) {
			if (dwNow - link.dwAdded > SEC2MS(60))
				pszDrop = link.own ? _T("the torrent is no longer in the list") : _T("the torrent could not be added");
		} else if (pStatus->state == TS_ERROR) {
			pszDrop = _T("the torrent reported an error");
		} else if (pFile->GetCorruptionLoss() > link.corruptionAtLink && link.fileIndex >= 0) {
			pszDrop = _T("data failed the eD2K hash check - it is not the same file");
		} else if (link.fileIndex < 0) {
			const std::vector<STorrentFile> files(engine.GetFiles(id));
			if (!files.empty()) {	// else: magnet link, file list not known yet
				for (const STorrentFile &file : files)
					if ((uint64)file.size == (uint64)pFile->GetFileSize() && link.fileIndex < 0) {
						link.fileIndex = file.index;
						link.offset = file.offset;
					}
				if (link.fileIndex < 0)
					pszDrop = _T("it has no file of the same size");
				else
					link.dwLastRanges = link.dwLastRedeliver = 0;
			}
		}

		if (pszDrop != NULL) {
			if (*pszDrop != 0)
				LogWarning(LOG_STATUSBAR, _T("Hybrid download: torrent dropped as a source of \"%s\": %s")
					, pFile ? (LPCTSTR)pFile->GetFileName() : _T("?"), pszDrop);
			if (pFile)
				pFile->SetHybridPeers(-1);
			engine.DeliverPieces(id, false);
			if (!link.own) {	// a torrent of the list goes on being what it was
				engine.Remove(id, true);
				m_leaving.insert(id);	// stays hidden until the engine has let go of it
			}
			it = m_links.erase(it);
			bChanged = true;
			continue;
		}

		engine.DeliverPieces(id, true);	// the engine forgets it when it is restarted
		if (pFile != NULL && pStatus != NULL) {
			pFile->SetHybridPeers(pStatus->state == TS_PAUSED ? 0 : pStatus->peers);
			const bool bActive = IsActive(pFile);
			if (link.own) {
				// it downloads what it downloads; what it has of the file is handed over, a
				// stretch at a time (a whole file read at once would not fit in memory)
				if (link.fileIndex >= 0 && bActive && (link.dwLastRanges == 0 || dwNow - link.dwLastRanges >= SEC2MS(3))) {
					std::vector<std::pair<int64_t, int64_t> > ranges;
					const CList<Gap_Struct> &gaps = pFile->GetGapList();
					for (POSITION pos = gaps.GetHeadPosition(); pos != NULL;) {
						const Gap_Struct &gap = gaps.GetNext(pos);
						ranges.emplace_back(link.offset + (int64_t)gap.start, link.offset + (int64_t)gap.end + 1);
					}
					engine.RedeliverRanges(id, ranges, 64 * 1024 * 1024);
					link.dwLastRanges = dwNow ? dwNow : 1;
				}
				++it;
				continue;
			}
			if (bActive == link.paused) {
				link.paused = !bActive;
				if (bActive)
					engine.Resume(id);
				else
					engine.Pause(id);
			}
			// ask the torrent only for what the download still lacks
			if (link.fileIndex >= 0 && bActive && (link.dwLastRanges == 0 || dwNow - link.dwLastRanges >= SEC2MS(3))) {
				std::vector<std::pair<int64_t, int64_t> > ranges;
				const CList<Gap_Struct> &gaps = pFile->GetGapList();
				for (POSITION pos = gaps.GetHeadPosition(); pos != NULL;) {
					const Gap_Struct &gap = gaps.GetNext(pos);
					ranges.emplace_back(link.offset + (int64_t)gap.start, link.offset + (int64_t)gap.end + 1);
				}
				// now and then also what the torrent has but did not get through (restart, write error)
				const bool bRedeliver = link.dwLastRedeliver == 0 || dwNow - link.dwLastRedeliver >= SEC2MS(30);
				engine.SetWantedRanges(id, ranges, bRedeliver);
				link.dwLastRanges = dwNow ? dwNow : 1;
				if (bRedeliver)
					link.dwLastRedeliver = dwNow ? dwNow : 1;
			}
		}
		++it;
	}
	if (bChanged)
		Save();

	// finished pieces go into the downloads - only where data is still missing, so that
	// nothing which has passed the ed2k check already is written over
	std::vector<STorrentPiece> pieces;
	engine.PollPieces(pieces);
	for (const STorrentPiece &piece : pieces) {
		const auto it = m_links.find(piece.id);
		if (it == m_links.end() || it->second.fileIndex < 0)
			continue;
		CPartFile *pFile = DownloadOf(it->second.hash);
		if (pFile == NULL)
			continue;
		const int64_t iStart = piece.offset - it->second.offset;	// position inside the file; may be negative
		const int64_t iEnd = iStart + (int64_t)piece.data.size();	// exclusive
		std::vector<std::pair<uint64, uint64> > todo;	// inclusive ranges, as the gap list has them
		const CList<Gap_Struct> &gaps = pFile->GetGapList();
		for (POSITION pos = gaps.GetHeadPosition(); pos != NULL;) {
			const Gap_Struct &gap = gaps.GetNext(pos);
			const int64_t iFrom = max(iStart, (int64_t)gap.start);
			const int64_t iTo = min(iEnd - 1, (int64_t)gap.end);
			if (iFrom <= iTo)
				todo.emplace_back((uint64)iFrom, (uint64)iTo);
		}
		for (const std::pair<uint64, uint64> &range : todo) {
			const uint32 uLen = (uint32)(range.second - range.first + 1);
			BYTE *pData = new BYTE[uLen];
			memcpy(pData, piece.data.data() + ((int64_t)range.first - iStart), uLen);
			pFile->WriteWebData(range.first, pData, uLen);	// takes the buffer over
		}
	}
}
