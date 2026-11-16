// Pegaso: HTTP/HTTPS download engine.
//
// A web download is an ordinary CPartFile whose data is fetched by worker threads
// through WinHTTP instead of by ed2k sources. The file is split dynamically into
// segments, each served by its own connection using HTTP range requests.
//
// Threading: CWebDownload and CWebDownloadQueue are used from the main thread only.
// Worker threads never touch a CPartFile; they hand over data through a locked queue
// which the main thread drains in CWebDownload::Process().
#pragma once
#include <memory>

// Every web request of Pegaso starts here (a WinHTTP session handle). With a proxy in the
// preferences the request goes through it or fails: it never falls back to a direct connection.
LPVOID PegasoHttpSession();
#include <vector>

class CPartFile;
struct SWebShared;
struct SWebSegment;
struct SWebProbe;

class CWebDownload
{
public:
	explicit CWebDownload(const CString &sUrl);
	~CWebDownload();
	CWebDownload(const CWebDownload&) = delete;
	CWebDownload& operator=(const CWebDownload&) = delete;

	// Called about ten times per second. Writes received data into the file and keeps
	// the connections going; with bActive == false all connections are dropped.
	void	Process(CPartFile *pFile, bool bActive);

	const CString& GetUrl() const						{ return m_sUrl; }
	uint32	GetDatarate() const							{ return m_uDatarate; }	// bytes per second
	UINT	GetConnectionCount() const					{ return (UINT)m_segments.size(); }

private:
	void	Stop();
	bool	StartSegment(uint64 uStart, uint64 uEnd);
	void	StartSegments(CPartFile *pFile);

	CString	m_sUrl;
	std::shared_ptr<SWebShared> m_shared;
	std::vector<std::shared_ptr<SWebSegment> > m_segments;	// running connections
	DWORD	m_dwRetryTick;		// no new connections before this time
	DWORD	m_dwRateTick;
	uint64	m_uRateBytes;
	uint32	m_uDatarate;
	UINT	m_uFailures;		// consecutive failed connections
};

// Turns URLs into downloads: asks the server for size and name in the background,
// then creates the part file and puts it into the download queue.
class CWebDownloadQueue
{
public:
	static bool IsWebUrl(const CString &sUrl);
	// A single address whose path ends like a file worth downloading (archive, video, ...).
	// Used to decide whether an address copied to the clipboard is offered as a download.
	static bool LooksLikeFileUrl(const CString &sText);

	// Download rate of everything which is not a web download (ed2k sources), in bytes per
	// second. The web downloads get what this leaves of the configured download limit.
	void	SetOtherDatarate(uint32 uBytesPerSec)		{ m_uOtherDatarate = uBytesPerSec; }

	// Connections per file, preferences.ini: WebMaxConnections
	UINT	GetMaxConnections();
	void	SetMaxConnections(UINT uConnections);

	void	AddUrl(const CString &sUrl, int cat);	// also a series: see ExpandPattern
	static bool ExpandPattern(const CString &sUrl, std::vector<CString> &urls);
	void	Process();		// main thread, called from CDownloadQueue::Process()
	void	Clear();		// forget pending requests (shutdown)

private:
	std::vector<std::shared_ptr<SWebProbe> > m_probes;
	uint32	m_uOtherDatarate = 0;
	UINT	m_uMaxConnections = 0;	// 0: not read from the preferences yet
};

extern CWebDownloadQueue theWebDownloads;

// Fetches a small document (search answers, .torrent files) into memory. Any thread.
bool PegasoHttpGet(const CString &sUrl, std::vector<BYTE> &data, CString &sError, size_t uMaxSize);

// Marks a downloaded file as coming from the Internet (the "Zone.Identifier" stream), as
// browsers do: Windows then applies SmartScreen and its warnings when the file is opened.
void PegasoMarkFromInternet(const CString &sPath, const CString &sSourceUrl = CString());
