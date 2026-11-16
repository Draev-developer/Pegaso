// Pegaso: names for finished videos, from the film and series databases. See PegasoRename.h.
#include "stdafx.h"
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <winhttp.h>
#include "emule.h"
#include "PegasoRename.h"
#include "PegasoStrings.h"
#include "KnownFile.h"
#include "KnownFileList.h"
#include "SharedFileList.h"
#include "TorrentQueue.h"
#include "WebDownload.h"
#include "NetGuard.h"
#include "PegasoPrivacy.h"
#include "Preferences.h"
#include "OtherFunctions.h"
#include "StringConversion.h"
#include "Version.h"
#include "Log.h"
#include "zlib/zlib.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// ---------------------------------------------------------------- what the lookups need

struct SRenameEnv
{
	CString	keyTmdb, keyOmdb, keyTvdb;
	CString	testBase;	// tests only (RenameTestBase): the databases are asked at this address instead
	CString	configDir;
	CString	lang2;		// "es"
	CString	lang3;		// "spa"
	CString	langTag;	// "es-ES"
};

struct SRenameItem
{
	int		row;
	SVideoName name;
};

enum ERenameCode
{
	RC_FOUND,
	RC_NOT_FOUND,
	RC_NEED_KEY,
	RC_BLOCKED,		// AniDB does not let programs fetch its list from here
	RC_ERROR		// 'text' explains
};

struct SRenameResult
{
	int		row;
	SVideoName name;
	ERenameCode code;
	ERenameDb db;
	CString	text;
};

struct SMoveJob
{
	std::atomic<bool> done{false};
	std::atomic<bool> cancel{false};
	std::atomic<ULONGLONG> moved{0};	// bytes of the file which is being moved
	bool	ok = false;
	DWORD	error = 0;
};

struct SRenameJob
{
	std::mutex mtx;
	std::vector<SRenameResult> results;
	std::atomic<bool> done{false};
	std::atomic<bool> cancel{false};
};

namespace
{
	const UINT_PTR TIMER_POLL = 1;
	const UINT_PTR TIMER_MOVE = 2;

	DWORD CALLBACK MoveProgress(LARGE_INTEGER, LARGE_INTEGER liMoved, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID pData)
	{
		SMoveJob *pJob = static_cast<SMoveJob*>(pData);
		pJob->moved = (ULONGLONG)liMoved.QuadPart;
		return pJob->cancel ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
	}

	// To another disk a move is a copy, which takes as long as the file is large.
	void MoveWorker(std::shared_ptr<SMoveJob> pJob, CString sFrom, CString sTo)
	{
		pJob->ok = ::MoveFileWithProgress(sFrom, sTo, MoveProgress, pJob.get(), MOVEFILE_COPY_ALLOWED) != FALSE;
		if (!pJob->ok)
			pJob->error = ::GetLastError();
		pJob->done = true;
	}
	const size_t MAX_ANSWER = 4 * 1024 * 1024;
	const size_t MAX_TITLES = 64 * 1024 * 1024;

	LPCTSTR const s_apszVideo[] = {
		_T(".avi"), _T(".mkv"), _T(".mp4"), _T(".m4v"), _T(".mpg"), _T(".mpeg"), _T(".wmv"), _T(".mov"), _T(".ts"),
		_T(".m2ts"), _T(".divx"), _T(".ogm"), _T(".webm"), _T(".flv"), _T(".vob"), _T(".rmvb")
	};

	LPCTSTR DbName(ERenameDb db)
	{
		switch (db) {
		case RDB_TVMAZE:
			return _T("TVmaze");
		case RDB_TMDB:
			return _T("TheMovieDB");
		case RDB_TVDB:
			return _T("TheTVDB");
		case RDB_ANIDB:
			return _T("AniDB");
		default:
			return _T("OMDb");
		}
	}

	CString Extension(const CString &sPath)
	{
		const int iDot = sPath.ReverseFind(_T('.'));
		return (iDot > sPath.ReverseFind(_T('\\')) && sPath.GetLength() - iDot <= 6) ? sPath.Mid(iDot) : CString();
	}

	CString FolderOf(const CString &sPath)
	{
		return sPath.Left(sPath.ReverseFind(_T('\\')));
	}

	CString NameOf(const CString &sPath)
	{
		return sPath.Mid(sPath.ReverseFind(_T('\\')) + 1);
	}

	CString ProfileText(LPCTSTR pszKey)
	{
		return CString(OptUtf8ToStr(URLDecode(theApp.GetProfileString(_T("eMule"), pszKey, _T("")))));
	}

	void WriteProfileText(LPCTSTR pszKey, const CString &sValue)
	{
		theApp.WriteProfileString(_T("eMule"), pszKey, EncodeUrlUtf8(sValue));
	}

	// The keys of the databases are kept encrypted for the user account (PegasoPrivacy.h)
	CString ProfileSecret(LPCTSTR pszKey)
	{
		const CString sStored(theApp.GetProfileString(_T("eMule"), pszKey, _T("")));
		return sStored.Left(6) == _T("dpapi:") ? PegasoUnprotect(sStored) : CString(OptUtf8ToStr(URLDecode(sStored)));
	}

	void WriteProfileSecret(LPCTSTR pszKey, const CString &sValue)
	{
		theApp.WriteProfileString(_T("eMule"), pszKey, PegasoProtect(sValue));
	}

	// ------------------------------------------------------------ HTTP and JSON

	CString QueryValue(const CString &sValue)
	{
		const CStringA sUtf8(StrToUtf8(sValue));
		CString sOut;
		for (int i = 0; i < sUtf8.GetLength(); ++i) {
			const unsigned char c = (unsigned char)sUtf8[i];
			if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
				sOut += (TCHAR)c;
			else
				sOut.AppendFormat(_T("%%%02X"), c);
		}
		return sOut;
	}

	// One request. sHeaders: complete header lines, or empty. The status is given back as it
	// came: a database says "not found" with 404.
	bool Http(LPCWSTR pszMethod, const CString &sUrl, const CString &sHeaders, const CStringA &sBody, std::vector<BYTE> &data, DWORD &dwStatus, CString &sError, size_t uMaxSize = MAX_ANSWER)
	{
		data.clear();
		dwStatus = 0;
		std::vector<WCHAR> host(256), path(4096), extra(4096);
		URL_COMPONENTS uc = {};
		uc.dwStructSize = sizeof uc;
		uc.lpszHostName = host.data();
		uc.dwHostNameLength = (DWORD)host.size();
		uc.lpszUrlPath = path.data();
		uc.dwUrlPathLength = (DWORD)path.size();
		uc.lpszExtraInfo = extra.data();
		uc.dwExtraInfoLength = (DWORD)extra.size();
		if (!::WinHttpCrackUrl(sUrl, 0, 0, &uc)) {
			sError = _T("bad address");
			return false;
		}
		bool bOk = false;
		HINTERNET hSession = PegasoHttpSession();
		if (hSession == NULL && ::GetLastError() == ERROR_NOT_SUPPORTED)
			sError = _T("the proxy of the preferences is SOCKS4, which cannot look names up: web requests are not made (SOCKS4a and SOCKS5 can)");
		HINTERNET hConnect = NULL;
		HINTERNET hRequest = NULL;
		if (hSession != NULL) {
			::WinHttpSetTimeouts(hSession, 15000, 15000, 30000, 30000);
			hConnect = ::WinHttpConnect(hSession, host.data(), uc.nPort, 0);
		}
		if (hConnect != NULL)
			hRequest = ::WinHttpOpenRequest(hConnect, pszMethod, CString(path.data()) + extra.data(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES
				, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
		if (hRequest != NULL
			&& ::WinHttpSendRequest(hRequest, sHeaders.IsEmpty() ? WINHTTP_NO_ADDITIONAL_HEADERS : (LPCWSTR)sHeaders, (DWORD)-1
				, sBody.IsEmpty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)(LPCSTR)sBody, (DWORD)sBody.GetLength(), (DWORD)sBody.GetLength(), 0)
			&& ::WinHttpReceiveResponse(hRequest, NULL))
		{
			DWORD dwSize = sizeof dwStatus;
			::WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatus, &dwSize, WINHTTP_NO_HEADER_INDEX);
			BYTE buffer[16 * 1024];
			bOk = true;
			for (;;) {
				DWORD dwRead = 0;
				if (!::WinHttpReadData(hRequest, buffer, sizeof buffer, &dwRead)) {
					bOk = false;
					break;
				}
				if (dwRead == 0)
					break;
				if (data.size() + dwRead > uMaxSize) {
					sError = _T("the answer is too large");
					bOk = false;
					break;
				}
				data.insert(data.end(), buffer, buffer + dwRead);
			}
		}
		if (!bOk && sError.IsEmpty())
			sError.Format(_T("no answer (%u)"), ::GetLastError());
		if (hRequest != NULL)
			::WinHttpCloseHandle(hRequest);
		if (hConnect != NULL)
			::WinHttpCloseHandle(hConnect);
		if (hSession != NULL)
			::WinHttpCloseHandle(hSession);
		return bOk;
	}

