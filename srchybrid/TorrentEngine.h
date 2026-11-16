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
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct STorrentSettings
{
	std::wstring savePath;			// where finished and running downloads are stored
	std::wstring stateDir;			// session state and resume data
	std::string	userAgent;
	std::string	bindAddress;			// IPv4 address of the only adapter to use; empty = any
	int		listenPort = 6881;
	int		downloadLimit = 0;		// bytes per second, 0 = unlimited
	int		uploadLimit = 0;
	bool	requireEncryption = false;	// only encrypted peer connections
	bool	noRootFolder = false;		// the files of a torrent are saved without the folder it brings
	bool	ipv6 = true;				// false: no IPv6 at all, it would go round a VPN or a proxy
	// The proxy of the preferences: everything goes through it or does not go at all.
	int		proxyType = 0;				// 0 none, 4 SOCKS4, 5 SOCKS5, 1 HTTP
	std::string	proxyHost;
	int		proxyPort = 0;
	std::string	proxyUser;				// empty: no authentication
	std::string	proxyPassword;
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
	int64_t	totalSize = 0;		// all files, whatever is selected; 0 while not known
	int64_t	done = 0;
	int		downloadRate = 0;	// bytes per second
	int		uploadRate = 0;
	int		peers = 0;
	int		seeds = 0;
	ETorrentState state = TS_METADATA;
	bool	finished = false;	// everything wanted is there (also true while paused)
	bool	webSeeds = false;	// the torrent has web servers among its sources
	int64_t	uploaded = 0;		// payload sent to others, all sessions
	int		piecesDone = 0;
	// Missing data as [start, end) ranges on a scale of 0..mapSize: the wanted pieces put
	// one after another. mapSize is 0 while the pieces are not known.
	int64_t	mapSize = 0;
	std::vector<std::pair<int64_t, int64_t> > gaps;
};

struct STorrentFile
{
	int		index = 0;			// position inside the torrent
	std::wstring path;			// as it is now, inside the folder of the torrent; absolute if it was moved out of it
	std::wstring fullPath;		// where the file is on disk
	int64_t	size = 0;
	int64_t	offset = 0;			// where the file starts inside the torrent
	bool	wanted = true;
};

// A finished piece, read back from disk (hybrid downloads).
struct STorrentPiece
{
	std::string	id;
	int64_t	offset = 0;			// position inside the torrent
	std::vector<char> data;
};

enum ETorrentEvent
{
	TE_ADDED,
	TE_METADATA,		// name and size are known now
	TE_FINISHED,		// 'text' is "downloaded" if something of it arrived in this session
	TE_REMOVED,
	TE_MOVED,			// its files are in another folder now ('text')
	TE_MOVE_FAILED,		// they could not be taken there ('text' says why): a file may be in use for a moment
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

// What a new torrent is made of.
struct STorrentCreate
{
	std::wstring path;					// a file or a folder
	std::vector<std::string> trackers;	// optional
	bool	privateTorrent = false;		// only through its trackers: no DHT, no peer exchange
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
	// savePath: where this torrent keeps its data, if not the common folder.
	// pId: receives the identifier - also when the torrent is refused for being in the list already.
	bool	AddMagnet(const std::string &uri, std::wstring &error, const std::wstring &savePath = std::wstring(), std::string *pId = nullptr);
	bool	AddTorrentFile(const std::wstring &path, std::wstring &error, const std::wstring &savePath = std::wstring(), std::string *pId = nullptr);
	// Makes the torrent of a file or folder and writes it to torrentPath; magnet receives its
	// link. Everything is read to work out the hashes, which may take long: progress is told
	// in pieces. Needs no engine and may be called from any thread.
	static bool CreateTorrent(const STorrentCreate &what, const std::wstring &torrentPath, std::string &magnet, std::wstring &error
		, const std::function<void(int done, int total)> &progress);
	// Adds a torrent whose data is known to be complete (it has just been made): it is
	// offered at once, without checking the data again. dataFolder: where its data is.
	bool	AddSeed(const std::wstring &torrentPath, const std::wstring &dataFolder, std::wstring &error, std::string *pId = nullptr);
	// A web server which has the files of the torrent (BEP 19 web seed).
	bool	AddWebSeed(const std::string &id, const std::string &url);

	// Hybrid downloads: a torrent as one more source of a download which is managed elsewhere.
	// Its finished pieces are read back and handed out by PollPieces().
	void	DeliverPieces(const std::string &id, bool deliver);
	// Only the pieces which touch these [start, end) ranges (positions inside the torrent) are
	// downloaded; with redeliver, the ones which are there already are handed out again.
	void	SetWantedRanges(const std::string &id, const std::vector<std::pair<int64_t, int64_t> > &ranges, bool redeliver);
	void	PollPieces(std::vector<STorrentPiece> &pieces);

	void	Pause(const std::string &id);
	// true once a torrent which was paused has let go of its files: they can be moved then
	bool	IsReleased(const std::string &id) const;
	void	Resume(const std::string &id);
	void	Remove(const std::string &id, bool deleteFiles);
	void	SetLimits(int downloadLimit, int uploadLimit);	// bytes per second, 0 = unlimited
	// Fetches the pieces of the torrent in order, the beginning of this file first of all and
	// its end next (players read the index of many formats from there). Gives back how many
	// bytes of the file are there from its beginning on; -1 if there is no such file.
	int64_t	PlayFile(const std::string &id, int index);
	// Takes all the files of the torrent to another folder; a file which is already there is kept.
	void	MoveStorage(const std::string &id, const std::wstring &folder);
	// A torrent of another program which uses the same engine, with what that program knew of
	// it (its "fastresume" file; may be empty): where the files are, which are wanted, what is there.
	bool	AddFromResume(const std::wstring &resumePath, const std::wstring &torrentPath, std::wstring &error);
	void	SetNoRootFolder(bool on);	// for the torrents added from now on
	void	PauseSession(bool pause);	// stops all network activity without touching the torrents

	// The files of a torrent; empty while the description of a magnet link has not arrived.
	// Link files (*.url) are deselected by default when a torrent is added.
	std::vector<STorrentFile> GetFiles(const std::string &id);
	void	SetFilesWanted(const std::string &id, const std::vector<std::pair<int, bool> > &wanted);
	// Gives one file of a torrent another name or place (newPath: complete path). The engine
	// moves the file itself and goes on offering it; the call returns when that is done.
	bool	RenameFile(const std::string &id, int index, const std::wstring &newPath, std::wstring &error);

	void	Poll(std::vector<STorrentEvent> &events);	// collect what happened since the last call
	std::vector<STorrentStatus> GetStatus();

private:
	struct Impl;
	std::unique_ptr<Impl> m;
};
