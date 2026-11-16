// Pegaso: HTTP/HTTPS download engine. See WebDownload.h for the design notes.
#include "stdafx.h"
#include <winhttp.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <algorithm>
#include "emule.h"
#include "WebDownload.h"
#include "PartFile.h"
#include "DownloadQueue.h"
#include "Preferences.h"
#include "OtherFunctions.h"
#include "StringConversion.h"
#include "MD4.h"
#include "Log.h"
#include "NetGuard.h"
#include "PegasoScan.h"

#pragma comment(lib, "winhttp.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CWebDownloadQueue theWebDownloads;

namespace
{
	const UINT		WEB_DEF_CONNECTIONS = 8;				// per file; preferences.ini: WebMaxConnections
	const UINT		WEB_MAX_CONNECTIONS = 16;
	const uint64	WEB_MIN_SPLIT = 1024 * 1024;			// do not split a segment into pieces smaller than this
	const uint32	WEB_READ_SIZE = 64 * 1024;
	const uint64	WEB_MAX_QUEUED = 16 * 1024 * 1024;		// workers wait when this much data is pending
	const int		WEB_MAX_SERIES = 1000;					// addresses one series may stand for
	const UINT		WEB_MAX_PROBES = 4;						// simultaneous size/name lookups
	const uint64	WEB_SIZE_UNKNOWN = _UI64_MAX;

	// ---------------------------------------------------------------- WinHTTP helpers

	struct SHttpRequest
	{
		HINTERNET hSession = NULL;
		HINTERNET hConnect = NULL;
		HINTERNET hRequest = NULL;

		~SHttpRequest()
		{
			if (hRequest)
				::WinHttpCloseHandle(hRequest);
			if (hConnect)
				::WinHttpCloseHandle(hConnect);
			if (hSession)
				::WinHttpCloseHandle(hSession);
		}
	};

	CString WinHttpError(LPCTSTR pszWhat)
	{
		const DWORD dwErr = ::GetLastError();
		CString s;
		switch (dwErr) {
		case ERROR_WINHTTP_TIMEOUT:
			s.Format(_T("%s: timeout"), pszWhat);
			break;
		case ERROR_WINHTTP_NAME_NOT_RESOLVED:
			s.Format(_T("%s: host name not found"), pszWhat);
			break;
		case ERROR_WINHTTP_CANNOT_CONNECT:
			s.Format(_T("%s: cannot connect"), pszWhat);
			break;
		case ERROR_WINHTTP_CONNECTION_ERROR:
			s.Format(_T("%s: connection lost"), pszWhat);
			break;
		case ERROR_WINHTTP_SECURE_FAILURE:
			s.Format(_T("%s: TLS certificate rejected"), pszWhat);
			break;
		default:
			s.Format(_T("%s: error %u"), pszWhat, dwErr);
		}
		return s;
	}

	// Sends a GET request and waits for the response headers.
	// pszRange is the value of the Range header or NULL.
	bool SendGet(const CString &sUrl, LPCTSTR pszRange, SHttpRequest &req, DWORD &dwStatus, CString &sError)
	{
		URL_COMPONENTS uc = {};
		uc.dwStructSize = sizeof uc;
		TCHAR szHost[256];
		uc.lpszHostName = szHost;
		uc.dwHostNameLength = _countof(szHost);
		uc.dwUrlPathLength = (DWORD)-1;		// path and query: pointer into sUrl
		uc.dwExtraInfoLength = (DWORD)-1;
		if (!::WinHttpCrackUrl(sUrl, (DWORD)sUrl.GetLength(), 0, &uc) || uc.lpszUrlPath == NULL) {
			sError = _T("invalid URL");
			return false;
		}
		if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) {
			sError = _T("unsupported protocol");
			return false;
		}
		// lpszUrlPath points at the path; the query string follows it directly, the fragment is not sent
		CString sObject(uc.lpszUrlPath);
		int iFragment = sObject.Find(_T('#'));
		if (iFragment >= 0)
			sObject.Truncate(iFragment);
		if (sObject.IsEmpty())
			sObject = _T('/');