	CStringA Text(const std::vector<BYTE> &data)
	{
		return CStringA((LPCSTR)data.data(), (int)data.size());
	}

	void AppendUtf8(CStringA &s, unsigned long uCode)
	{
		if (uCode < 0x80)
			s += (char)uCode;
		else if (uCode < 0x800) {
			s += (char)(0xC0 | (uCode >> 6));
			s += (char)(0x80 | (uCode & 0x3F));
		} else if (uCode < 0x10000) {
			s += (char)(0xE0 | (uCode >> 12));
			s += (char)(0x80 | ((uCode >> 6) & 0x3F));
			s += (char)(0x80 | (uCode & 0x3F));
		} else {
			s += (char)(0xF0 | (uCode >> 18));
			s += (char)(0x80 | ((uCode >> 12) & 0x3F));
			s += (char)(0x80 | ((uCode >> 6) & 0x3F));
			s += (char)(0x80 | (uCode & 0x3F));
		}
	}

	// The value of the first "key" from iFrom on whose value is a string; empty if there is
	// none before iTo (-1: the end). Enough for the flat answers of these databases.
	CString JsonString(const CStringA &s, LPCSTR pszKey, int iFrom = 0, int iTo = -1)
	{
		const int iLen = (iTo < 0 || iTo > s.GetLength()) ? s.GetLength() : iTo;
		const CStringA sKey(CStringA("\"") + pszKey + "\"");
		for (int iPos = s.Find(sKey, max(0, iFrom)); iPos >= 0 && iPos < iLen; iPos = s.Find(sKey, iPos + 1)) {
			int i = iPos + sKey.GetLength();
			while (i < iLen && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
				++i;
			if (i >= iLen || s[i] != ':')
				continue;	// a value which reads like the key
			++i;
			while (i < iLen && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
				++i;
			if (i >= iLen || s[i] != '"')
				continue;	// null, a number, an object
			CStringA sOut;
			for (++i; i < iLen && s[i] != '"'; ++i) {
				if (s[i] != '\\' || i + 1 >= iLen) {
					sOut += s[i];
					continue;
				}
				++i;
				if (s[i] == 'u' && i + 4 < iLen) {
					unsigned long uCode = strtoul(s.Mid(i + 1, 4), NULL, 16);
					i += 4;
					if (uCode >= 0xD800 && uCode < 0xDC00 && i + 6 < iLen && s[i + 1] == '\\' && s[i + 2] == 'u') {
						const unsigned long uLow = strtoul(s.Mid(i + 3, 4), NULL, 16);
						uCode = 0x10000 + ((uCode - 0xD800) << 10) + (uLow - 0xDC00);
						i += 6;
					}
					AppendUtf8(sOut, uCode);
				} else if (s[i] == 'n' || s[i] == 't' || s[i] == 'r')
					sOut += ' ';
				else
					sOut += s[i];
			}
			return CString(OptUtf8ToStr(sOut)).Trim();
		}
		return CString();
	}

	// ------------------------------------------------------------ the databases

	struct SAniTitle
	{
		int		aid;
		int		type;	// 1 main, 2 synonym, 3 short, 4 official
		CString	lang;
		CString	title;
		CString	key;	// letters and digits only, lower case
	};

	struct SLookupState
	{
		SRenameEnv env;
		CString	tvdbToken;
		bool	tvdbTried = false;
		std::vector<SAniTitle> ani;
		bool	aniTried = false;
		bool	aniBlocked = false;
		CString	aniError;
		std::map<CString, SRenameResult> cache;
	};

	CString PlainKey(const CString &s)
	{
		CString sOut;
		for (int i = 0; i < s.GetLength(); ++i)
			if (_istalnum((_TUCHAR)s[i]) || s[i] > 0x7F)
				sOut += (TCHAR)_totlower(s[i]);
		return sOut;
	}

	bool Gunzip(const std::vector<BYTE> &in, std::vector<BYTE> &out)
	{
		out.clear();
		z_stream zs = {};
		if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)
			return false;
		zs.next_in = const_cast<Bytef*>(in.data());
		zs.avail_in = (uInt)in.size();
		std::vector<BYTE> buffer(256 * 1024);
		int iResult = Z_OK;
		while (iResult == Z_OK && out.size() < MAX_TITLES) {
			zs.next_out = buffer.data();
			zs.avail_out = (uInt)buffer.size();
			iResult = inflate(&zs, Z_NO_FLUSH);
			out.insert(out.end(), buffer.data(), buffer.data() + (buffer.size() - zs.avail_out));
		}
		inflateEnd(&zs);
		return iResult == Z_STREAM_END;
	}

	// AniDB has no search for programs: it publishes the list of all its titles, to be fetched
	// once a day at most and searched at home. The list is kept in the configuration folder.
	void LoadAniTitles(SLookupState &state)
	{
		if (state.aniTried)
			return;
		state.aniTried = true;
		const CString sFile(state.env.configDir + _T("anidb-titles.dat.gz"));
		std::vector<BYTE> packed;
		bool bFresh = false;
		WIN32_FILE_ATTRIBUTE_DATA fad;
		if (::GetFileAttributesEx(sFile, GetFileExInfoStandard, &fad)) {
			FILETIME ftNow;
			::GetSystemTimeAsFileTime(&ftNow);
			const ULONGLONG uAge = (((ULONGLONG)ftNow.dwHighDateTime << 32) | ftNow.dwLowDateTime) - (((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) | fad.ftLastWriteTime.dwLowDateTime);
			bFresh = uAge < 24ULL * 3600 * 10000000;
		}
		if (!bFresh) {
			const CString sUrl(state.env.testBase.IsEmpty() ? CString(_T("https://anidb.net/api/anime-titles.dat.gz")) : state.env.testBase + _T("/anidb/anime-titles.dat.gz"));
			DWORD dwStatus;
			CString sError;
			if (Http(L"GET", sUrl, CString(), CStringA(), packed, dwStatus, sError, MAX_TITLES) && dwStatus == HTTP_STATUS_OK && packed.size() > 2 && packed[0] == 0x1F && packed[1] == 0x8B) {
				FILE *f = _tfsopen(sFile, _T("wb"), _SH_DENYWR);
				if (f != NULL) {
					fwrite(packed.data(), 1, packed.size(), f);
					fclose(f);
				}
			} else {
				// AniDB puts a check for browsers in front of the list on some connections. Pegaso does
				// not get around it: the user can fetch the file with his browser and leave it here.
				state.aniBlocked = sError.IsEmpty() && (dwStatus == HTTP_STATUS_FORBIDDEN || dwStatus == HTTP_STATUS_SERVICE_UNAVAIL || dwStatus == HTTP_STATUS_OK);
				if (sError.IsEmpty())
					sError.Format(_T("HTTP status %u"), dwStatus);
				state.aniError = sError;
				packed.clear();
			}
		}
		if (packed.empty()) {	// the copy at home, also when a new one could not be had
			FILE *f = _tfsopen(sFile, _T("rb"), _SH_DENYWR);
			if (f != NULL) {
				BYTE buffer[64 * 1024];
				for (size_t n; (n = fread(buffer, 1, sizeof buffer, f)) > 0;)
					packed.insert(packed.end(), buffer, buffer + n);
				fclose(f);
			}
		}
		std::vector<BYTE> plain;
		if (packed.empty() || !Gunzip(packed, plain)) {
			if (state.aniError.IsEmpty())
				state.aniError = _T("the list of titles cannot be read");
			return;
		}
		state.aniError.Empty();
		state.aniBlocked = false;
		// <aid>|<type>|<language>|<title>
		const char *p = (const char*)plain.data();
		const char *const pEnd = p + plain.size();
		while (p < pEnd) {
			const char *pLine = p;
			while (p < pEnd && *p != '\n')
				++p;
			CStringA sLine(pLine, (int)(p - pLine));
			++p;
			sLine.TrimRight("\r");
			if (sLine.IsEmpty() || sLine[0] == '#')
				continue;
			const int i1 = sLine.Find('|');
			const int i2 = sLine.Find('|', i1 + 1);
			const int i3 = sLine.Find('|', i2 + 1);
			if (i1 <= 0 || i2 < 0 || i3 < 0)
				continue;
			SAniTitle t;
			t.aid = atoi(sLine.Left(i1));
			t.type = atoi(sLine.Mid(i1 + 1, i2 - i1 - 1));
			t.lang = CString(sLine.Mid(i2 + 1, i3 - i2 - 1));
			t.title = CString(OptUtf8ToStr(sLine.Mid(i3 + 1)));
			t.key = PlainKey(t.title);
			if (t.aid > 0 && !t.key.IsEmpty())
				state.ani.push_back(t);
		}
	}

	ERenameCode LookupAniDb(SLookupState &state, SVideoName &name, CString &sText)
	{
		LoadAniTitles(state);
		if (state.ani.empty()) {
			sText = state.aniError;
			return state.aniBlocked ? RC_BLOCKED : RC_ERROR;
		}
		const CString sKey(PlainKey(name.title));
		if (sKey.IsEmpty())
			return RC_NOT_FOUND;
		int iAid = 0;
		int iBest = INT_MAX;
		for (const SAniTitle &t : state.ani) {
			if (t.key == sKey) {
				iAid = t.aid;
				break;
			}
			// the title in the name of the file is often shorter than the one in the list
			if (sKey.GetLength() >= 4 && t.key.Find(sKey) == 0 && t.key.GetLength() - sKey.GetLength() < iBest) {
				iBest = t.key.GetLength() - sKey.GetLength();
				iAid = t.aid;
			}
		}
		if (iAid == 0)
			return RC_NOT_FOUND;
		// the official title in the language of the user, else the English one, else the main one
		CString sMine, sEnglish, sMain;
		for (const SAniTitle &t : state.ani)
			if (t.aid == iAid) {
				if (t.type == 4 && sMine.IsEmpty() && t.lang.CompareNoCase(state.env.lang2) == 0)
					sMine = t.title;
				else if (t.type == 4 && sEnglish.IsEmpty() && t.lang.CompareNoCase(_T("en")) == 0)
					sEnglish = t.title;
				else if (t.type == 1 && sMain.IsEmpty())
					sMain = t.title;
			}
		name.title = !sMine.IsEmpty() ? sMine : !sEnglish.IsEmpty() ? sEnglish : sMain;
		return name.title.IsEmpty() ? RC_NOT_FOUND : RC_FOUND;
	}

	ERenameCode Failed(DWORD dwStatus, const CString &sError, CString &sText)
	{
		if (dwStatus == HTTP_STATUS_DENIED || dwStatus == HTTP_STATUS_FORBIDDEN)
			return RC_NEED_KEY;	// the key is not a good one
		if (!sError.IsEmpty())
			sText = sError;
		else
			sText.Format(_T("HTTP status %u"), dwStatus);
		return RC_ERROR;
	}

	ERenameCode LookupTvMaze(SLookupState &state, SVideoName &name, CString &sText)
	{
		const CString sBase(state.env.testBase.IsEmpty() ? CString(_T("https://api.tvmaze.com")) : state.env.testBase + _T("/tvmaze"));
		std::vector<BYTE> data;
		DWORD dwStatus;
		CString sError;
		if (!Http(L"GET", sBase + _T("/singlesearch/shows?q=") + QueryValue(name.title), CString(), CStringA(), data, dwStatus, sError))
			return Failed(0, sError, sText);
		if (dwStatus == HTTP_STATUS_NOT_FOUND)
			return RC_NOT_FOUND;
		if (dwStatus != HTTP_STATUS_OK)
			return Failed(dwStatus, CString(), sText);
		const CString sName(JsonString(Text(data), "name"));
		if (sName.IsEmpty())
			return RC_NOT_FOUND;
		name.title = sName;
		return RC_FOUND;
	}

	ERenameCode LookupTmdb(SLookupState &state, SVideoName &name, CString &sText)
	{
		if (state.env.keyTmdb.IsEmpty())
			return RC_NEED_KEY;
		const CString sBase(state.env.testBase.IsEmpty() ? CString(_T("https://api.themoviedb.org/3")) : state.env.testBase + _T("/tmdb"));
		// the short key goes in the address; the long one ("API Read Access Token") in a header
		const bool bToken = state.env.keyTmdb.GetLength() > 64;
		CString sUrl(sBase + (name.series ? _T("/search/tv") : _T("/search/movie")));
		sUrl += _T("?query=") + QueryValue(name.title) + _T("&language=") + state.env.langTag;
		if (!name.series && name.year > 0)
			sUrl.AppendFormat(_T("&year=%d"), name.year);
		if (!bToken)
			sUrl += _T("&api_key=") + QueryValue(state.env.keyTmdb);
		std::vector<BYTE> data;
		DWORD dwStatus;
		CString sError;
		if (!Http(L"GET", sUrl, bToken ? _T("Authorization: Bearer ") + state.env.keyTmdb : CString(), CStringA(), data, dwStatus, sError))
			return Failed(0, sError, sText);
		if (dwStatus != HTTP_STATUS_OK)
			return Failed(dwStatus, CString(), sText);
		const CStringA sJson(Text(data));
		const int iResults = sJson.Find("\"results\"");
		if (iResults < 0)
			return RC_NOT_FOUND;
		// the first result only: up to the end of its object
		int iEnd = sJson.Find("},{", iResults);
		if (iEnd < 0)
			iEnd = sJson.GetLength();
		const CString sName(JsonString(sJson, name.series ? "name" : "title", iResults, iEnd));
		if (sName.IsEmpty())
			return RC_NOT_FOUND;
		name.title = sName;
		if (!name.series) {
			const int iYear = _ttoi(JsonString(sJson, "release_date", iResults, iEnd).Left(4));
			if (iYear > 1800)
				name.year = iYear;
		}
		return RC_FOUND;
	}

	ERenameCode LookupOmdb(SLookupState &state, SVideoName &name, CString &sText)
	{
		if (state.env.keyOmdb.IsEmpty())
			return RC_NEED_KEY;
		CString sUrl(state.env.testBase.IsEmpty() ? CString(_T("https://www.omdbapi.com/")) : state.env.testBase + _T("/omdb/"));
		sUrl += _T("?apikey=") + QueryValue(state.env.keyOmdb) + _T("&type=movie&t=") + QueryValue(name.title);
		if (name.year > 0)
			sUrl.AppendFormat(_T("&y=%d"), name.year);
		std::vector<BYTE> data;
		DWORD dwStatus;
		CString sError;
		if (!Http(L"GET", sUrl, CString(), CStringA(), data, dwStatus, sError))
			return Failed(0, sError, sText);
		if (dwStatus != HTTP_STATUS_OK)
			return Failed(dwStatus, CString(), sText);
		const CStringA sJson(Text(data));
		const CString sName(JsonString(sJson, "Title"));
		if (sName.IsEmpty())
			return RC_NOT_FOUND;	// {"Response":"False","Error":"Movie not found!"}
		name.title = sName;
		const int iYear = _ttoi(JsonString(sJson, "Year").Left(4));
		if (iYear > 1800)
			name.year = iYear;
		return RC_FOUND;
	}

	ERenameCode LookupTvdb(SLookupState &state, SVideoName &name, CString &sText)
	{
		if (state.env.keyTvdb.IsEmpty())
			return RC_NEED_KEY;
		const CString sBase(state.env.testBase.IsEmpty() ? CString(_T("https://api4.thetvdb.com/v4")) : state.env.testBase + _T("/tvdb"));
		std::vector<BYTE> data;
		DWORD dwStatus;
		CString sError;
		if (!state.tvdbTried) {	// the key is exchanged for a token, once
			state.tvdbTried = true;
			CStringA sKey(StrToUtf8(state.env.keyTvdb));
			sKey.Replace("\\", "");
			sKey.Replace("\"", "");
			if (!Http(L"POST", sBase + _T("/login"), _T("Content-Type: application/json"), "{\"apikey\":\"" + sKey + "\"}", data, dwStatus, sError))
				return Failed(0, sError, sText);
			if (dwStatus != HTTP_STATUS_OK)
				return Failed(dwStatus, CString(), sText);
			state.tvdbToken = JsonString(Text(data), "token");
		}
		if (state.tvdbToken.IsEmpty())
			return RC_NEED_KEY;
		if (!Http(L"GET", sBase + _T("/search?type=series&limit=1&query=") + QueryValue(name.title), _T("Authorization: Bearer ") + state.tvdbToken, CStringA(), data, dwStatus, sError))
			return Failed(0, sError, sText);
		if (dwStatus != HTTP_STATUS_OK)
			return Failed(dwStatus, CString(), sText);
		const CStringA sJson(Text(data));
		const int iData = sJson.Find("\"data\"");
		if (iData < 0)
			return RC_NOT_FOUND;
		CString sName(JsonString(sJson, "name", iData));
		// the name in the language of the user, if the answer brings it
		const int iTranslations = sJson.Find("\"translations\"", iData);
		if (iTranslations >= 0) {
			const int iClose = sJson.Find('}', iTranslations);
			const CString sMine(JsonString(sJson, CStringA(state.env.lang3), iTranslations, iClose));
			if (!sMine.IsEmpty())
				sName = sMine;
		}
		if (sName.IsEmpty())
			return RC_NOT_FOUND;
		name.title = sName;
		return RC_FOUND;
	}

	SRenameResult Lookup(SLookupState &state, ERenameDb db, const SRenameItem &item)
	{
		// the episodes of a series ask the same thing: once is enough
		CString sKey;
		sKey.Format(_T("%d|%d|%d|%s"), (int)db, (int)item.name.series, item.name.series ? 0 : item.name.year, (LPCTSTR)PlainKey(item.name.title));
		const auto it = state.cache.find(sKey);
		SRenameResult result;
		if (it != state.cache.end()) {
			result = it->second;
			const SVideoName found(result.name);
			result.name = item.name;
			if (result.code == RC_FOUND) {
				result.name.title = found.title;
				if (!item.name.series)
					result.name.year = found.year;
			}
		} else {
			result.name = item.name;
			result.db = db;
			switch (db) {
			case RDB_TVMAZE:
				result.code = LookupTvMaze(state, result.name, result.text);
				break;
			case RDB_TMDB:
				result.code = LookupTmdb(state, result.name, result.text);
				break;
			case RDB_TVDB:
				result.code = LookupTvdb(state, result.name, result.text);
				break;
			case RDB_ANIDB:
				result.code = LookupAniDb(state, result.name, result.text);
				break;
			default:
				result.code = LookupOmdb(state, result.name, result.text);
			}
			state.cache[sKey] = result;
		}
		result.row = item.row;
		return result;
	}

	void LookupWorker(std::shared_ptr<SRenameJob> pJob, SRenameEnv env, std::vector<SRenameItem> items, ERenameDb dbSeries, ERenameDb dbMovies)
	{
		SLookupState state;
		state.env = env;
		for (const SRenameItem &item : items) {
			if (pJob->cancel)
				break;
			const SRenameResult result(Lookup(state, item.name.series ? dbSeries : dbMovies, item));
			std::lock_guard<std::mutex> lock(pJob->mtx);
			pJob->results.push_back(result);
		}
		pJob->done = true;
	}

	void Languages(SRenameEnv &env)
	{
		switch (thePrefs.GetLanguageID()) {
		case 1034:
			env.lang2 = _T("es"), env.lang3 = _T("spa"), env.langTag = _T("es-ES");
			break;
		case 1036:
			env.lang2 = _T("fr"), env.lang3 = _T("fra"), env.langTag = _T("fr-FR");
			break;
		case 1040:
			env.lang2 = _T("it"), env.lang3 = _T("ita"), env.langTag = _T("it-IT");
			break;
		case 1049:
			env.lang2 = _T("ru"), env.lang3 = _T("rus"), env.langTag = _T("ru-RU");
			break;
		case 2070:
			env.lang2 = _T("pt"), env.lang3 = _T("por"), env.langTag = _T("pt-PT");
			break;
		case 1046:
			env.lang2 = _T("pt-BR"), env.lang3 = _T("pt"), env.langTag = _T("pt-BR");
			break;
		case 1031:
			env.lang2 = _T("de"), env.lang3 = _T("deu"), env.langTag = _T("de-DE");
			break;
		default:
			env.lang2 = _T("en"), env.lang3 = _T("eng"), env.langTag = _T("en-US");
		}
	}

	// ------------------------------------------------------------ the name of a file

	std::wstring Tidy(std::wstring s)
	{
		// separators and what is left hanging at the ends
		std::wstring out;
		bool bSpace = false;
		for (wchar_t c : s) {
			if (c == L' ' || c == L'\t') {
				bSpace = !out.empty();
				continue;
			}
			if (bSpace)
				out += L' ';
			bSpace = false;
			out += c;
		}
		const wchar_t *const pszEdge = L" -([{,;:";
		while (!out.empty() && wcschr(pszEdge, out.back()) != NULL)
			out.pop_back();
		while (!out.empty() && wcschr(L" -)]},;:", out.front()) != NULL)
			out.erase(out.begin());
		return out;
	}

	// what a name must not have in Windows
	CString Safe(CString s)
	{
		s.Replace(_T(": "), _T(" - "));
		s.Replace(_T(':'), _T('-'));
		s.Replace(_T('/'), _T('-'));
		s.Replace(_T('\\'), _T('-'));
		s.Replace(_T('|'), _T('-'));
		s.Remove(_T('?'));
		s.Remove(_T('*'));
		s.Remove(_T('<'));
		s.Remove(_T('>'));
		s.Replace(_T('"'), _T('\''));
		for (int i = 0; i < s.GetLength(); ++i)
			if ((unsigned)s[i] < 0x20)
				s.SetAt(i, _T(' '));
		s.Trim(_T(" ."));
		return s;
	}
}

// ---------------------------------------------------------------- PegasoRename

bool PegasoRename::IsVideo(const CString &sPath)
{
	const CString sExt(Extension(sPath));
	for (LPCTSTR pszVideo : s_apszVideo)
		if (sExt.CompareNoCase(pszVideo) == 0)
			return true;
	return false;
}

SVideoName PegasoRename::Parse(const CString &sFileName)
{
	SVideoName v;
	CString sBase(NameOf(sFileName));
	if (IsVideo(sBase))
		sBase.Truncate(sBase.GetLength() - Extension(sBase).GetLength());
	std::wstring s((LPCTSTR)sBase);
	for (wchar_t &c : s)
		if (c == L'.' || c == L'_')
			c = L' ';
	// the group which released it, in front: "[Group] Title - 05"
	static const std::wregex reLead(L"^\\s*\\[[^\\]]*\\]\\s*");
	const bool bGroup = std::regex_search(s, reLead);
	while (std::regex_search(s, reLead))
		s = std::regex_replace(s, reLead, L"", std::regex_constants::format_first_only);

	const auto icase = std::regex_constants::icase;
	static const std::wregex areEpisode[] = {
		std::wregex(L"\\bS(\\d{1,2})\\s?E(\\d{1,3})\\b", icase),									// S01E02
		std::wregex(L"\\b(\\d{1,2})x(\\d{1,3})\\b", icase),											// 1x02
		std::wregex(L"\\bSeason\\s?(\\d{1,2}).*?\\bEpisode\\s?(\\d{1,3})\\b", icase),
		std::wregex(L"\\bTemporada\\s?(\\d{1,2}).*?\\bCap(?:[i\\u00ed]tulo)?\\s?(\\d{1,3})\\b", icase),
		std::wregex(L"\\bCap(?:[i\\u00ed]tulo)?\\s?(\\d{1,2})(\\d{2})\\b", icase)					// Cap.102
	};
	std::wsmatch m;
	size_t uCut = std::wstring::npos;
	for (const std::wregex &re : areEpisode)
		if (std::regex_search(s, m, re)) {
			v.series = true;
			v.season = _wtoi(m[1].str().c_str());
			v.episode = _wtoi(m[2].str().c_str());
			uCut = (size_t)m.position(0);
			break;
		}
	if (!v.series && bGroup) {
		// anime: "Title - 05", the season is not told
		static const std::wregex reAnime(L"\\s-\\s(\\d{1,3})(?:v\\d)?(?=\\s|$|\\[|\\()");
		if (std::regex_search(s, m, reAnime)) {
			v.series = true;
			v.season = 1;
			v.episode = _wtoi(m[1].str().c_str());
			uCut = (size_t)m.position(0);
		}
	}
	if (!v.series) {
		// a film: the year is the last one written, unless the name starts with it
		static const std::wregex reYear(L"\\b(19\\d\\d|20\\d\\d)\\b");
		for (std::wsregex_iterator it(s.begin(), s.end(), reYear), end; it != end; ++it)
			if (it->position(0) > 0) {
				v.year = _wtoi((*it)[1].str().c_str());
				uCut = (size_t)it->position(0);
			}
		// or the name ends where the description of the copy starts
		static const std::wregex reTag(L"\\b(2160p|1080p|1080i|720p|576p|480p|4k|uhd|hdtv|hdrip|bluray|blu-ray|bdrip|brrip|bdremux|remux|dvdrip|dvd|webrip|web-dl|webdl|web|microhd|x264|x265|h264|h265|hevc|xvid|divx|ac3|dts|aac|screener|ts-screener|castellano|spanish|espa\\u00f1ol|latino|dual|subs|vose|vos|multi)\\b", icase);
		if (std::regex_search(s, m, reTag) && m.position(0) > 0 && (size_t)m.position(0) < uCut)
			uCut = (size_t)m.position(0);
	}
	// brackets after the title belong to the description as well
	const size_t uBracket = s.find_first_of(L"[(");
	if (uBracket != std::wstring::npos && uBracket > 0 && uBracket < uCut)
		uCut = uBracket;
	if (uCut != std::wstring::npos)
		s.resize(uCut);
	v.title = Tidy(s).c_str();
	return v;
}

CString PegasoRename::Build(const SVideoName &name, const CString &sExt)
{
	CString s;
	if (name.series)
		s.Format(_T("%s - %dx%02d"), (LPCTSTR)name.title, name.season, name.episode);
	else if (name.year > 0)
		s.Format(_T("%s (%d)"), (LPCTSTR)name.title, name.year);
	else
		s = name.title;
	return Safe(s) + sExt;
}

bool PegasoRename::Prepare(const CString &sOldPath, const CString &sNewPath, CString &sError)
{
	if (sOldPath.CompareNoCase(sNewPath) != 0 && ::GetFileAttributes(sNewPath) != INVALID_FILE_ATTRIBUTES) {
		sError = GetPegasoString(PS_RN_EXISTS);
		return false;
	}
	const CString sNewDir(FolderOf(sNewPath));
	if (::GetFileAttributes(sNewDir) == INVALID_FILE_ATTRIBUTES)
		::SHCreateDirectoryEx(NULL, sNewDir, NULL);
	return true;
}

namespace
{
	CString Plain(CString sDir)
	{
		sDir.TrimRight(_T('\\'));
		return sDir.MakeLower();
	}

	// inside one of the folders downloads end up in - and not one of them
	bool InsideDownloads(const CString &sDir)
	{
		const CString sPlain(Plain(sDir));
		std::vector<CString> roots;
		roots.push_back(Plain(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR)));
		for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i)
			roots.push_back(Plain(thePrefs.GetCatPath(i)));
		bool bInside = false;
		for (const CString &sRoot : roots) {
			if (sRoot.IsEmpty())
				continue;
			if (sPlain == sRoot)
				return false;
			if (sPlain.Left(sRoot.GetLength() + 1) == sRoot + _T('\\'))
				bInside = true;
		}
		return bInside;
	}

