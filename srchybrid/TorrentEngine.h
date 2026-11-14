// Pegaso: BitTorrent engine (libtorrent-rasterbar behind a plain C++ interface).
//
// This header is deliberately free of MFC and of libtorrent types: TorrentEngine.cpp is
// compiled on its own (C++17, no precompiled header) and nothing of Boost or libtorrent
// leaks into the rest of the application. Strings are UTF-16 for text and paths, and
// plain ASCII/UTF-8 for links and identifiers.
//
// All methods are to be called from one thread (the main thread). libtorrent does its
// work on its own threads; results are collected with Poll().
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct STorrentSettings
{
	std::wstring savePath;			// where finished and running downloads are stored
	std::wstring stateDir;			// session state and resume data
	std::string	userAgent;
	int		listenPort = 6881;
	int		downloadLimit = 0;		// bytes per second, 0 = unlimited
	int		uploadLimit = 0;
	bool	requireEncryption = false;	// only encrypted peer connections
};

enum ETorrentState
{
	TS_METADATA,		// magnet link: waiting for the torrent description
	TS_CHECKING,
	TS_DOWNLOADING,
	TS_SEEDING,
	TS_PAUSED,
	TS_ERROR
};

struct STorrentStatus
{
	std::string	id;				// info-hash, hexadecimal
	std::wstring name;
	std::wstring error;
	int64_t	size = 0;			// wanted bytes
	int64_t	done = 0;
	int		downloadRate = 0;	// bytes per second
	int		uploadRate = 0;
	int		peers = 0;
	int		seeds = 0;
	ETorrentState state = TS_METADATA;
};

struct STorrentFile
{
	int		index = 0;			// position inside the torrent
	std::wstring path;
	int64_t	size = 0;
	bool	wanted = true;
};

enum ETorrentEvent
{
	TE_ADDED,
	TE_METADATA,		// name and size are known now
	TE_FINISHED,
	TE_REMOVED,
	TE_ERROR,			// 'text' explains; the torrent may be gone (id empty) or stopped
	TE_INFO				// engine level message, e.g. the listen port
};

struct STorrentEvent
{
	ETorrentEvent kind;
	std::string	id;
	std::wstring name;
	std::wstring text;
};

class CTorrentEngine
{
public:
	CTorrentEngine();
	~CTorrentEngine();
	CTorrentEngine(const CTorrentEngine&) = delete;
	CTorrentEngine& operator=(const CTorrentEngine&) = delete;

	bool	Start(const STorrentSettings &settings, std::wstring &error);
	void	Stop();		// saves the state of all torrents; may take a few seconds
	bool	IsRunning() const;

	static bool IsTorrentMagnet(const std::string &uri);	// magnet link with a BitTorrent hash
	bool	AddMagnet(const std::string &uri, std::wstring &error);
	bool	AddTorrentFile(const std::wstring &path, std::wstring &error);

	void	Pause(const std::string &id);
	void	Resume(const std::string &id);
	void	Remove(const std::string &id, bool deleteFiles);
	void	SetLimits(int downloadLimit, int uploadLimit);	// bytes per second, 0 = unlimited

	// The files of a torrent; empty while the description of a magnet link has not arrived.
	// Link files (*.url) are deselected by default when a torrent is added.
	std::vector<STorrentFile> GetFiles(const std::string &id);
	void	SetFilesWanted(const std::string &id, const std::vector<std::pair<int, bool> > &wanted);

	void	Poll(std::vector<STorrentEvent> &events);	// collect what happened since the last call
	std::vector<STorrentStatus> GetStatus();

private:
	struct Impl;
	std::unique_ptr<Impl> m;
};
