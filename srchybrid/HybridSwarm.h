// Pegaso: hybrid swarm - one download fed by eMule sources and by a torrent at once.
//
// A download of the eMule networks (which may also have web sources) can be given a torrent
// that carries the very same file: a magnet link or a .torrent file with a file of exactly
// that size. The torrent then works as one more source:
//
//   - it downloads only the parts which the download still lacks, and stops asking for a
//     piece as soon as eMule sources (or the web) have delivered that data;
//   - every piece it finishes (already checked by BitTorrent's own hashes) is copied into
//     the download, where it goes through the ed2k part hashes like any other data;
//   - if data from it fails that check, the torrent is not the same file: it is dropped.
//
// The helper torrent has no row of its own; its data lives in a "hybrid" folder inside the
// temporary directory and is removed when the download completes or is cancelled. The links
// survive a restart (config\hybrid.ini).
//
// Main thread only.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "TorrentEngine.h"

class CPartFile;

class CHybridSwarm
{
public:
	CHybridSwarm();

	// sTorrent: magnet link or path of a .torrent file. Errors are logged.
	bool	Link(CPartFile *pFile, const CString &sTorrent);
	// Is this torrent the helper of a download (and therefore not a download of its own)?
	bool	IsHelper(const std::string &id);
	// About once per second, with the fresh status of all torrents.
	void	Process(CTorrentEngine &engine, const std::vector<STorrentStatus> &status);

private:
	struct SLink
	{
		uchar	hash[16];			// the ed2k download
		int		fileIndex;			// file of the torrent; -1 until the file list is known
		int64_t	offset;				// where that file starts inside the torrent
		uint64	corruptionAtLink;	// corruption counter of the download when the link was made
		DWORD	dwAdded;
		DWORD	dwLastRanges;
		DWORD	dwLastRedeliver;
		bool	paused;
	};

	void	Load();
	void	Save() const;
	static CString IniPath();

	std::map<std::string, SLink> m_links;	// by torrent id
	std::set<std::string> m_leaving;		// dropped helpers which the engine is still removing
	bool	m_bLoaded;
};

extern CHybridSwarm theHybrid;