	// all the files of a folder, those of the folders inside it included; false if there are too many
	bool FilesOf(const CString &sDir, std::vector<CString> &files, int iDepth = 0)
	{
		CFileFind ff;
		for (BOOL bMore = ff.FindFile(sDir + _T("\\*")); bMore;) {
			bMore = ff.FindNextFile();
			if (ff.IsDots())
				continue;
			if (ff.IsDirectory()) {
				if (iDepth >= 8 || !FilesOf(ff.GetFilePath(), files, iDepth + 1))
					return false;
			} else {
				if (files.size() >= 2000)
					return false;
				files.push_back(ff.GetFilePath());
			}
		}
		return true;
	}

	bool IsEmptyFolder(const CString &sDir)
	{
		std::vector<CString> files;
		if (!FilesOf(sDir, files) || !files.empty())
			return false;
		CFileFind ff;
		for (BOOL bMore = ff.FindFile(sDir + _T("\\*")); bMore;) {
			bMore = ff.FindNextFile();
			if (!ff.IsDots())
				return false;	// a folder inside
		}
		return true;
	}

	// Windows does not remove a folder which is marked read-only, and many are
	bool RemoveFolder(const CString &sDir)
	{
		const DWORD dwAttributes = ::GetFileAttributes(sDir);
		if (dwAttributes != INVALID_FILE_ATTRIBUTES && (dwAttributes & FILE_ATTRIBUTE_READONLY) != 0)
			::SetFileAttributes(sDir, dwAttributes & ~FILE_ATTRIBUTE_READONLY);
		if (::RemoveDirectory(sDir))
			return true;
		if (dwAttributes != INVALID_FILE_ATTRIBUTES)
			::SetFileAttributes(sDir, dwAttributes);	// as it was
		return false;
	}