		req.hSession = ::WinHttpOpen(PEGASO_NAME _T("/") PEGASO_VERSION_NAME, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
			, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (req.hSession == NULL) {
			sError = WinHttpError(_T("session"));
			return false;
		}
		::WinHttpSetTimeouts(req.hSession, 15000, 15000, 30000, 30000);
		DWORD dwProtocols = WINHTTP_PROTOCOL_FLAG_HTTP2;	// optional, older systems ignore it
		::WinHttpSetOption(req.hSession, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &dwProtocols, sizeof dwProtocols);

		req.hConnect = ::WinHttpConnect(req.hSession, szHost, uc.nPort, 0);
		if (req.hConnect == NULL) {
			sError = WinHttpError(_T("connect"));
			return false;
		}
		req.hRequest = ::WinHttpOpenRequest(req.hConnect, L"GET", sObject, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES
			, (uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) | WINHTTP_FLAG_ESCAPE_DISABLE);
		if (req.hRequest == NULL) {
			sError = WinHttpError(_T("request"));
			return false;
		}

		// Compressed transfer encodings would make byte offsets meaningless.
		CString sHeaders(_T("Accept-Encoding: identity\r\n"));
		if (pszRange != NULL)
			sHeaders.AppendFormat(_T("Range: %s\r\n"), pszRange);
		if (!::WinHttpSendRequest(req.hRequest, sHeaders, (DWORD)sHeaders.GetLength(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
			|| !::WinHttpReceiveResponse(req.hRequest, NULL))
		{
			sError = WinHttpError(_T("request"));
			return false;
		}
		DWORD dwSize = sizeof dwStatus;
		if (!::WinHttpQueryHeaders(req.hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX
			, &dwStatus, &dwSize, WINHTTP_NO_HEADER_INDEX))
		{
			sError = WinHttpError(_T("response"));
			return false;
		}
		return true;
	}

	bool GetHeader(HINTERNET hRequest, DWORD dwInfo, CString &sValue)
	{
		TCHAR szBuf[2048];
		DWORD dwSize = sizeof szBuf;
		if (!::WinHttpQueryHeaders(hRequest, dwInfo, WINHTTP_HEADER_NAME_BY_INDEX, szBuf, &dwSize, WINHTTP_NO_HEADER_INDEX)) {
			sValue.Empty();
			return false;
		}
		sValue = szBuf;
		return true;
	}

	bool ParseUInt64(LPCTSTR psz, uint64 &uValue)
	{
		if (*psz < _T('0') || *psz > _T('9'))
			return false;
		LPTSTR pEnd;
		errno = 0;
		uValue = _tcstoui64(psz, &pEnd, 10);
		return errno == 0 && pEnd != psz;
	}

	// "bytes 100-199/1000" -> first = 100, total = 1000 (WEB_SIZE_UNKNOWN for "/*")
	bool ParseContentRange(const CString &sValue, uint64 &uFirst, uint64 &uTotal)
	{
		CString s(sValue);
		s.Trim();
		if (_tcsnicmp(s, _T("bytes"), 5) != 0)
			return false;
		s = s.Mid(5).TrimLeft();
		int iSlash = s.Find(_T('/'));
		if (iSlash < 0 || !ParseUInt64(s, uFirst))
			return false;
		const CString &sTotal(s.Mid(iSlash + 1).Trim());
		if (sTotal == _T("*"))
			uTotal = WEB_SIZE_UNKNOWN;
		else if (!ParseUInt64(sTotal, uTotal))
			return false;
		return true;
	}

	// ---------------------------------------------------------------- file names

	CString SanitizeFileName(const CString &sName)
	{
		CString s(sName);
		s.Trim();
		for (int i = 0; i < s.GetLength(); ++i)
			if (s[i] < _T(' ') || _tcschr(_T("\\/:*?\"<>|"), s[i]) != NULL)
				s.SetAt(i, _T('_'));
		s.TrimRight(_T(". "));	// not allowed at the end of a Windows file name
		if (s.GetLength() > 200)
			s = s.Left(200);
		return s;
	}

	CString PercentDecodeUtf8(const CString &sIn)
	{
		CStringA sBytes;
		for (int i = 0; i < sIn.GetLength(); ++i) {
			const TCHAR c = sIn[i];
			unsigned uByte;
			if (c == _T('%') && i + 2 < sIn.GetLength() + 0 && _stscanf(sIn.Mid(i + 1, 2), _T("%2x"), &uByte) == 1) {
				sBytes += (char)uByte;
				i += 2;
			} else
				sBytes += CStringA(CString(c));	// plain ASCII in practice
		}
		return CString(OptUtf8ToStr(sBytes));
	}

	// Content-Disposition: attachment; filename="a.zip"; filename*=UTF-8''a%20b.zip
	CString FileNameFromDisposition(const CString &sValue)
	{
		CString sLower(sValue);
		sLower.MakeLower();
		int i = sLower.Find(_T("filename*="));
		if (i >= 0) {
			CString s(sValue.Mid(i + 10));
			int iEnd = s.Find(_T(';'));
			if (iEnd >= 0)
				s.Truncate(iEnd);
			s.Trim(_T(" \""));
			int iQuote = s.Find(_T("''"));
			if (iQuote >= 0)
				return PercentDecodeUtf8(s.Mid(iQuote + 2));
		}
		i = sLower.Find(_T("filename="));
		if (i >= 0) {
			CString s(sValue.Mid(i + 9));
			s.TrimLeft();
			if (!s.IsEmpty() && s[0] == _T('"')) {
				int iEnd = s.Find(_T('"'), 1);
				s = (iEnd > 0) ? s.Mid(1, iEnd - 1) : s.Mid(1);
			} else {
				int iEnd = s.Find(_T(';'));
				if (iEnd >= 0)
					s.Truncate(iEnd);
			}
			return s;
		}
		return CString();
	}

	CString FileNameFromUrl(const CString &sUrl)
	{
		CString s(sUrl);
		int i = s.FindOneOf(_T("?#"));
		if (i >= 0)
			s.Truncate(i);
		i = s.Find(_T("://"));
		if (i >= 0)
			s = s.Mid(i + 3);
		i = s.ReverseFind(_T('/'));
		if (i < 0)
			return CString();	// only a host name
		return PercentDecodeUtf8(s.Mid(i + 1));
	}
}

// ---------------------------------------------------------------- shared state

struct SWebChunk
{
	uint64	start;
	BYTE	*data;		// new BYTE[len]
	uint32	len;
};

// One connection. 'pos', 'end', 'finished' and 'failed' are guarded by SWebShared::mtx.
struct SWebSegment
{
	uint64	start;
	uint64	end;		// inclusive; the owner may lower it to hand the tail to another connection
	uint64	pos;		// next byte to be queued
	uint64	snapPos;	// main thread only: 'pos' when the queue was drained last time
	bool	finished;	// the worker is done with this segment
	bool	failed;		// ...but did not reach 'end'
	CString	error;
};

struct SWebShared
{
	std::mutex	mtx;
	std::deque<SWebChunk> chunks;
	uint64		queuedBytes = 0;
	CString		fatalError;				// set once: the download cannot continue (e.g. the file changed)

	CString		url;					// immutable after creation
	uint64		fileSize = 0;			// immutable after creation
	std::atomic<bool>	abort{false};
	std::atomic<bool>	rangesOk{false};	// a 206 response was seen
	std::atomic<bool>	noRanges{false};	// the server ignores range requests

	~SWebShared()
	{
		for (const SWebChunk &chunk : chunks)
			delete[] chunk.data;
	}
};

namespace
{
	// Speed limit shared by all web downloads (token bucket).
	std::atomic<uint32> g_uRateLimit{0};	// bytes per second, 0 = unlimited
	std::mutex	g_mtxRate;
	double		g_dTokens = 0;
	DWORD		g_dwTokenTick = 0;

	// Blocks until 'uBytes' may be received under the current speed limit.
	void Throttle(SWebShared &sh, uint32 uBytes)
	{
		while (!sh.abort) {
			const uint32 uLimit = g_uRateLimit;
			if (uLimit == 0)
				return;
			{
				std::lock_guard<std::mutex> lock(g_mtxRate);
				const DWORD dwNow = ::GetTickCount();
				g_dTokens += (double)(dwNow - g_dwTokenTick) * uLimit / 1000.0;
				g_dwTokenTick = dwNow;
				const double dMax = (double)max(uLimit, uBytes);	// at most one second worth of burst
				if (g_dTokens > dMax)
					g_dTokens = dMax;
				if (g_dTokens >= uBytes) {
					g_dTokens -= uBytes;
					return;
				}
			}
			::Sleep(20);
		}
	}

	void FinishSegment(SWebShared &sh, SWebSegment &seg, const CString &sError, bool bFatal = false)
	{
		std::lock_guard<std::mutex> lock(sh.mtx);
		seg.finished = true;
		seg.failed = seg.pos <= seg.end;
		seg.error = sError;
		if (bFatal && sh.fatalError.IsEmpty())
			sh.fatalError = sError;
	}

	void WebWorker(std::shared_ptr<SWebShared> pShared, std::shared_ptr<SWebSegment> pSeg)
	{
		SWebShared &sh = *pShared;
		SWebSegment &seg = *pSeg;

		uint64 uEnd;
		{
			std::lock_guard<std::mutex> lock(sh.mtx);
			uEnd = seg.end;
		}
		CString sRange;
		sRange.Format(_T("bytes=%I64u-%I64u"), seg.start, uEnd);

		SHttpRequest req;
		DWORD dwStatus;
		CString sError;
		if (!SendGet(sh.url, sRange, req, dwStatus, sError)) {
			FinishSegment(sh, seg, sError);
			return;
		}

		uint64 uSkip = 0;	// bytes to throw away before the wanted data starts
		CString sValue;
		if (dwStatus == HTTP_STATUS_PARTIAL_CONTENT) {
			uint64 uFirst, uTotal;
			if (!GetHeader(req.hRequest, WINHTTP_QUERY_CONTENT_RANGE, sValue) || !ParseContentRange(sValue, uFirst, uTotal) || uFirst != seg.start) {
				FinishSegment(sh, seg, _T("unexpected Content-Range in the server response"));
				return;
			}
			if (uTotal != WEB_SIZE_UNKNOWN && uTotal != sh.fileSize) {
				FinishSegment(sh, seg, _T("the file on the server has changed (different size)"), true);
				return;
			}
			sh.rangesOk = true;
		} else if (dwStatus == HTTP_STATUS_OK) {
			// The whole file is being sent: usable, but only with a single connection.
			uint64 uLength;
			if (GetHeader(req.hRequest, WINHTTP_QUERY_CONTENT_LENGTH, sValue) && ParseUInt64(sValue, uLength) && uLength != sh.fileSize) {
				FinishSegment(sh, seg, _T("the file on the server has changed (different size)"), true);
				return;
			}
			sh.noRanges = true;
			uSkip = seg.start;
		} else {
			sError.Format(_T("HTTP status %u"), dwStatus);
			// 4xx other than "too many requests" will not get better by retrying
			FinishSegment(sh, seg, sError, dwStatus >= 400 && dwStatus < 500 && dwStatus != 408 && dwStatus != 429);
			return;
		}

		std::vector<BYTE> buffer(WEB_READ_SIZE);
		while (!sh.abort) {
			uint32 uWant = WEB_READ_SIZE;
			const uint32 uLimit = g_uRateLimit;
			if (uLimit != 0)	// small reads keep a low limit smooth
				uWant = max(2048u, min(WEB_READ_SIZE, uLimit / 8));
			if (uSkip == 0)
				Throttle(sh, uWant);

			// do not let the main thread fall behind without bound
			for (;;) {
				{
					std::lock_guard<std::mutex> lock(sh.mtx);
					if (sh.queuedBytes < WEB_MAX_QUEUED || seg.pos > seg.end)
						break;
				}
				if (sh.abort)
					break;
				::Sleep(50);
			}
			if (sh.abort)
				break;

			DWORD dwRead = 0;
			if (!::WinHttpReadData(req.hRequest, buffer.data(), uWant, &dwRead)) {
				FinishSegment(sh, seg, WinHttpError(_T("read")));
				return;
			}
			if (dwRead == 0) {
				FinishSegment(sh, seg, _T("the server closed the connection early"));
				return;
			}

			const BYTE *pData = buffer.data();
			if (uSkip > 0) {
				const DWORD dwDrop = (DWORD)min((uint64)dwRead, uSkip);
				uSkip -= dwDrop;
				pData += dwDrop;
				dwRead -= dwDrop;
				if (dwRead == 0)
					continue;
			}

			std::lock_guard<std::mutex> lock(sh.mtx);
			if (seg.pos > seg.end)	// the tail was given to another connection meanwhile
				break;
			const uint32 uTake = (uint32)min((uint64)dwRead, seg.end - seg.pos + 1);
			SWebChunk chunk{seg.pos, new BYTE[uTake], uTake};
			memcpy(chunk.data, pData, uTake);
			sh.chunks.push_back(chunk);
			sh.queuedBytes += uTake;
			seg.pos += uTake;
			if (seg.pos > seg.end)
				break;
		}
		FinishSegment(sh, seg, CString());
	}
}

// ---------------------------------------------------------------- CWebDownload

CWebDownload::CWebDownload(const CString &sUrl)
	: m_sUrl(sUrl)
	, m_dwRetryTick()
	, m_dwRateTick()
	, m_uRateBytes()
	, m_uDatarate()
	, m_uFailures()
{
}

CWebDownload::~CWebDownload()
{
	Stop();
}

void CWebDownload::Stop()
{
	// The workers own a reference to the shared state and leave on their own
	// after the current network call; nothing here waits for them.
	if (m_shared)
		m_shared->abort = true;
	m_shared.reset();
	m_segments.clear();
	m_uDatarate = 0;
	m_uRateBytes = 0;
	m_dwRateTick = 0;
}

bool CWebDownload::StartSegment(uint64 uStart, uint64 uEnd)
{
	std::shared_ptr<SWebSegment> pSeg = std::make_shared<SWebSegment>();
	pSeg->start = pSeg->pos = pSeg->snapPos = uStart;
	pSeg->end = uEnd;
	pSeg->finished = pSeg->failed = false;
	try {
		std::thread(WebWorker, m_shared, pSeg).detach();
	} catch (const std::system_error&) {
		return false;
	}
	m_segments.push_back(pSeg);
	return true;
}

void CWebDownload::StartSegments(CPartFile *pFile)
{
	// With range support unknown or missing only one connection is used.
	const UINT uMax = m_shared->rangesOk ? theWebDownloads.GetMaxConnections() : 1;
	while (m_segments.size() < uMax) {
		// Claimed by running connections: everything from the last drained position to the segment end.
		// Data between snapPos and the real position may still be in the queue and thus not yet
		// reflected by the gap list - it must not be handed out again.
		std::vector<std::pair<uint64, uint64> > claims;
		for (const std::shared_ptr<SWebSegment> &pSeg : m_segments)
			if (pSeg->snapPos <= pSeg->end)
				claims.push_back(std::make_pair(pSeg->snapPos, pSeg->end));
		std::sort(claims.begin(), claims.end());

		// largest piece which is missing in the file and not claimed
		uint64 uBestStart = 0, uBestLen = 0;
		const CList<Gap_Struct> &gaps = pFile->GetGapList();
		for (POSITION pos = gaps.GetHeadPosition(); pos != NULL;) {
			const Gap_Struct &gap = gaps.GetNext(pos);
			uint64 uCursor = gap.start;
			for (size_t i = 0; i < claims.size() && uCursor <= gap.end; ++i) {
				if (claims[i].second < uCursor)
					continue;
				if (claims[i].first > gap.end)
					break;
				if (claims[i].first > uCursor && claims[i].first - uCursor > uBestLen) {
					uBestStart = uCursor;
					uBestLen = claims[i].first - uCursor;
				}
				uCursor = claims[i].second + 1;
				if (uCursor == 0)	// wrapped around
					uCursor = gap.end + 1;
			}
			if (uCursor <= gap.end && gap.end - uCursor + 1 > uBestLen) {
				uBestStart = uCursor;
				uBestLen = gap.end - uCursor + 1;
			}
		}
		if (uBestLen > 0) {
			if (!StartSegment(uBestStart, uBestStart + uBestLen - 1))
				return;
			continue;
		}

		// Nothing is free: take over the second half of the connection with the most work left.
		uint64 uNewStart = 0, uNewEnd = 0;
		{
			std::lock_guard<std::mutex> lock(m_shared->mtx);
			SWebSegment *pVictim = NULL;
			uint64 uMaxLeft = 0;
			for (const std::shared_ptr<SWebSegment> &pSeg : m_segments)
				if (!pSeg->finished && pSeg->pos <= pSeg->end && pSeg->end - pSeg->pos + 1 > uMaxLeft) {
					uMaxLeft = pSeg->end - pSeg->pos + 1;
					pVictim = pSeg.get();
				}
			if (pVictim == NULL || uMaxLeft < 2 * WEB_MIN_SPLIT)
				return;
			uNewStart = pVictim->pos + uMaxLeft / 2;
			uNewEnd = pVictim->end;
			pVictim->end = uNewStart - 1;
		}
		if (!StartSegment(uNewStart, uNewEnd))
			return;
	}
}

void CWebDownload::Process(CPartFile *pFile, bool bActive)
{
	if (!bActive) {
		if (m_shared) {
			// keep what was received already (pause); the file ignores it when it cannot take data
			std::deque<SWebChunk> chunks;
			{
				std::lock_guard<std::mutex> lock(m_shared->mtx);
				chunks.swap(m_shared->chunks);
				m_shared->queuedBytes = 0;
			}
			Stop();
			for (const SWebChunk &chunk : chunks)
				pFile->WriteWebData(chunk.start, chunk.data, chunk.len);
		}
		return;
	}

	const DWORD dwNow = ::GetTickCount();
	if (!m_shared) {
		m_shared = std::make_shared<SWebShared>();
		m_shared->url = m_sUrl;
		m_shared->fileSize = (uint64)pFile->GetFileSize();
		m_dwRateTick = dwNow;
		m_uRateBytes = 0;
	}

	// Take over the received data and note how far each connection got, in one step.
	std::deque<SWebChunk> chunks;
	CString sFatal;
	{
		std::lock_guard<std::mutex> lock(m_shared->mtx);
		chunks.swap(m_shared->chunks);
		m_shared->queuedBytes = 0;
		sFatal = m_shared->fatalError;
		for (const std::shared_ptr<SWebSegment> &pSeg : m_segments)
			pSeg->snapPos = pSeg->pos;
	}
	for (const SWebChunk &chunk : chunks) {
		m_uRateBytes += chunk.len;
		pFile->WriteWebData(chunk.start, chunk.data, chunk.len);	// takes ownership of the buffer
	}
	if (!chunks.empty())
		m_uFailures = 0;

	// Connections which ended. 'finished' is set after the last chunk was queued, and
	// reading it without the lock is fine: a worker never touches its segment afterwards.
	for (size_t i = m_segments.size(); i-- > 0;) {
		bool bFinished, bFailed;
		CString sError;
		{
			std::lock_guard<std::mutex> lock(m_shared->mtx);
			bFinished = m_segments[i]->finished && m_segments[i]->snapPos == m_segments[i]->pos;
			bFailed = m_segments[i]->failed;
			sError = m_segments[i]->error;
		}
		if (!bFinished)
			continue;
		if (bFailed && sFatal.IsEmpty()) {
			++m_uFailures;
			m_dwRetryTick = dwNow + min(30000u, 2000u * m_uFailures);
			if (thePrefs.GetVerbose())
				AddDebugLogLine(false, _T("Web download \"%s\": %s (retry %u)"), (LPCTSTR)pFile->GetFileName(), (LPCTSTR)sError, m_uFailures);
		}
		m_segments.erase(m_segments.begin() + i);
	}

	if (!sFatal.IsEmpty()) {
		LogError(LOG_STATUSBAR, _T("Web download \"%s\" stopped: %s"), (LPCTSTR)pFile->GetFileName(), (LPCTSTR)sFatal);
		Stop();
		pFile->PauseFile();
		return;
	}

	if (dwNow - m_dwRateTick >= 1000) {
		const uint32 uRate = (uint32)(m_uRateBytes * 1000 / (dwNow - m_dwRateTick));
		m_uDatarate = m_uDatarate ? (m_uDatarate + uRate) / 2 : uRate;	// light smoothing
		m_uRateBytes = 0;
		m_dwRateTick = dwNow;
	}

	if (m_uFailures == 0 || (int)(dwNow - m_dwRetryTick) >= 0)
		StartSegments(pFile);
}

// ---------------------------------------------------------------- CWebDownloadQueue

struct SWebProbe
{
	CString	url;
	int		cat = 0;
	bool	started = false;			// main thread only
	std::atomic<bool> done{false};		// set by the worker last; the fields below are valid then
	uint64	size = WEB_SIZE_UNKNOWN;
	CString	name;
	CString	error;
};

namespace
{
	void ProbeWorker(std::shared_ptr<SWebProbe> pProbe)
	{
		SWebProbe &probe = *pProbe;
		SHttpRequest req;
		DWORD dwStatus;
		CString sValue;
		// A one byte range request reveals the total size and whether ranges are supported,
		// also on servers which refuse HEAD requests.
		if (SendGet(probe.url, _T("bytes=0-0"), req, dwStatus, probe.error)) {
			uint64 uFirst, uTotal;
			if (dwStatus == HTTP_STATUS_PARTIAL_CONTENT) {
				if (GetHeader(req.hRequest, WINHTTP_QUERY_CONTENT_RANGE, sValue) && ParseContentRange(sValue, uFirst, uTotal))
					probe.size = uTotal;
			} else if (dwStatus == HTTP_STATUS_OK) {
				if (GetHeader(req.hRequest, WINHTTP_QUERY_CONTENT_LENGTH, sValue) && ParseUInt64(sValue, uTotal))
					probe.size = uTotal;
			} else
				probe.error.Format(_T("HTTP status %u"), dwStatus);

			if (probe.error.IsEmpty()) {
				if (probe.size == WEB_SIZE_UNKNOWN)
					probe.error = _T("the server does not tell the file size");
				else if (probe.size == 0)
					probe.error = _T("the file is empty");
			}
			if (probe.error.IsEmpty()) {
				if (GetHeader(req.hRequest, WINHTTP_QUERY_CONTENT_DISPOSITION, sValue))
					probe.name = FileNameFromDisposition(sValue);
				if (probe.name.IsEmpty()) {
					// the address after redirections usually carries the better name
					TCHAR szUrl[2084];
					DWORD dwSize = sizeof szUrl;
					if (::WinHttpQueryOption(req.hRequest, WINHTTP_OPTION_URL, szUrl, &dwSize))
						probe.name = FileNameFromUrl(szUrl);
				}
				if (probe.name.IsEmpty())
					probe.name = FileNameFromUrl(probe.url);
				probe.name = SanitizeFileName(probe.name);
				if (probe.name.IsEmpty())
					probe.name = _T("download");
			}
		}
		probe.done = true;
	}
}

bool CWebDownloadQueue::IsWebUrl(const CString &sUrl)
{
	return _tcsnicmp(sUrl, _T("http://"), 7) == 0 || _tcsnicmp(sUrl, _T("https://"), 8) == 0;
}

// A series of addresses written as one: "https://host/file[001-120].zip" stands for file001.zip
// to file120.zip. The first number tells how the others are written: with leading zeros it
// gives the width of all. Returns false if the address holds no such range.
bool CWebDownloadQueue::ExpandPattern(const CString &sUrl, std::vector<CString> &urls)
{
	const int iHost = sUrl.Find(_T("://"));
	const int iPath = iHost < 0 ? -1 : sUrl.Find(_T('/'), iHost + 3);	// after the host, which may be "[::1]"
	for (int iOpen = iPath < 0 ? -1 : sUrl.Find(_T('['), iPath); iOpen >= 0; iOpen = sUrl.Find(_T('['), iOpen + 1)) {
		const int iClose = sUrl.Find(_T(']'), iOpen);
		if (iClose < 0)
			break;
		const CString sRange(sUrl.Mid(iOpen + 1, iClose - iOpen - 1));
		const int iDash = sRange.Find(_T('-'));
		if (iDash <= 0 || iDash >= sRange.GetLength() - 1 || iDash > 9 || sRange.GetLength() - iDash - 1 > 9)
			continue;
		const CString sFirst(sRange.Left(iDash));
		const CString sLast(sRange.Mid(iDash + 1));
		if (sFirst.SpanIncluding(_T("0123456789")) != sFirst || sLast.SpanIncluding(_T("0123456789")) != sLast)
			continue;
		const int iFirst = _ttoi(sFirst);
		const int iLast = _ttoi(sLast);
		if (iLast < iFirst || iLast - iFirst >= WEB_MAX_SERIES)
			continue;
		const int iWidth = (sFirst.GetLength() > 1 && sFirst[0] == _T('0')) ? sFirst.GetLength() : 0;
		for (int i = iFirst; i <= iLast; ++i) {
			CString sNumber;
			sNumber.Format(_T("%0*d"), iWidth, i);
			urls.push_back(sUrl.Left(iOpen) + sNumber + sUrl.Mid(iClose + 1));
		}
		return true;
	}
	return false;
}

void CWebDownloadQueue::AddUrl(const CString &sUrl, int cat)
{
	std::vector<CString> series;
	if (ExpandPattern(sUrl, series)) {
		AddLogLine(true, _T("Web download: %u addresses from %s"), (unsigned)series.size(), (LPCTSTR)sUrl);
		for (const CString &sOne : series)
			AddUrl(sOne, cat);
		return;
	}
	std::shared_ptr<SWebProbe> pProbe = std::make_shared<SWebProbe>();
	pProbe->url = sUrl;
	pProbe->cat = cat;
	m_probes.push_back(pProbe);
	AddLogLine(false, _T("Web download: requesting file information for %s"), (LPCTSTR)sUrl);
}

void CWebDownloadQueue::Clear()
{
	m_probes.clear();	// running workers keep their own reference
}

UINT CWebDownloadQueue::GetMaxConnections()
{
	if (m_uMaxConnections == 0)
		m_uMaxConnections = max(1u, min(WEB_MAX_CONNECTIONS, theApp.GetProfileInt(_T("eMule"), _T("WebMaxConnections"), WEB_DEF_CONNECTIONS)));
	return m_uMaxConnections;
}

void CWebDownloadQueue::SetMaxConnections(UINT uConnections)
{
	m_uMaxConnections = max(1u, min(WEB_MAX_CONNECTIONS, uConnections));
	theApp.WriteProfileInt(_T("eMule"), _T("WebMaxConnections"), (int)m_uMaxConnections);
}

bool CWebDownloadQueue::LooksLikeFileUrl(const CString &sText)
{
	CString sUrl(sText);
	sUrl.Trim();
	if (!IsWebUrl(sUrl) || sUrl.FindOneOf(_T(" \t\r\n")) >= 0 || sUrl.GetLength() > 2000)
		return false;
	CString sName(FileNameFromUrl(sUrl));
	int iDot = sName.ReverseFind(_T('.'));
	if (iDot < 0)
		return false;
	CString sExt(sName.Mid(iDot + 1));
	sExt.MakeLower();
	static LPCTSTR const apszExt[] = {
		_T("zip"), _T("rar"), _T("7z"), _T("gz"), _T("bz2"), _T("xz"), _T("zst"), _T("tar"), _T("tgz"), _T("cab"),
		_T("iso"), _T("img"), _T("bin"), _T("dmg"), _T("exe"), _T("msi"), _T("msix"), _T("apk"), _T("deb"), _T("rpm"),
		_T("mp4"), _T("mkv"), _T("avi"), _T("mov"), _T("wmv"), _T("webm"), _T("mpg"), _T("mpeg"), _T("ts"), _T("m4v"),
		_T("mp3"), _T("flac"), _T("ogg"), _T("opus"), _T("m4a"), _T("wav"), _T("aac"),
		_T("pdf"), _T("epub"), _T("mobi"), _T("azw3"), _T("cbz"), _T("cbr"), _T("djvu")
	};
	for (LPCTSTR pszExt : apszExt)
		if (sExt == pszExt)
			return true;
	return false;
}

void CWebDownloadQueue::Process()
{
	// The download limit of the preferences covers ed2k and web together:
	// the web downloads may use what the ed2k sources leave, but never less than a tenth.
	const uint64 uMaxDownload = thePrefs.GetMaxDownloadInBytesPerSec(true);
	if (uMaxDownload == UNLIMITED * 1024ull || uMaxDownload > _UI32_MAX)
		g_uRateLimit = 0;
	else {
		const uint32 uLimit = (uint32)uMaxDownload;
		const uint32 uFloor = max(uLimit / 10, 1024u);
		g_uRateLimit = (m_uOtherDatarate + uFloor < uLimit) ? uLimit - m_uOtherDatarate : uFloor;
	}

	UINT uRunning = 0;
	for (const std::shared_ptr<SWebProbe> &pProbe : m_probes)
		uRunning += static_cast<UINT>(pProbe->started && !pProbe->done);

	for (size_t i = 0; i < m_probes.size();) {
		const std::shared_ptr<SWebProbe> pProbe = m_probes[i];
		if (!pProbe->started) {
			if (uRunning < WEB_MAX_PROBES && !theNetGuard.IsBlocked()) {
				pProbe->started = true;
				try {
					std::thread(ProbeWorker, pProbe).detach();
					++uRunning;
				} catch (const std::system_error&) {
					pProbe->error = _T("cannot start a thread");
					pProbe->done = true;
				}
			}
			++i;
			continue;
		}
		if (!pProbe->done) {
			++i;
			continue;
		}
		m_probes.erase(m_probes.begin() + i);

		if (!pProbe->error.IsEmpty()) {
			LogError(LOG_STATUSBAR, _T("Web download failed: %s - %s"), (LPCTSTR)pProbe->error, (LPCTSTR)pProbe->url);
			continue;
		}

		// A web file has no ed2k identity until it is complete. Until then it carries a
		// placeholder hash derived from the address, which also detects duplicates.
		const CStringA sKey("pegaso-web:" + CStringA(pProbe->url));
		CMD4 md4;
		md4.Add((LPCSTR)sKey, sKey.GetLength());
		md4.Finish();
		if (theApp.downloadqueue->IsFileExisting(md4.GetHash(), false)) {
			LogWarning(LOG_STATUSBAR, _T("Web download: this address is already in the download list - %s"), (LPCTSTR)pProbe->url);
			continue;
		}

		CString sLink;
		sLink.Format(_T("ed2k://|file|%s|%I64u|%s|/"), (LPCTSTR)EncodeUrlUtf8(pProbe->name), pProbe->size, (LPCTSTR)md4str(md4.GetHash()));
		CPartFile *pFile = new CPartFile(sLink, pProbe->cat);
		if (pFile->GetStatus() == PS_ERROR) {
			delete pFile;
			continue;
		}
		pFile->SetWebUrl(pProbe->url);
		theApp.downloadqueue->AddDownload(pFile, thePrefs.AddNewFilesPaused());
		AddLogLine(true, _T("Web download added: %s (%s)"), (LPCTSTR)pFile->GetFileName(), (LPCTSTR)CastItoXBytes((uint64)pFile->GetFileSize()));
	}
}

// ---------------------------------------------------------------- shared helpers

bool PegasoHttpGet(const CString &sUrl, std::vector<BYTE> &data, CString &sError, size_t uMaxSize)
{
	data.clear();
	SHttpRequest req;
	DWORD dwStatus;
	if (!SendGet(sUrl, NULL, req, dwStatus, sError))
		return false;
	if (dwStatus != HTTP_STATUS_OK) {
		sError.Format(_T("HTTP status %u"), dwStatus);
		return false;
	}
	BYTE buffer[16 * 1024];
	for (;;) {
		DWORD dwRead = 0;
		if (!::WinHttpReadData(req.hRequest, buffer, sizeof buffer, &dwRead)) {
			sError = WinHttpError(_T("read"));
			return false;
		}
		if (dwRead == 0)
			return true;
		if (data.size() + dwRead > uMaxSize) {
			sError = _T("the answer is too large");
			return false;
		}
		data.insert(data.end(), buffer, buffer + dwRead);
	}
}

void PegasoMarkFromInternet(const CString &sPath, const CString &sSourceUrl)
{
	PegasoScanFile(sPath);	// every finished download is also shown to the antivirus
	// Writing the mark changes the modification time of the file, and both eMule and the
	// BitTorrent engine take a file with another time for a changed one (hashing it again):
	// the time is put back afterwards.
	FILETIME ftWrite = {};
	bool bHaveTime = false;
	HANDLE hData = ::CreateFile(sPath, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	if (hData != INVALID_HANDLE_VALUE) {
		bHaveTime = ::GetFileTime(hData, NULL, NULL, &ftWrite) != FALSE;
		::CloseHandle(hData);
	}
	// An alternate data stream; file systems without them (FAT) simply refuse it.
	HANDLE hFile = ::CreateFile(sPath + _T(":Zone.Identifier"), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return;
	CStringA sText("[ZoneTransfer]\r\nZoneId=3\r\n");	// 3 = Internet zone
	if (CWebDownloadQueue::IsWebUrl(sSourceUrl))
		sText += "HostUrl=" + CStringA(EncodeUrlUtf8(sSourceUrl)) + "\r\n";
	DWORD dwWritten;
	::WriteFile(hFile, (LPCSTR)sText, (DWORD)sText.GetLength(), &dwWritten, NULL);
	::CloseHandle(hFile);
	if (bHaveTime) {
		hData = ::CreateFile(sPath, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
		if (hData != INVALID_HANDLE_VALUE) {
			::SetFileTime(hData, NULL, NULL, &ftWrite);
			::CloseHandle(hData);
		}
	}
}