	// a folder with nothing but folders in it, the folders inside first
	void RemoveEmptyTree(const CString &sDir)
	{
		CFileFind ff;
		for (BOOL bMore = ff.FindFile(sDir + _T("\\*")); bMore;) {
			bMore = ff.FindNextFile();
			if (!ff.IsDots() && ff.IsDirectory())
				RemoveEmptyTree(ff.GetFilePath());
		}
		ff.Close();
		RemoveFolder(sDir);
	}
}

bool PegasoRename::CleanFolder(const CString &sFolder)
{
	CString sDir(sFolder);
	sDir.TrimRight(_T('\\'));
	if (!InsideDownloads(sDir) || ::GetFileAttributes(sDir) == INVALID_FILE_ATTRIBUTES)
		return false;
	std::vector<CString> files;
	if (!FilesOf(sDir, files))
		return false;
	// another video in it: the folder is still in use. The short samples some bring do not count.
	for (const CString &sFile : files)
		if (IsVideo(sFile) && CString(NameOf(sFile)).MakeLower().Find(_T("sample")) < 0)
			return false;

	// What is left and belongs to a torrent of the list is part of what that torrent offers:
	// without it the torrent would break. It is kept for the torrent with the state of the engine.
	struct STorrentRef
	{
		std::string id;
		int index;
		bool finished;
	};
	std::map<CString, STorrentRef> ofTorrents;
	for (const STorrentStatus &st : theTorrents.GetStatus())
		for (const STorrentFile &f : theTorrents.GetFiles(st.id))
			ofTorrents[CString(f.fullPath.c_str()).MakeLower()] = STorrentRef{st.id, f.index, st.finished};
	for (const CString &sFile : files) {
		const auto it = ofTorrents.find(CString(sFile).MakeLower());
		if (it != ofTorrents.end() && !it->second.finished)
			return false;	// still on its way
	}
	bool bOthers = false;
	for (const CString &sFile : files) {
		const auto it = ofTorrents.find(CString(sFile).MakeLower());
		if (it == ofTorrents.end()) {
			bOthers = true;
			continue;
		}
		const CString sKeep(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("torrents\\files\\") + CString(it->second.id.c_str()) + sFile.Mid(sDir.GetLength()));
		::SHCreateDirectoryEx(NULL, FolderOf(sKeep), NULL);
		std::wstring error;
		if (!theTorrents.Engine().RenameFile(it->second.id, it->second.index, std::wstring((LPCTSTR)sKeep), error)) {
			AddLogLine(false, _T("Folder kept, a file of a torrent could not be put aside: %s (%s)"), (LPCTSTR)sFile, error.c_str());
			return false;
		}
	}
	if (bOthers) {
		// to the Recycle Bin: what Pegaso takes for leftovers may matter to the user
		std::vector<TCHAR> from((LPCTSTR)sDir, (LPCTSTR)sDir + sDir.GetLength());
		from.push_back(_T('\0'));
		from.push_back(_T('\0'));
		SHFILEOPSTRUCT op = {};
		op.wFunc = FO_DELETE;
		op.pFrom = from.data();
		op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_WANTNUKEWARNING;
		if (::SHFileOperation(&op) != 0 || op.fAnyOperationsAborted)
			return false;
	} else
		RemoveEmptyTree(sDir);
	if (::GetFileAttributes(sDir) != INVALID_FILE_ATTRIBUTES)
		return false;
	// the folders above it which are left with nothing
	for (CString sUp(FolderOf(sDir)); InsideDownloads(sUp) && IsEmptyFolder(sUp); sUp = FolderOf(sUp))
		if (!RemoveFolder(sUp))
			break;
	AddLogLine(false, _T("Folder removed: %s"), (LPCTSTR)sDir);
	return true;
}

void PegasoRename::Moved(const CString &sOldPath, const CString &sNewPath)
{
	const CString sNewDir(FolderOf(sNewPath));
	const CString sOldDir(FolderOf(sOldPath));
	// eD2K and Kad: the file is known by its hash, which is of its contents. The file which
	// eMule knows gets its new name and place, so that nothing has to be hashed again.
	CKnownFile *pKnown = NULL;
	CKnownFilesMap shared;
	theApp.sharedfiles->CopySharedFileMap(shared);
	for (POSITION pos = shared.GetStartPosition(); pos != NULL && pKnown == NULL;) {
		CCKey key;
		CKnownFile *pFile;
		shared.GetNextAssoc(pos, key, pFile);
		if (pFile != NULL && !pFile->IsPartFile() && pFile->GetFilePath().CompareNoCase(sOldPath) == 0)
			pKnown = pFile;
	}
	const bool bNewShared = theApp.sharedfiles->ShouldBeShared(sNewDir, sNewPath, false);
	if (pKnown != NULL) {
		const bool bSlash = pKnown->GetPath().Right(1) == _T("\\");
		theApp.sharedfiles->RemoveKeywords(pKnown);
		pKnown->SetFileName(NameOf(sNewPath));
		pKnown->SetPath(bSlash ? sNewDir + _T('\\') : sNewDir);
		pKnown->SetFilePath(sNewPath);
		theApp.sharedfiles->AddKeywords(pKnown);
		if (!bNewShared) {
			// the new folder is not a shared one: the file is shared on its own
			theApp.sharedfiles->AddSingleSharedFile(sNewPath, true);
			theApp.sharedfiles->Save();
		}
		theApp.sharedfiles->UpdateFile(pKnown);
		theApp.sharedfiles->RepublishFile(pKnown);	// the servers and Kad learn the new name
	} else if (theApp.sharedfiles->ShouldBeShared(sOldDir, sOldPath, false)) {
		// it was to be shared, and eMule had not got to it yet
		if (bNewShared)
			theApp.sharedfiles->AddFileFromNewlyCreatedCollection(sNewPath);
		else {
			theApp.sharedfiles->AddSingleSharedFile(sNewPath);
			theApp.sharedfiles->Save();
		}
	}
	AddLogLine(false, _T("Renamed: %s -> %s"), (LPCTSTR)sOldPath, (LPCTSTR)sNewPath);
}

// ---------------------------------------------------------------- CPegasoRenameDlg

IMPLEMENT_DYNAMIC(CPegasoRenameDlg, CDialog)

BEGIN_MESSAGE_MAP(CPegasoRenameDlg, CDialog)
	ON_BN_CLICKED(IDC_RN_LOOKUP, OnOK)
	ON_BN_CLICKED(IDC_RN_KEYS, OnBnClickedKeys)
	ON_BN_CLICKED(IDC_RN_APPLY, OnBnClickedApply)
	ON_BN_CLICKED(IDC_RN_MOVIEDIR_BROWSE, OnBnClickedMoviesDir)
	ON_BN_CLICKED(IDC_RN_SERIESDIR_BROWSE, OnBnClickedSeriesDir)
	ON_NOTIFY(LVN_ENDLABELEDIT, IDC_RN_LIST, OnLvnEndLabelEdit)
	ON_WM_TIMER()
END_MESSAGE_MAP()

namespace
{
	CPegasoRenameDlg *s_pOpen;
}

CPegasoRenameDlg::CPegasoRenameDlg(CWnd *pParent)
	: CDialog(CPegasoRenameDlg::IDD, pParent)
	, m_eMove(MS_IDLE)
	, m_uNext()
	, m_uTotal()
	, m_uFinished()
	, m_iDone()
	, m_iFailed()
	, m_uSize()
	, m_iTorrentFile()
	, m_bWasPaused()
	, m_dwSince()
{
}

CPegasoRenameDlg::~CPegasoRenameDlg()
{
	if (m_job)
		m_job->cancel = true;
	if (m_move)
		m_move->cancel = true;	// a copy under way is given up; the file stays where it was
}

void CPegasoRenameDlg::Open(CWnd *pParent)
{
	if (s_pOpen != NULL) {
		s_pOpen->ShowWindow(SW_RESTORE);
		s_pOpen->SetForegroundWindow();
		return;
	}
	s_pOpen = new CPegasoRenameDlg(pParent);
	if (!s_pOpen->Create(CPegasoRenameDlg::IDD, pParent)) {
		delete s_pOpen;	// not destroyed as a window: there never was one
		s_pOpen = NULL;
		return;
	}
	s_pOpen->ShowWindow(SW_SHOW);
}

void CPegasoRenameDlg::OnCancel()
{
	if (m_eMove != MS_IDLE) {
		SetDlgItemText(IDC_RN_STATUS, GetPegasoString(PS_RN_WAIT));
		return;
	}
	DestroyWindow();
}

void CPegasoRenameDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (s_pOpen == this)
		s_pOpen = NULL;
	delete this;
}

void CPegasoRenameDlg::EnableButtons(bool bEnable)
{
	GetDlgItem(IDC_RN_APPLY)->EnableWindow(bEnable);
	GetDlgItem(IDC_RN_LOOKUP)->EnableWindow(bEnable);
	GetDlgItem(IDC_RN_KEYS)->EnableWindow(bEnable);
}

BOOL CPegasoRenameDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	m_list.SubclassDlgItem(IDC_RN_LIST, this);
	m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES);

	SetWindowText(GetPegasoString(PS_RN_TITLE));
	SetDlgItemText(IDC_RN_SERIES_LBL, GetPegasoString(PS_RN_SERIES));
	SetDlgItemText(IDC_RN_MOVIES_LBL, GetPegasoString(PS_RN_MOVIES));
	SetDlgItemText(IDC_RN_LOOKUP, GetPegasoString(PS_RN_LOOKUP));
	SetDlgItemText(IDC_RN_MOVIEDIR_LBL, GetPegasoString(PS_RN_MOVIEDIR));
	SetDlgItemText(IDC_RN_SERIESDIR_LBL, GetPegasoString(PS_RN_SERIESDIR));
	SetDlgItemText(IDC_RN_KEYS, GetPegasoString(PS_RN_KEYS));
	SetDlgItemText(IDC_RN_APPLY, GetPegasoString(PS_RN_APPLY));
	SetDlgItemText(IDCANCEL, GetPegasoString(PS_WS_CLOSE));

	// the order of the lists is the one of the values kept in the preferences
	CComboBox *pSeries = static_cast<CComboBox*>(GetDlgItem(IDC_RN_SERIES_DB));
	static const ERenameDb aSeries[] = {RDB_TVMAZE, RDB_TMDB, RDB_TVDB, RDB_ANIDB};
	for (ERenameDb db : aSeries)
		pSeries->SetItemData(pSeries->AddString(DbName(db)), (DWORD_PTR)db);
	pSeries->SetCurSel(min(3, max(0, (int)theApp.GetProfileInt(_T("eMule"), _T("RenameSeriesDb"), 0))));
	CComboBox *pMovies = static_cast<CComboBox*>(GetDlgItem(IDC_RN_MOVIES_DB));
	static const ERenameDb aMovies[] = {RDB_TMDB, RDB_OMDB};
	for (ERenameDb db : aMovies)
		pMovies->SetItemData(pMovies->AddString(DbName(db)), (DWORD_PTR)db);
	pMovies->SetCurSel(min(1, max(0, (int)theApp.GetProfileInt(_T("eMule"), _T("RenameMoviesDb"), 0))));
	SetDlgItemText(IDC_RN_CLEAN, GetPegasoString(PS_RN_CLEAN));
	CheckDlgButton(IDC_RN_CLEAN, theApp.GetProfileInt(_T("eMule"), _T("RenameClean"), 1) != 0 ? BST_CHECKED : BST_UNCHECKED);
	SetDlgItemText(IDC_RN_MOVIEDIR, ProfileText(_T("RenameMoviesDir")));
	SetDlgItemText(IDC_RN_SERIESDIR, ProfileText(_T("RenameSeriesDir")));

	CRect rc;
	m_list.GetClientRect(&rc);
	const int iState = 150;
	const int iName = (rc.Width() - iState - ::GetSystemMetrics(SM_CXVSCROLL)) / 2;
	m_list.InsertColumn(0, GetPegasoString(PS_RN_COL_NEW), LVCFMT_LEFT, iName);
	m_list.InsertColumn(1, GetPegasoString(PS_RN_COL_OLD), LVCFMT_LEFT, iName);
	m_list.InsertColumn(2, GetPegasoString(PS_RN_COL_STATE), LVCFMT_LEFT, iState);

	static_cast<CProgressCtrl*>(GetDlgItem(IDC_RN_PROGRESS))->SetRange(0, 1000);
	GetDlgItem(IDC_RN_PROGRESS)->ShowWindow(SW_HIDE);
	Scan();
	if (m_rows.empty())
		SetDlgItemText(IDC_RN_STATUS, GetPegasoString(PS_RN_NOFILES));
	return TRUE;
}

// The finished videos: what is in the folders downloads end up in, except the files of
// torrents which are still on their way.
void CPegasoRenameDlg::Scan()
{
	std::set<CString> busy;
	for (const STorrentStatus &st : theTorrents.GetStatus())
		if (!st.finished)
			for (const STorrentFile &f : theTorrents.GetFiles(st.id))
				busy.insert(CString(f.fullPath.c_str()).MakeLower());

	std::vector<CString> folders;
	folders.push_back(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i)
		folders.push_back(thePrefs.GetCatPath(i));
	std::set<CString> seen;
	// folders inside them as well (a torrent often brings its own), three levels at most
	for (size_t i = 0; i < folders.size() && folders.size() < 2000; ++i) {
		CString sDir(folders[i]);
		int iDepth = 0;
		const int iMark = sDir.Find(_T('|'));
		if (iMark >= 0) {
			iDepth = _ttoi(sDir.Left(iMark));
			sDir = sDir.Mid(iMark + 1);
		}
		sDir.TrimRight(_T('\\'));
		if (sDir.IsEmpty() || !seen.insert(CString(sDir).MakeLower()).second)
			continue;
		CFileFind ff;
		for (BOOL bMore = ff.FindFile(sDir + _T("\\*")); bMore;) {
			bMore = ff.FindNextFile();
			if (ff.IsDots() || ff.IsHidden() || ff.IsSystem())
				continue;
			if (ff.IsDirectory()) {
				if (iDepth < 3) {
					CString sSub;
					sSub.Format(_T("%d|%s"), iDepth + 1, (LPCTSTR)ff.GetFilePath());
					folders.push_back(sSub);
				}
			} else if (PegasoRename::IsVideo(ff.GetFilePath()) && busy.count(CString(ff.GetFilePath()).MakeLower()) == 0 && m_rows.size() < 5000) {
				SRow row;
				row.path = ff.GetFilePath();
				row.name = PegasoRename::Parse(ff.GetFileName());
				row.target = PegasoRename::Build(row.name, Extension(row.path));
				m_rows.push_back(row);
			}
		}
	}
	std::sort(m_rows.begin(), m_rows.end(), [](const SRow &a, const SRow &b) { return a.path.CompareNoCase(b.path) < 0; });
	for (int i = 0; i < (int)m_rows.size(); ++i) {
		m_list.InsertItem(i, m_rows[(size_t)i].target);
		ShowRow(i);
	}
}

void CPegasoRenameDlg::ShowRow(int iRow)
{
	const SRow &row = m_rows[(size_t)iRow];
	m_list.SetItemText(iRow, 0, row.target);
	m_list.SetItemText(iRow, 1, NameOf(row.path));
	m_list.SetItemText(iRow, 2, !row.state.IsEmpty() ? row.state : CString(GetPegasoString(row.name.series ? PS_RN_SERIE : PS_RN_MOVIE)));
}

void CPegasoRenameDlg::SavePrefs()
{
	theApp.WriteProfileInt(_T("eMule"), _T("RenameSeriesDb"), static_cast<CComboBox*>(GetDlgItem(IDC_RN_SERIES_DB))->GetCurSel());
	theApp.WriteProfileInt(_T("eMule"), _T("RenameMoviesDb"), static_cast<CComboBox*>(GetDlgItem(IDC_RN_MOVIES_DB))->GetCurSel());
	theApp.WriteProfileInt(_T("eMule"), _T("RenameClean"), IsDlgButtonChecked(IDC_RN_CLEAN) ? 1 : 0);
	CString s;
	GetDlgItemText(IDC_RN_MOVIEDIR, s);
	WriteProfileText(_T("RenameMoviesDir"), s.Trim());
	GetDlgItemText(IDC_RN_SERIESDIR, s);
	WriteProfileText(_T("RenameSeriesDir"), s.Trim());
}

// looks the names up
void CPegasoRenameDlg::OnOK()
{
	if (m_rows.empty() || (m_job && !m_job->done) || m_eMove != MS_IDLE)
		return;
	if (theNetGuard.IsBlocked()) {
		SetDlgItemText(IDC_RN_STATUS, GetPegasoString(PS_WS_BLOCKED));
		return;
	}
	SavePrefs();
	SRenameEnv env;
	env.keyTmdb = ProfileSecret(_T("RenameKeyTmdb"));
	env.keyOmdb = ProfileSecret(_T("RenameKeyOmdb"));
	env.keyTvdb = ProfileSecret(_T("RenameKeyTvdb"));
	env.testBase = theApp.GetProfileString(_T("eMule"), _T("RenameTestBase"), _T(""));
	env.configDir = thePrefs.GetMuleDirectory(EMULE_CONFIGDIR);
	Languages(env);
	CComboBox *pSeries = static_cast<CComboBox*>(GetDlgItem(IDC_RN_SERIES_DB));
	CComboBox *pMovies = static_cast<CComboBox*>(GetDlgItem(IDC_RN_MOVIES_DB));
	const ERenameDb dbSeries = (ERenameDb)pSeries->GetItemData(max(0, pSeries->GetCurSel()));
	const ERenameDb dbMovies = (ERenameDb)pMovies->GetItemData(max(0, pMovies->GetCurSel()));

	std::vector<SRenameItem> items;
	for (int i = 0; i < (int)m_rows.size(); ++i) {
		SRow &row = m_rows[(size_t)i];
		if (row.path.IsEmpty())
			continue;	// renamed already
		// asked again from what the name of the file says, not from an earlier answer
		row.name = PegasoRename::Parse(NameOf(row.path));
		if (row.name.title.IsEmpty())
			continue;
		row.state = GetPegasoString(PS_RN_SEARCHING);
		ShowRow(i);
		items.push_back(SRenameItem{i, row.name});
	}
	if (items.empty())
		return;
	m_job = std::make_shared<SRenameJob>();
	SetDlgItemText(IDC_RN_STATUS, GetPegasoString(PS_RN_SEARCHING));
	try {
		std::thread(LookupWorker, m_job, env, items, dbSeries, dbMovies).detach();
	} catch (const std::system_error&) {
		m_job->done = true;
	}
	SetTimer(TIMER_POLL, 300, NULL);
}

void CPegasoRenameDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TIMER_MOVE) {
		MoveTick();
		return;
	}
	if (nIDEvent != TIMER_POLL || !m_job) {
		CDialog::OnTimer(nIDEvent);
		return;
	}
	const bool bDone = m_job->done;	// read first: nothing is added after it
	std::vector<SRenameResult> results;
	{
		std::lock_guard<std::mutex> lock(m_job->mtx);
		results.swap(m_job->results);
	}
	for (const SRenameResult &r : results) {
		if (r.row < 0 || (size_t)r.row >= m_rows.size())
			continue;
		SRow &row = m_rows[(size_t)r.row];
		row.found = r.code == RC_FOUND;
		if (!row.found)	// not the name an earlier answer gave
			row.target = PegasoRename::Build(row.name, Extension(row.path));
		switch (r.code) {
		case RC_FOUND:
			row.name = r.name;
			row.target = PegasoRename::Build(row.name, Extension(row.path));
			row.state = DbName(r.db);
			break;
		case RC_NOT_FOUND:
			row.state.Format(_T("%s: %s"), DbName(r.db), GetPegasoString(PS_RN_NOTFOUND));
			break;
		case RC_NEED_KEY:
			row.state.Format(GetPegasoString(PS_RN_NEEDKEY), DbName(r.db));
			break;
		case RC_BLOCKED:
			row.state = GetPegasoString(PS_RN_ANIDB);
			break;
		default:
			row.state.Format(_T("%s: %s"), DbName(r.db), (LPCTSTR)r.text);
		}
		ShowRow(r.row);
		// ticked: what was found and would change
		m_list.SetCheck(r.row, row.found && row.target != NameOf(row.path));
	}
	if (bDone) {
		KillTimer(TIMER_POLL);
		int iFound = 0;
		for (const SRow &row : m_rows)
			iFound += row.found ? 1 : 0;
		CString sStatus;
		sStatus.Format(GetPegasoString(PS_RN_FOUND), iFound, (int)m_rows.size());
		for (const SRow &row : m_rows)
			if (row.state == GetPegasoString(PS_RN_ANIDB)) {
				sStatus = row.state + _T(' ') + thePrefs.GetMuleDirectory(EMULE_CONFIGDIR);	// where to leave the file
				break;
			}
		SetDlgItemText(IDC_RN_STATUS, sStatus);
	}
}

// the new name can be corrected by hand: a slow click on it, or F2
void CPegasoRenameDlg::OnLvnEndLabelEdit(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const NMLVDISPINFO *pInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
	*pResult = FALSE;
	if (pInfo->item.pszText == NULL || pInfo->item.iItem < 0 || (size_t)pInfo->item.iItem >= m_rows.size())
		return;
	SRow &row = m_rows[(size_t)pInfo->item.iItem];
	CString sName(Safe(pInfo->item.pszText));
	if (sName.IsEmpty() || row.path.IsEmpty())
		return;
	if (Extension(sName).CompareNoCase(Extension(row.path)) != 0)
		sName += Extension(row.path);	// the extension is not for changing
	row.target = sName;
	m_list.SetItemText(pInfo->item.iItem, 0, sName);
	m_list.SetCheck(pInfo->item.iItem, sName != NameOf(row.path));
}

void CPegasoRenameDlg::OnBnClickedApply()
{
	if ((m_job && !m_job->done) || m_eMove != MS_IDLE)
		return;
	SavePrefs();
	GetDlgItemText(IDC_RN_MOVIEDIR, m_sMoviesDir);
	GetDlgItemText(IDC_RN_SERIESDIR, m_sSeriesDir);
	m_sMoviesDir.Trim();
	m_sSeriesDir.Trim();
	m_sMoviesDir.TrimRight(_T('\\'));
	m_sSeriesDir.TrimRight(_T('\\'));
	m_queue.clear();
	m_left.clear();
	m_uNext = 0;
	m_uTotal = m_uFinished = 0;
	m_iDone = m_iFailed = 0;
	for (int i = 0; i < (int)m_rows.size(); ++i) {
		const SRow &row = m_rows[(size_t)i];
		if (!m_list.GetCheck(i) || row.path.IsEmpty() || row.target.IsEmpty())
			continue;
		m_queue.push_back(i);
		WIN32_FILE_ATTRIBUTE_DATA fad;
		if (::GetFileAttributesEx(row.path, GetFileExInfoStandard, &fad))
			m_uTotal += ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
	}
	if (m_queue.empty())
		return;
	EnableButtons(false);
	static_cast<CProgressCtrl*>(GetDlgItem(IDC_RN_PROGRESS))->SetPos(0);
	GetDlgItem(IDC_RN_PROGRESS)->ShowWindow(SW_SHOW);
	SetTimer(TIMER_MOVE, 100, NULL);
	NextMove();
}

// the next file of the queue; when there is none left, the account of what was done
void CPegasoRenameDlg::NextMove()
{
	m_eMove = MS_IDLE;
	m_move.reset();
	while (m_uNext < m_queue.size()) {
		const int iRow = m_queue[m_uNext];
		SRow &row = m_rows[(size_t)iRow];
		const CString &sMoveTo(row.name.series ? m_sSeriesDir : m_sMoviesDir);
		m_sFrom = row.path;
		m_sTo = (sMoveTo.IsEmpty() ? FolderOf(row.path) : sMoveTo) + _T('\\') + row.target;
		m_uSize = 0;
		WIN32_FILE_ATTRIBUTE_DATA fad;
		if (::GetFileAttributesEx(m_sFrom, GetFileExInfoStandard, &fad))
			m_uSize = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;

		CString sError;
		bool bOk = m_sFrom == m_sTo || PegasoRename::Prepare(m_sFrom, m_sTo, sError);
		// a file of a torrent: the torrent lets go of it first, and learns its new place afterwards
		m_torrent.clear();
		m_bWasPaused = false;
		for (const STorrentStatus &st : theTorrents.GetStatus()) {
			for (const STorrentFile &f : theTorrents.GetFiles(st.id))
				if (m_sFrom.CompareNoCase(f.fullPath.c_str()) == 0) {
					m_torrent = st.id;
					m_iTorrentFile = f.index;
					m_bWasPaused = st.state == TS_PAUSED;
					if (!st.finished) {
						sError = GetPegasoString(PS_RN_BUSY);
						bOk = false;
					}
					break;
				}
			if (!m_torrent.empty())
				break;
		}
		if (!bOk) {
			++m_iFailed;
			row.state = sError;
			ShowRow(iRow);
			m_uFinished += m_uSize;
			++m_uNext;
			continue;
		}
		CString sStatus;
		sStatus.Format(GetPegasoString(PS_RN_MOVING), (int)m_uNext + 1, (int)m_queue.size(), (LPCTSTR)row.target);
		SetDlgItemText(IDC_RN_STATUS, sStatus);
		m_list.EnsureVisible(iRow, FALSE);
		if (m_sFrom == m_sTo) {
			m_move = std::make_shared<SMoveJob>();	// nothing to move: already there
			m_move->ok = true;
			m_move->done = true;
			m_eMove = MS_MOVING;
		} else if (!m_torrent.empty() && !m_bWasPaused) {
			theTorrents.Pause(m_torrent);
			m_dwSince = ::GetTickCount();
			m_eMove = MS_PAUSING;
		} else
			StartMove();
		return;
	}

	KillTimer(TIMER_MOVE);
	// the folders the videos were taken out of
	if (IsDlgButtonChecked(IDC_RN_CLEAN)) {
		bool bGone = false;
		for (const CString &sLeft : m_left)
			bGone |= PegasoRename::CleanFolder(sLeft);
		if (bGone)
			theApp.sharedfiles->Reload();	// what was in them is not shared any more
	}
	m_left.clear();
	GetDlgItem(IDC_RN_PROGRESS)->ShowWindow(SW_HIDE);
	EnableButtons(true);
	CString sStatus;
	sStatus.Format(GetPegasoString(PS_RN_SUMMARY), m_iDone, m_iFailed);
	SetDlgItemText(IDC_RN_STATUS, sStatus);
}

void CPegasoRenameDlg::StartMove()
{
	m_move = std::make_shared<SMoveJob>();
	m_eMove = MS_MOVING;
	try {
		std::thread(MoveWorker, m_move, m_sFrom, m_sTo).detach();
	} catch (const std::system_error&) {
		m_move->error = ERROR_NOT_ENOUGH_MEMORY;
		m_move->done = true;
	}
}

void CPegasoRenameDlg::MoveTick()
{
	if (m_eMove == MS_PAUSING) {
		// when the engine says the torrent has closed its files - or, should it never say so, after a while
		if (theTorrents.Engine().IsReleased(m_torrent) || ::GetTickCount() - m_dwSince > 20000)
			StartMove();
		return;
	}
	if (m_eMove != MS_MOVING || !m_move)
		return;
	if (m_uTotal > 0) {
		const ULONGLONG uNow = min(m_uTotal, m_uFinished + (ULONGLONG)m_move->moved);
		static_cast<CProgressCtrl*>(GetDlgItem(IDC_RN_PROGRESS))->SetPos((int)(uNow * 1000 / m_uTotal));
	}
	if (m_move->done)
		CompleteMove();
}

void CPegasoRenameDlg::CompleteMove()
{
	const int iRow = m_queue[m_uNext];
	SRow &row = m_rows[(size_t)iRow];
	bool bOk = m_move->ok;
	CString sError(bOk ? CString() : GetErrorMessage(m_move->error));
	if (!m_torrent.empty()) {
		if (bOk && m_sFrom != m_sTo) {
			// the file is at its new place already: the engine only takes note of it
			std::wstring error;
			if (!theTorrents.Engine().RenameFile(m_torrent, m_iTorrentFile, std::wstring((LPCTSTR)m_sTo), error)) {
				bOk = false;
				sError = error.c_str();
				::MoveFileEx(m_sTo, m_sFrom, MOVEFILE_COPY_ALLOWED);	// back to where the torrent expects it
			}
		}
		if (!m_bWasPaused)
			theTorrents.Resume(m_torrent);
	}
	if (bOk) {
		PegasoRename::Moved(m_sFrom, m_sTo);
		if (FolderOf(m_sFrom).CompareNoCase(FolderOf(m_sTo)) != 0)
			m_left.push_back(FolderOf(m_sFrom));
		++m_iDone;
		row.path = m_sTo;
		row.state = GetPegasoString(PS_RN_DONE);
		m_list.SetCheck(iRow, FALSE);
	} else {
		++m_iFailed;
		row.state = sError;
	}
	ShowRow(iRow);
	m_uFinished += m_uSize;
	++m_uNext;
	NextMove();
}

void CPegasoRenameDlg::OnBnClickedKeys()
{
	CPegasoRenameKeysDlg dlg(this);
	dlg.DoModal();
}

void CPegasoRenameDlg::OnBnClickedMoviesDir()
{
	TCHAR szPath[MAX_PATH] = {};
	GetDlgItemText(IDC_RN_MOVIEDIR, szPath, _countof(szPath));
	if (SelectDir(GetSafeHwnd(), szPath, GetPegasoString(PS_RN_MOVIEDIR)))
		SetDlgItemText(IDC_RN_MOVIEDIR, szPath);
}

void CPegasoRenameDlg::OnBnClickedSeriesDir()
{
	TCHAR szPath[MAX_PATH] = {};
	GetDlgItemText(IDC_RN_SERIESDIR, szPath, _countof(szPath));
	if (SelectDir(GetSafeHwnd(), szPath, GetPegasoString(PS_RN_SERIESDIR)))
		SetDlgItemText(IDC_RN_SERIESDIR, szPath);
}

// ---------------------------------------------------------------- CPegasoRenameKeysDlg

IMPLEMENT_DYNAMIC(CPegasoRenameKeysDlg, CDialog)

BEGIN_MESSAGE_MAP(CPegasoRenameKeysDlg, CDialog)
	ON_BN_CLICKED(IDC_RK_TMDB_OPEN, OnBnClickedTmdb)
	ON_BN_CLICKED(IDC_RK_OMDB_OPEN, OnBnClickedOmdb)
	ON_BN_CLICKED(IDC_RK_TVDB_OPEN, OnBnClickedTvdb)
END_MESSAGE_MAP()

// the page of each service where its key is asked for, in the browser of the user
void CPegasoRenameKeysDlg::OnBnClickedTmdb()
{
	::ShellExecute(NULL, _T("open"), _T("https://www.themoviedb.org/settings/api"), NULL, NULL, SW_SHOWNORMAL);
}

void CPegasoRenameKeysDlg::OnBnClickedOmdb()
{
	::ShellExecute(NULL, _T("open"), _T("https://www.omdbapi.com/apikey.aspx"), NULL, NULL, SW_SHOWNORMAL);
}

void CPegasoRenameKeysDlg::OnBnClickedTvdb()
{
	::ShellExecute(NULL, _T("open"), _T("https://thetvdb.com/api-information"), NULL, NULL, SW_SHOWNORMAL);
}

CPegasoRenameKeysDlg::CPegasoRenameKeysDlg(CWnd *pParent)
	: CDialog(CPegasoRenameKeysDlg::IDD, pParent)
{
}

BOOL CPegasoRenameKeysDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	SetWindowText(GetPegasoString(PS_RK_TITLE));
	SetDlgItemText(IDC_RK_HINT, GetPegasoString(PS_RK_HINT));
	SetDlgItemText(IDC_RK_TMDB_LBL, _T("TheMovieDB (themoviedb.org)"));
	SetDlgItemText(IDC_RK_OMDB_LBL, _T("OMDb (omdbapi.com)"));
	SetDlgItemText(IDC_RK_TVDB_LBL, _T("TheTVDB (thetvdb.com)"));
	SetDlgItemText(IDC_RK_TMDB_HELP, GetPegasoString(PS_RK_TMDB_HELP));
	SetDlgItemText(IDC_RK_OMDB_HELP, GetPegasoString(PS_RK_OMDB_HELP));
	SetDlgItemText(IDC_RK_TVDB_HELP, GetPegasoString(PS_RK_TVDB_HELP));
	SetDlgItemText(IDC_RK_TMDB_OPEN, GetPegasoString(PS_RK_OPEN));
	SetDlgItemText(IDC_RK_OMDB_OPEN, GetPegasoString(PS_RK_OPEN));
	SetDlgItemText(IDC_RK_TVDB_OPEN, GetPegasoString(PS_RK_OPEN));
	SetDlgItemText(IDC_RK_TMDB, ProfileSecret(_T("RenameKeyTmdb")));
	SetDlgItemText(IDC_RK_OMDB, ProfileSecret(_T("RenameKeyOmdb")));
	SetDlgItemText(IDC_RK_TVDB, ProfileSecret(_T("RenameKeyTvdb")));
	SetDlgItemText(IDCANCEL, GetPegasoString(PS_WS_CLOSE));
	return TRUE;
}

void CPegasoRenameKeysDlg::OnOK()
{
	CString s;
	GetDlgItemText(IDC_RK_TMDB, s);
	WriteProfileSecret(_T("RenameKeyTmdb"), s.Trim());
	GetDlgItemText(IDC_RK_OMDB, s);
	WriteProfileSecret(_T("RenameKeyOmdb"), s.Trim());
	GetDlgItemText(IDC_RK_TVDB, s);
	WriteProfileSecret(_T("RenameKeyTvdb"), s.Trim());
	CDialog::OnOK();
}
